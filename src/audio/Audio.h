#pragma once

// Sound effects, music and 3D positional audio, built on miniaudio.
//
//   app.audio().play(core::assetPath("sounds/coin.wav"));
//   app.audio().play(path, {.volume = 0.5f, .pitch = 1.2f});
//   SoundHandle h = app.audio().play(path, {.loop = true, .spatial = true, .position = p});
//   app.audio().setPosition(h, newPosition);
//   app.audio().playMusic(core::assetPath("music/theme.ogg"), {.fadeInSeconds = 2.0f});
//
// Effects (every change glides smoothly, so nothing clicks):
//   app.audio().setLowPass(audio::Bus::Sfx, 600.0f);             // muffle effects (underwater)
//   app.audio().setReverb(audio::ReverbParams::preset(audio::ReverbPreset::Cave));
//   app.audio().setEcho(audio::Bus::Sfx, {.delaySeconds = 0.4f, .feedback = 0.4f, .mix = 0.5f});
//   app.audio().setLowPass(handle, 800.0f);                      // one sound behind a wall
//
// Supported formats: WAV, FLAC, MP3 and OGG Vorbis.
// All functions are meant to be called from the main (game) thread. If no audio
// device is available the engine runs silently: every call is a harmless no-op.

#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace audio {

// Filter cutoffs in Hz. A low-pass at kLowPassOff or a high-pass at kHighPassOff
// lets everything through (and costs nothing).
inline constexpr float kLowPassOff = 20000.0f;
inline constexpr float kHighPassOff = 0.0f;

// Where an effect is applied. Sound effects and music each have a bus, and both
// go through the master bus (so a master effect changes everything).
enum class Bus { Sfx, Music, Master };

// Distinct repeats of the sound (a canyon, a long corridor).
struct EchoParams {
    float delaySeconds = 0.35f; // time between repeats (up to 2 s)
    float feedback = 0.35f;     // 0..0.95: how much of each repeat comes back again
    float mix = 0.0f;           // loudness of the repeats; 0 = off
};

enum class ReverbPreset { Off, Room, Hall, Cave };

// The shared reverb: a room's reflections, blending into a tail. Every sound
// effect sends some of itself to it (PlayParams::reverbSend, default all of it);
// music never does. Its output joins the sound effects, so the effects volume and
// Bus::Sfx filters apply to it too.
struct ReverbParams {
    float level = 0.0f;    // loudness of the reverb; 0 = off
    float roomSize = 0.5f; // 0..1: longer tail as it grows
    float damping = 0.5f;  // 0..1: higher = the tail loses its high frequencies faster (darker)
    float width = 1.0f;    // 0 = mono, 1 = wide stereo

    static ReverbParams preset(ReverbPreset preset);
};

struct AudioDesc {
    bool enabled = true;          // false: never open a device (also: env var GAME_AUDIO=0)
    float masterVolume = 1.0f;    // 0..1 (values above 1 amplify)
    float sfxVolume = 1.0f;
    float musicVolume = 0.8f;
    uint32_t maxVoices = 64;      // simultaneous sound effects; the oldest one-shot is cut when full
    // Advanced: run without an output device and pull mixed audio with
    // readFrames() instead (automated tests, recording to a file).
    bool noDevice = false;
    uint32_t noDeviceSampleRate = 48000;
};

// Identifies a playing sound. Stays valid until the sound finishes or is
// stopped; after that, functions taking the handle simply do nothing.
struct SoundHandle {
    uint32_t id = 0;
    explicit operator bool() const { return id != 0; }
};

struct PlayParams {
    float volume = 1.0f;       // linear gain; 1 = as recorded
    float pitch = 1.0f;        // playback rate: 2 = one octave higher (and twice as fast)
    float pan = 0.0f;          // -1 = left, 0 = centre, +1 = right (non-spatial sounds)
    bool loop = false;         // looping sounds play until stop()
    // 3D: attenuate and pan relative to the listener (see setListener).
    bool spatial = false;
    glm::vec3 position{0.0f};  // world position (spatial sounds)
    float minDistance = 1.0f;  // full volume up to this distance
    float maxDistance = 100.0f;// no further quietening beyond this distance
    float rolloff = 1.0f;      // how quickly volume falls with distance (inverse model)
    // Effects for this sound only (both can change later, see setLowPass/setReverbSend).
    float lowPassHz = kLowPassOff; // e.g. 800 to muffle a sound behind a wall
    float reverbSend = 1.0f;       // share sent to the reverb; 0 keeps it dry (UI sounds)
};

struct MusicParams {
    float volume = 1.0f;
    bool loop = true;
    float fadeInSeconds = 0.0f;
    float fadeOutPreviousSeconds = 1.0f; // if music is already playing, it fades out over this time
};

class AudioEngine {
public:
    explicit AudioEngine(const AudioDesc& desc = {});
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // false when audio is disabled or no device could be opened.
    bool available() const;

    // Called by Application once per frame: releases finished sounds.
    void update();

    // --- Sound effects ---------------------------------------------------------
    // Files are decoded on first use and cached in memory, so later plays start
    // instantly. preload() does the decoding up front (e.g. in onInit) to avoid
    // a small hitch the first time a sound plays.
    bool preload(const std::string& path);
    void unload(const std::string& path); // drop a cached sound (playing copies keep going)

    SoundHandle play(const std::string& path, const PlayParams& params = {});
    void stop(SoundHandle sound, float fadeOutSeconds = 0.0f);
    void stopAllSounds();
    bool isPlaying(SoundHandle sound) const;
    void setVolume(SoundHandle sound, float volume);
    void setPitch(SoundHandle sound, float pitch);
    void setPan(SoundHandle sound, float pan);
    void setPosition(SoundHandle sound, const glm::vec3& position);
    void setLowPass(SoundHandle sound, float cutoffHz, float glideSeconds = 0.1f);
    void setReverbSend(SoundHandle sound, float amount, float glideSeconds = 0.1f);

    // --- Music (streamed from disk, one track at a time) -----------------------
    bool playMusic(const std::string& path, const MusicParams& params = {});
    void stopMusic(float fadeOutSeconds = 1.0f);
    void pauseMusic();
    void resumeMusic();
    bool isMusicPlaying() const;

    // --- Mixer -----------------------------------------------------------------
    void setMasterVolume(float volume);
    void setSfxVolume(float volume);
    void setMusicVolume(float volume);
    float masterVolume() const;
    float sfxVolume() const;
    float musicVolume() const;
    void setMuted(bool muted);   // silences everything without losing the volume settings
    bool muted() const;

    // --- Effects -------------------------------------------------------------------
    // Each change glides to the new value over glideSeconds (0 = instantly).
    void setLowPass(Bus bus, float cutoffHz, float glideSeconds = 0.3f);   // muffle: kLowPassOff = off
    void setHighPass(Bus bus, float cutoffHz, float glideSeconds = 0.3f);  // thin (radio): kHighPassOff = off
    void setEcho(Bus bus, const EchoParams& echo, float glideSeconds = 0.3f);
    void setReverb(const ReverbParams& reverb, float glideSeconds = 0.5f);
    void resetEffects(float glideSeconds = 0.3f); // every bus effect and the reverb off
    float lowPass(Bus bus) const;
    float highPass(Bus bus) const;
    EchoParams echo(Bus bus) const;
    ReverbParams reverb() const;

    // --- 3D listener (usually the camera) ---------------------------------------
    // Right-handed, +Y up by default, like Camera3D.
    void setListener(const glm::vec3& position, const glm::vec3& forward,
                     const glm::vec3& up = {0.0f, 1.0f, 0.0f});

    // --- Advanced ----------------------------------------------------------------
    uint32_t sampleRate() const;
    uint32_t activeSounds() const;
    // noDevice mode only: mix `frameCount` stereo frames (interleaved floats) into
    // `out`. Returns the number of frames written.
    uint64_t readFrames(float* out, uint64_t frameCount);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace audio
