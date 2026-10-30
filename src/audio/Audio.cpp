#include "audio/Audio.h"

#include "core/Log.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <miniaudio.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace audio {

namespace {

ma_uint64 toMilliseconds(float seconds)
{
    return seconds > 0.0f ? ma_uint64(seconds * 1000.0f + 0.5f) : 0;
}

} // namespace

// One playing sound effect. ma_sound must not move in memory, hence unique_ptr.
struct Voice {
    ma_sound sound{};
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
            ma_sound_uninit(&v->sound);
        for (auto& [path, s] : cache)
            ma_sound_uninit(s.get());
        for (auto& s : fadingMusic)
            ma_sound_uninit(s.get());
        if (music)
            ma_sound_uninit(music.get());
        ma_sound_group_uninit(&musicGroup);
        ma_sound_group_uninit(&sfxGroup);
        ma_engine_uninit(&engine);
    }
};

AudioEngine::AudioEngine(const AudioDesc& desc)
    : impl_(std::make_unique<Impl>())
{
    Impl& m = *impl_;
    m.desc = desc;
    m.desc.maxVoices = std::max(desc.maxVoices, 1u);

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

    setMasterVolume(desc.masterVolume);
    setSfxVolume(desc.sfxVolume);
    setMusicVolume(desc.musicVolume);

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
    Impl& m = *impl_;
    if (!m.engineOk)
        return;

    // Sounds that reached their end or were stopped (including after a fade).
    auto finished = [](ma_sound* s) { return !ma_sound_is_playing(s); };
    for (auto it = m.voices.begin(); it != m.voices.end();) {
        if (finished(&(*it)->sound)) {
            ma_sound_uninit(&(*it)->sound);
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
        ma_sound_uninit(&(*oldest)->sound);
        m.voices.erase(oldest);
    }

    auto v = std::make_unique<Voice>();
    ma_uint32 flags = MA_SOUND_FLAG_DECODE;
    if (!p.spatial)
        flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    const ma_result r = ma_sound_init_from_file(&m.engine, path.c_str(), flags, &m.sfxGroup, nullptr, &v->sound);
    if (r != MA_SUCCESS) {
        m.warnOnce(path, r);
        return {};
    }

    ma_sound* s = &v->sound;
    ma_sound_set_volume(s, std::max(p.volume, 0.0f));
    ma_sound_set_pitch(s, std::max(p.pitch, 0.01f));
    ma_sound_set_looping(s, p.loop ? MA_TRUE : MA_FALSE);
    if (p.spatial) {
        ma_sound_set_position(s, p.position.x, p.position.y, p.position.z);
        ma_sound_set_min_distance(s, p.minDistance);
        ma_sound_set_max_distance(s, p.maxDistance);
        ma_sound_set_rolloff(s, p.rolloff);
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
    if (Voice* v = impl_->find(h))
        ma_sound_set_position(&v->sound, position.x, position.y, position.z);
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

void AudioEngine::setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up)
{
    Impl& m = *impl_;
    if (!m.engineOk)
        return;
    ma_engine_listener_set_position(&m.engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m.engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m.engine, 0, up.x, up.y, up.z);
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
