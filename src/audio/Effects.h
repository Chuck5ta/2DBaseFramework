#pragma once

// Internal to the audio module: the miniaudio nodes that apply effects. Games
// use the AudioEngine API in Audio.h instead.
//
//   voice ─► VoiceNode ─┬─ out 0 (dry, filtered) ──► sfx group ─► BusNode(Sfx) ─┐
//                       └─ out 1 (× reverb send) ─► ReverbNode ─► sfx group      ├─► BusNode(Master) ─► device
//   music ────────────────────────────────────────► music group ─► BusNode(Music)┘
//
// Parameters are written by the game thread and read by the audio thread, and
// glide to new values (SmoothParam) so changes never click.

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <miniaudio.h>
#include <extras/nodes/ma_reverb_node/verblib.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace audio::detail {

// A value set on the game thread that the audio thread moves towards smoothly.
struct SmoothParam {
    std::atomic<float> target{0.0f};
    std::atomic<float> glide{0.0f}; // seconds to (almost) reach the target
    float current = 0.0f;           // audio thread only

    // Only before the node is attached (the audio thread can't see it yet).
    void reset(float value);
    void set(float value, float glideSeconds);
    // Audio thread: advance by dt seconds and return the new current value.
    float advance(float dt);
    bool idleAt(float value) const; // reached `value` and staying there
};

// Filters are smoothed in log2(Hz), so a glide sounds even across octaves.
float hzToLog2(float hz);

// Low-pass, high-pass and echo on one bus (stereo in, stereo out).
struct BusNode {
    ma_node_base base; // must be first: miniaudio treats this struct as an ma_node
    uint32_t sampleRate = 48000;
    ma_lpf lpf;
    ma_hpf hpf;
    float lpfApplied = -1.0f, hpfApplied = -1.0f; // cutoff the filter was last set to (log2 Hz)
    bool lpfActive = false, hpfActive = false;
    SmoothParam lowPass, highPass;                // log2 Hz
    SmoothParam echoDelay, echoFeedback, echoMix; // seconds, 0..0.95, 0..1
    float* echoBuffer = nullptr;                  // stereo ring buffer
    uint32_t echoFrames = 0;
    uint32_t echoWrite = 0;
    bool echoStale = false; // holds an old tail: clear before the echo is used again
    float echoMixApplied = 0.0f;
};

// One playing sound's own low-pass and its send to the reverb.
// Output bus 0 = the (filtered) sound; output bus 1 = the same × reverb send.
struct VoiceNode {
    ma_node_base base;
    uint32_t sampleRate = 48000;
    ma_lpf lpf;
    float lpfApplied = -1.0f;
    bool lpfActive = false;
    SmoothParam lowPass; // log2 Hz
    SmoothParam send;    // 0..1
    float sendApplied = 0.0f;
};

// The shared reverb (verblib, a Freeverb implementation that ships with miniaudio).
struct ReverbNode {
    ma_node_base base;
    uint32_t sampleRate = 48000;
    verblib verb;
    SmoothParam level, roomSize, damping, width;
    float levelApplied = 0.0f;
    float roomApplied = -1.0f, dampingApplied = -1.0f, widthApplied = -1.0f;
    bool silent = true; // tail cleared; nothing to process until the level rises
};

ma_result initBusNode(ma_node_graph* graph, uint32_t sampleRate, BusNode* node);
void uninitBusNode(BusNode* node);
ma_result initVoiceNode(ma_node_graph* graph, uint32_t sampleRate, float lowPassHz, float send, VoiceNode* node);
void uninitVoiceNode(VoiceNode* node);
ma_result initReverbNode(ma_node_graph* graph, uint32_t sampleRate, ReverbNode* node);
void uninitReverbNode(ReverbNode* node);

} // namespace audio::detail
