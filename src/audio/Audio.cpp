#include "audio/Audio.h"

#include "audio/Effects.h"
#include "core/Log.h"

#include <SDL3/SDL_stdinc.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace audio {

namespace {

ma_uint64 toMilliseconds(float seconds)
{
    return seconds > 0.0f ? ma_uint64(seconds * 1000.0f + 0.5f) : 0;
}

constexpr int kBusCount = 3;

int busIndex(Bus bus)
{
    return std::clamp(int(bus), 0, kBusCount - 1);
}

// Velocity of something the game moves with setPosition/setListener, estimated
// once per frame from how far it moved.
struct Motion {
    glm::vec3 position{0.0f};
    glm::vec3 last{0.0f};
    glm::vec3 velocity{0.0f};
    bool automatic = true; // false once the game sets the velocity itself
    bool hasLast = false;

    void track(float dt, float speedOfSound)
    {
        if (!automatic)
            return;
        if (hasLast && dt > 0.0f) {
            const glm::vec3 measured = (position - last) / dt;
            if (glm::length(measured) > speedOfSound) {
                velocity = glm::vec3(0.0f); // faster than sound: a teleport, not real motion
            } else {
                // Smooth over ~0.1 s, so frame-time jitter doesn't wobble the pitch.
                velocity += (measured - velocity) * (1.0f - std::exp(-dt / 0.08f));
            }
        }
        last = position;
        hasLast = true;
    }
};

// Same formula as miniaudio's (from the OpenAL spec), for dopplerPitch().
// `toListener` points from the sound to the listener.
float dopplerRatio(glm::vec3 toListener, glm::vec3 soundVelocity, glm::vec3 listenerVelocity, float c, float factor)
{
    const float length = glm::length(toListener);
    if (length == 0.0f || factor <= 0.0f)
        return 1.0f;
    const float vls = std::min(glm::dot(toListener, listenerVelocity) / length, c / factor);
    const float vss = std::min(glm::dot(toListener, soundVelocity) / length, c / factor);
    return (c - factor * vls) / (c - factor * vss);
}

// Keeps a speed low enough that the pitch can't run away: miniaudio's formula
// divides by (speedOfSound - factor * speed), which reaches zero at the speed of sound.
glm::vec3 limitSpeed(glm::vec3 v, float speedOfSound, float factor)
{
    const float maxSpeed = 0.3f * speedOfSound / std::max(factor, 1e-3f);
    const float speed = glm::length(v);
    return speed > maxSpeed ? v * (maxSpeed / speed) : v;
}

// What the game last asked for on each bus (the audio thread glides towards it).
struct BusSettings {
    float lowPass = kLowPassOff;
    float highPass = kHighPassOff;
    EchoParams echo;
};

} // namespace

ReverbParams ReverbParams::preset(ReverbPreset preset)
{
    switch (preset) {
    case ReverbPreset::Room: return {.level = 0.30f, .roomSize = 0.45f, .damping = 0.60f, .width = 0.8f};
    case ReverbPreset::Hall: return {.level = 0.40f, .roomSize = 0.80f, .damping = 0.40f, .width = 1.0f};
    case ReverbPreset::Cave: return {.level = 0.55f, .roomSize = 0.92f, .damping = 0.25f, .width = 1.0f};
    case ReverbPreset::Off:
    default: return {};
    }
}

// One playing sound effect. ma_sound must not move in memory, hence unique_ptr.
struct Voice {
    ma_sound sound{};
    std::unique_ptr<detail::VoiceNode> fx; // its own low-pass and reverb send (null without effects)
    bool spatial = false;
    float doppler = 1.0f; // this sound's own factor (× the engine's)
    Motion motion;
    uint32_t id = 0;
    uint64_t order = 0; // start order, used to pick a voice to cut when full
    bool loop = false;
};

struct AudioEngine::Impl {
    AudioDesc desc;
    ma_engine engine{};
    bool engineOk = false;
    ma_sound_group sfxGroup{};
    ma_sound_group musicGroup{};

    // Effects. Without them (a node failed to start) sound still works, unprocessed.
    bool effectsOk = false;
    bool reverbOk = false;
    std::unique_ptr<detail::BusNode> buses[kBusCount];
    std::unique_ptr<detail::ReverbNode> reverbNode;
    BusSettings busSettings[kBusCount];
    ReverbParams reverbSettings;

    std::vector<std::unique_ptr<Voice>> voices;
    // One idle sound per file keeps its decoded samples cached in miniaudio's
    // resource manager, so every later play() starts instantly.
    std::unordered_map<std::string, std::unique_ptr<ma_sound>> cache;

    std::unique_ptr<ma_sound> music;
    std::vector<std::unique_ptr<ma_sound>> fadingMusic; // previous tracks fading out

    uint32_t nextId = 1;
    uint64_t nextOrder = 0;
    float master = 1.0f, sfx = 1.0f, musicVolume = 0.8f;
    bool muted = false;
    std::unordered_set<std::string> warned;

    // Doppler
    float dopplerFactor = 1.0f;
    float speedOfSound = 343.0f;
    Motion listener;
    std::chrono::steady_clock::time_point lastUpdate = std::chrono::steady_clock::now();

    float voiceFactor(const Voice& v) const { return std::max(v.doppler, 0.0f) * dopplerFactor; }

    // Sends velocities (limited, see limitSpeed) to miniaudio.
    void applyMotion()
    {
        float maxFactor = dopplerFactor;
        for (auto& v : voices)
            if (v->spatial) {
                const float f = voiceFactor(*v);
                maxFactor = std::max(maxFactor, f);
                const glm::vec3 vel = limitSpeed(v->motion.velocity, speedOfSound, f);
                ma_sound_set_velocity(&v->sound, vel.x, vel.y, vel.z);
            }
        const glm::vec3 lv = limitSpeed(listener.velocity, speedOfSound, maxFactor);
        ma_engine_listener_set_velocity(&engine, 0, lv.x, lv.y, lv.z);
    }


    void warnOnce(const std::string& path, ma_result result)
    {
        if (warned.insert(path).second)
            LOG_WARN("Audio: can't play '%s': %s", path.c_str(), ma_result_description(result));
    }

    Voice* find(SoundHandle h)
    {
        if (!h)
            return nullptr;
        for (auto& v : voices)
            if (v->id == h.id)
                return v.get();
        return nullptr;
    }

    bool ensureCached(const std::string& path)
    {
        if (cache.count(path))
            return true;
        auto s = std::make_unique<ma_sound>();
        const ma_result r = ma_sound_init_from_file(&engine, path.c_str(),
                                                    MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                                    &sfxGroup, nullptr, s.get());
        if (r != MA_SUCCESS) {
            warnOnce(path, r);
            return false;
        }
        cache.emplace(path, std::move(s));
        return true;
    }

    void applyMasterVolume() { ma_engine_set_volume(&engine, muted ? 0.0f : master); }

    // Builds the effect graph (see Effects.h). On failure everything stays
    // connected straight to the device, without effects.
    void initEffects()
    {
        ma_node_graph* graph = ma_engine_get_node_graph(&engine);
        const uint32_t rate = ma_engine_get_sample_rate(&engine);
        for (auto& bus : buses) {
            bus = std::make_unique<detail::BusNode>();
            if (detail::initBusNode(graph, rate, bus.get()) != MA_SUCCESS) {
                LOG_WARN("Audio: effects unavailable; continuing without them");
                for (auto& b : buses)
                    if (b && b->base.pNodeGraph)
                        detail::uninitBusNode(b.get());
                for (auto& b : buses)
                    b.reset();
                return;
            }
        }
        detail::BusNode* sfxBus = buses[int(Bus::Sfx)].get();
        detail::BusNode* musicBus = buses[int(Bus::Music)].get();
        detail::BusNode* masterBus = buses[int(Bus::Master)].get();
        ma_node_attach_output_bus(masterBus, 0, ma_engine_get_endpoint(&engine), 0);
        ma_node_attach_output_bus(sfxBus, 0, masterBus, 0);
        ma_node_attach_output_bus(musicBus, 0, masterBus, 0);
        ma_node_attach_output_bus(&sfxGroup, 0, sfxBus, 0);
        ma_node_attach_output_bus(&musicGroup, 0, musicBus, 0);
        effectsOk = true;

        reverbNode = std::make_unique<detail::ReverbNode>();
        if (detail::initReverbNode(graph, rate, reverbNode.get()) == MA_SUCCESS) {
            ma_node_attach_output_bus(reverbNode.get(), 0, &sfxGroup, 0); // joins the dry effects
            reverbOk = true;
        } else {
            LOG_WARN("Audio: reverb unavailable at %u Hz", rate);
            reverbNode.reset();
        }
    }

    void releaseVoice(Voice& v)
    {
        ma_sound_uninit(&v.sound);
        if (v.fx)
            detail::uninitVoiceNode(v.fx.get());
    }

    void applyBus(int i, float glide)
    {
        if (!effectsOk)
            return;
        const BusSettings& b = busSettings[i];
        detail::BusNode& node = *buses[i];
        node.lowPass.set(detail::hzToLog2(std::max(b.lowPass, 10.0f)), glide);
        node.highPass.set(detail::hzToLog2(std::max(b.highPass, 10.0f)), glide);
        node.echoDelay.set(std::clamp(b.echo.delaySeconds, 0.01f, 2.0f), glide);
        node.echoFeedback.set(std::clamp(b.echo.feedback, 0.0f, 0.95f), glide);
        node.echoMix.set(std::max(b.echo.mix, 0.0f), glide);
    }

    void applyReverb(float glide)
    {
        if (!reverbOk)
            return;
        const ReverbParams& p = reverbSettings;
        reverbNode->level.set(std::max(p.level, 0.0f), glide);
        reverbNode->roomSize.set(std::clamp(p.roomSize, 0.0f, 1.0f), glide);
        reverbNode->damping.set(std::clamp(p.damping, 0.0f, 1.0f), glide);
        reverbNode->width.set(std::clamp(p.width, 0.0f, 1.0f), glide);
    }

    void retireMusic(float fadeOutSeconds)
    {
        if (!music)
            return;
        if (fadeOutSeconds > 0.0f && ma_sound_is_playing(music.get()))
            ma_sound_stop_with_fade_in_milliseconds(music.get(), toMilliseconds(fadeOutSeconds));
        else
            ma_sound_stop(music.get());
        fadingMusic.push_back(std::move(music));
    }

    ~Impl()
    {
        if (!engineOk)
            return;
        for (auto& v : voices)
            releaseVoice(*v);
        for (auto& [path, s] : cache)
            ma_sound_uninit(s.get());
        for (auto& s : fadingMusic)
            ma_sound_uninit(s.get());
        if (music)
            ma_sound_uninit(music.get());
        if (reverbOk)
            detail::uninitReverbNode(reverbNode.get());
        ma_sound_group_uninit(&musicGroup);
        ma_sound_group_uninit(&sfxGroup);
        if (effectsOk)
            for (auto& bus : buses)
                detail::uninitBusNode(bus.get());
        ma_engine_uninit(&engine);
    }
};

AudioEngine::AudioEngine(const AudioDesc& desc)
    : impl_(std::make_unique<Impl>())
{
    Impl& m = *impl_;
    m.desc = desc;
    m.desc.maxVoices = std::max(desc.maxVoices, 1u);
    // Keep the volumes even when there's no sound, so they read back (and are saved in
    // the player's settings) as they were set.
    m.master = std::max(desc.masterVolume, 0.0f);
    m.sfx = std::max(desc.sfxVolume, 0.0f);
    m.musicVolume = std::max(desc.musicVolume, 0.0f);

    const char* env = SDL_getenv("GAME_AUDIO");
    if (!desc.enabled || (env && env[0] == '0')) {
        LOG_INFO("Audio disabled");
        return;
    }

    ma_engine_config config = ma_engine_config_init();
    if (desc.noDevice) {
        config.noDevice = MA_TRUE;
        config.channels = 2;
        config.sampleRate = desc.noDeviceSampleRate;
    }
    const ma_result r = ma_engine_init(&config, &m.engine);
    if (r != MA_SUCCESS) {
        LOG_WARN("Audio: no output device (%s); continuing without sound", ma_result_description(r));
        return;
    }
    m.engineOk = true;
    ma_sound_group_init(&m.engine, 0, nullptr, &m.sfxGroup);
    ma_sound_group_init(&m.engine, 0, nullptr, &m.musicGroup);
    m.initEffects();

    setMasterVolume(desc.masterVolume);
    setSfxVolume(desc.sfxVolume);
    setMusicVolume(desc.musicVolume);
    setSpeedOfSound(desc.speedOfSound);
    setDopplerFactor(desc.dopplerFactor);

    if (ma_device* device = ma_engine_get_device(&m.engine))
        LOG_INFO("Audio: %s via %s, %u Hz", device->playback.name, ma_get_backend_name(device->pContext->backend),
                 ma_engine_get_sample_rate(&m.engine));
    else
        LOG_INFO("Audio: no-device mode, %u Hz", ma_engine_get_sample_rate(&m.engine));
}

AudioEngine::~AudioEngine() = default;

bool AudioEngine::available() const
{
    return impl_->engineOk;
}

void AudioEngine::update()
{
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - impl_->lastUpdate).count();
    update(dt);
}

void AudioEngine::update(float dtSeconds)
{
    Impl& m = *impl_;
    m.lastUpdate = std::chrono::steady_clock::now();
    if (!m.engineOk)
        return;

    // Doppler: velocities from how far things moved since the last frame.
    for (auto& v : m.voices)
        if (v->spatial)
            v->motion.track(dtSeconds, m.speedOfSound);
    m.listener.track(dtSeconds, m.speedOfSound);
    m.applyMotion();

    // Sounds that reached their end or were stopped (including after a fade).
    auto finished = [](ma_sound* s) { return !ma_sound_is_playing(s); };
    for (auto it = m.voices.begin(); it != m.voices.end();) {
        if (finished(&(*it)->sound)) {
            m.releaseVoice(**it);
            it = m.voices.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m.fadingMusic.begin(); it != m.fadingMusic.end();) {
        if (finished(it->get())) {
            ma_sound_uninit(it->get());
            it = m.fadingMusic.erase(it);
        } else {
            ++it;
        }
    }
}

bool AudioEngine::preload(const std::string& path)
{
    return impl_->engineOk && impl_->ensureCached(path);
}

void AudioEngine::unload(const std::string& path)
{
    Impl& m = *impl_;
    auto it = m.cache.find(path);
    if (it != m.cache.end()) {
        ma_sound_uninit(it->second.get());
        m.cache.erase(it);
    }
}

SoundHandle AudioEngine::play(const std::string& path, const PlayParams& p)
{
    Impl& m = *impl_;
    if (!m.engineOk || !m.ensureCached(path))
        return {};

    // Full? Cut the oldest one-shot sound (looping sounds are never cut).
    if (m.voices.size() >= m.desc.maxVoices) {
        auto oldest = m.voices.end();
        for (auto it = m.voices.begin(); it != m.voices.end(); ++it)
            if (!(*it)->loop && (oldest == m.voices.end() || (*it)->order < (*oldest)->order))
                oldest = it;
        if (oldest == m.voices.end())
            return {};
        m.releaseVoice(**oldest);
        m.voices.erase(oldest);
    }

    auto v = std::make_unique<Voice>();
    ma_uint32 flags = MA_SOUND_FLAG_DECODE;
    if (!p.spatial)
        flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (m.effectsOk) {
        // sound -> its own effects node -> effects group (and the reverb)
        v->fx = std::make_unique<detail::VoiceNode>();
        if (detail::initVoiceNode(ma_engine_get_node_graph(&m.engine), ma_engine_get_sample_rate(&m.engine),
                                  p.lowPassHz, p.reverbSend, v->fx.get()) != MA_SUCCESS)
            return {};
        ma_node_attach_output_bus(v->fx.get(), 0, &m.sfxGroup, 0);
        if (m.reverbOk)
            ma_node_attach_output_bus(v->fx.get(), 1, m.reverbNode.get(), 0);
        flags |= MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
    }
    const ma_result r = ma_sound_init_from_file(&m.engine, path.c_str(), flags, m.effectsOk ? nullptr : &m.sfxGroup,
                                                nullptr, &v->sound);
    if (r != MA_SUCCESS) {
        if (v->fx)
            detail::uninitVoiceNode(v->fx.get());
        m.warnOnce(path, r);
        return {};
    }
    if (v->fx)
        ma_node_attach_output_bus(&v->sound, 0, v->fx.get(), 0);

    ma_sound* s = &v->sound;
    ma_sound_set_volume(s, std::max(p.volume, 0.0f));
    ma_sound_set_pitch(s, std::max(p.pitch, 0.01f));
    ma_sound_set_looping(s, p.loop ? MA_TRUE : MA_FALSE);
    if (p.spatial) {
        ma_sound_set_position(s, p.position.x, p.position.y, p.position.z);
        ma_sound_set_min_distance(s, p.minDistance);
        ma_sound_set_max_distance(s, p.maxDistance);
        ma_sound_set_rolloff(s, p.rolloff);
        v->spatial = true;
        v->doppler = std::max(p.doppler, 0.0f);
        v->motion.position = p.position;
        ma_sound_set_doppler_factor(s, m.voiceFactor(*v));
    } else {
        ma_sound_set_pan(s, std::clamp(p.pan, -1.0f, 1.0f));
    }
    ma_sound_start(s);

    v->id = m.nextId++;
    if (m.nextId == 0)
        m.nextId = 1; // 0 means "no sound"
    v->order = m.nextOrder++;
    v->loop = p.loop;
    const SoundHandle handle{v->id};
    m.voices.push_back(std::move(v));
    return handle;
}

void AudioEngine::stop(SoundHandle h, float fadeOutSeconds)
{
    if (Voice* v = impl_->find(h)) {
        if (fadeOutSeconds > 0.0f)
            ma_sound_stop_with_fade_in_milliseconds(&v->sound, toMilliseconds(fadeOutSeconds));
        else
            ma_sound_stop(&v->sound);
    }
}

void AudioEngine::stopAllSounds()
{
    for (auto& v : impl_->voices)
        ma_sound_stop(&v->sound);
}

bool AudioEngine::isPlaying(SoundHandle h) const
{
    Voice* v = impl_->find(h);
    return v && ma_sound_is_playing(&v->sound);
}

void AudioEngine::setVolume(SoundHandle h, float volume)
{
    if (Voice* v = impl_->find(h))
        ma_sound_set_volume(&v->sound, std::max(volume, 0.0f));
}

void AudioEngine::setPitch(SoundHandle h, float pitch)
{
    if (Voice* v = impl_->find(h))
        ma_sound_set_pitch(&v->sound, std::max(pitch, 0.01f));
}

void AudioEngine::setPan(SoundHandle h, float pan)
{
    if (Voice* v = impl_->find(h))
        ma_sound_set_pan(&v->sound, std::clamp(pan, -1.0f, 1.0f));
}

void AudioEngine::setPosition(SoundHandle h, const glm::vec3& position)
{
    if (Voice* v = impl_->find(h)) {
        ma_sound_set_position(&v->sound, position.x, position.y, position.z);
        v->motion.position = position;
    }
}

void AudioEngine::setVelocity(SoundHandle h, const glm::vec3& velocity)
{
    if (Voice* v = impl_->find(h)) {
        v->motion.automatic = false;
        v->motion.velocity = velocity;
        const glm::vec3 limited = limitSpeed(velocity, impl_->speedOfSound, impl_->voiceFactor(*v));
        ma_sound_set_velocity(&v->sound, limited.x, limited.y, limited.z);
    }
}

float AudioEngine::dopplerPitch(SoundHandle h) const
{
    const Impl& m = *impl_;
    Voice* v = impl_->find(h);
    if (!v || !v->spatial)
        return 1.0f;
    const float f = m.voiceFactor(*v);
    return dopplerRatio(m.listener.position - v->motion.position, limitSpeed(v->motion.velocity, m.speedOfSound, f),
                        limitSpeed(m.listener.velocity, m.speedOfSound, f), m.speedOfSound, f);
}

void AudioEngine::setLowPass(SoundHandle h, float cutoffHz, float glideSeconds)
{
    if (Voice* v = impl_->find(h); v && v->fx)
        v->fx->lowPass.set(detail::hzToLog2(cutoffHz), glideSeconds);
}

void AudioEngine::setReverbSend(SoundHandle h, float amount, float glideSeconds)
{
    if (Voice* v = impl_->find(h); v && v->fx)
        v->fx->send.set(std::max(amount, 0.0f), glideSeconds);
}

bool AudioEngine::playMusic(const std::string& path, const MusicParams& p)
{
    Impl& m = *impl_;
    if (!m.engineOk)
        return false;

    auto s = std::make_unique<ma_sound>();
    const ma_result r = ma_sound_init_from_file(&m.engine, path.c_str(),
                                                MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                                &m.musicGroup, nullptr, s.get());
    if (r != MA_SUCCESS) {
        m.warnOnce(path, r);
        return false;
    }

    m.retireMusic(p.fadeOutPreviousSeconds); // fade out whatever was playing

    ma_sound_set_looping(s.get(), p.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(s.get(), std::max(p.volume, 0.0f));
    if (p.fadeInSeconds > 0.0f)
        ma_sound_set_fade_in_milliseconds(s.get(), 0.0f, 1.0f, toMilliseconds(p.fadeInSeconds));
    ma_sound_start(s.get());
    m.music = std::move(s);
    return true;
}

void AudioEngine::stopMusic(float fadeOutSeconds)
{
    impl_->retireMusic(fadeOutSeconds);
}

void AudioEngine::pauseMusic()
{
    if (impl_->music)
        ma_sound_stop(impl_->music.get()); // keeps the play position
}

void AudioEngine::resumeMusic()
{
    if (impl_->music)
        ma_sound_start(impl_->music.get());
}

bool AudioEngine::isMusicPlaying() const
{
    return impl_->music && ma_sound_is_playing(impl_->music.get());
}

void AudioEngine::setMasterVolume(float volume)
{
    impl_->master = std::max(volume, 0.0f);
    if (impl_->engineOk)
        impl_->applyMasterVolume();
}

void AudioEngine::setSfxVolume(float volume)
{
    impl_->sfx = std::max(volume, 0.0f);
    if (impl_->engineOk)
        ma_sound_group_set_volume(&impl_->sfxGroup, impl_->sfx);
}

void AudioEngine::setMusicVolume(float volume)
{
    impl_->musicVolume = std::max(volume, 0.0f);
    if (impl_->engineOk)
        ma_sound_group_set_volume(&impl_->musicGroup, impl_->musicVolume);
}

float AudioEngine::masterVolume() const { return impl_->master; }
float AudioEngine::sfxVolume() const { return impl_->sfx; }
float AudioEngine::musicVolume() const { return impl_->musicVolume; }

void AudioEngine::setMuted(bool muted)
{
    impl_->muted = muted;
    if (impl_->engineOk)
        impl_->applyMasterVolume();
}

bool AudioEngine::muted() const
{
    return impl_->muted;
}

void AudioEngine::setLowPass(Bus bus, float cutoffHz, float glideSeconds)
{
    const int i = busIndex(bus);
    impl_->busSettings[i].lowPass = std::clamp(cutoffHz, 10.0f, kLowPassOff);
    impl_->applyBus(i, glideSeconds);
}

void AudioEngine::setHighPass(Bus bus, float cutoffHz, float glideSeconds)
{
    const int i = busIndex(bus);
    impl_->busSettings[i].highPass = std::clamp(cutoffHz, kHighPassOff, kLowPassOff);
    impl_->applyBus(i, glideSeconds);
}

void AudioEngine::setEcho(Bus bus, const EchoParams& echo, float glideSeconds)
{
    const int i = busIndex(bus);
    impl_->busSettings[i].echo = echo;
    impl_->applyBus(i, glideSeconds);
}

void AudioEngine::setReverb(const ReverbParams& reverb, float glideSeconds)
{
    impl_->reverbSettings = reverb;
    impl_->applyReverb(glideSeconds);
}

void AudioEngine::resetEffects(float glideSeconds)
{
    for (int i = 0; i < kBusCount; ++i) {
        impl_->busSettings[i] = {};
        impl_->applyBus(i, glideSeconds);
    }
    impl_->reverbSettings = {};
    impl_->applyReverb(glideSeconds);
}

float AudioEngine::lowPass(Bus bus) const { return impl_->busSettings[busIndex(bus)].lowPass; }
float AudioEngine::highPass(Bus bus) const { return impl_->busSettings[busIndex(bus)].highPass; }
EchoParams AudioEngine::echo(Bus bus) const { return impl_->busSettings[busIndex(bus)].echo; }
ReverbParams AudioEngine::reverb() const { return impl_->reverbSettings; }

void AudioEngine::setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up)
{
    Impl& m = *impl_;
    if (!m.engineOk)
        return;
    ma_engine_listener_set_position(&m.engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m.engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m.engine, 0, up.x, up.y, up.z);
    m.listener.position = position;
}

void AudioEngine::setListenerVelocity(const glm::vec3& velocity)
{
    Impl& m = *impl_;
    m.listener.automatic = false;
    m.listener.velocity = velocity;
    if (m.engineOk)
        m.applyMotion();
}

void AudioEngine::setDopplerFactor(float factor)
{
    Impl& m = *impl_;
    m.dopplerFactor = std::max(factor, 0.0f);
    if (!m.engineOk)
        return;
    for (auto& v : m.voices)
        if (v->spatial)
            ma_sound_set_doppler_factor(&v->sound, m.voiceFactor(*v));
    m.applyMotion();
}

float AudioEngine::dopplerFactor() const
{
    return impl_->dopplerFactor;
}

void AudioEngine::setSpeedOfSound(float unitsPerSecond)
{
    Impl& m = *impl_;
    m.speedOfSound = std::max(unitsPerSecond, 1.0f);
    if (m.engineOk)
        ma_spatializer_listener_set_speed_of_sound(&m.engine.listeners[0], m.speedOfSound);
}

float AudioEngine::speedOfSound() const
{
    return impl_->speedOfSound;
}

uint32_t AudioEngine::sampleRate() const
{
    return impl_->engineOk ? ma_engine_get_sample_rate(&impl_->engine) : 0;
}

uint32_t AudioEngine::activeSounds() const
{
    return uint32_t(impl_->voices.size());
}

uint64_t AudioEngine::readFrames(float* out, uint64_t frameCount)
{
    Impl& m = *impl_;
    if (!m.engineOk || !m.desc.noDevice) {
        LOG_WARN("AudioEngine::readFrames needs AudioDesc::noDevice = true");
        return 0;
    }
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&m.engine, out, frameCount, &read);
    return read;
}

} // namespace audio
