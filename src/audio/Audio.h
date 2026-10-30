#pragma once

// Sound effects, music and 3D positional audio, built on miniaudio.
//
//   app.audio().play(core::assetPath("sounds/coin.wav"));
//   app.audio().play(path, {.volume = 0.5f, .pitch = 1.2f});
//   SoundHandle h = app.audio().play(path, {.loop = true, .spatial = true, .position = p});
//   app.audio().setPosition(h, newPosition);
//   app.audio().playMusic(core::assetPath("music/theme.ogg"), {.fadeInSeconds = 2.0f});
//
// Supported formats: WAV, FLAC, MP3 and OGG Vorbis.
// All functions are meant to be called from the main (game) thread. If no audio
// device is available the engine runs silently: every call is a harmless no-op.

#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace audio {

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
