#include "audio/Effects.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define VERBLIB_IMPLEMENTATION
#include <extras/nodes/ma_reverb_node/verblib.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace audio::detail {

namespace {

constexpr ma_uint32 kChannels = 2;        // the engine mixes in stereo
constexpr ma_uint32 kBlock = 64;          // parameters are updated every 64 frames
constexpr float kMaxEchoSeconds = 2.0f;
constexpr float kLowPassBypass = 14.25f;  // log2(~19.5 kHz): above this the low-pass is skipped
constexpr float kHighPassBypass = 4.33f;  // log2(~20 Hz): below this the high-pass is skipped
constexpr ma_uint32 kFilterOrder = 4;     // 24 dB per octave: a clear "muffle"

// miniaudio hands our nodes back as ma_node*, which we cast to the full struct.
// That works because `base` is the first member and the structs have no virtual
// functions (so nothing is placed before `base`).
static_assert(!std::is_polymorphic_v<BusNode> && !std::is_polymorphic_v<VoiceNode> &&
              !std::is_polymorphic_v<ReverbNode>);

// Highest cutoff a filter accepts at this sample rate (below Nyquist).
float maxCutoff(uint32_t sampleRate)
{
    return float(sampleRate) * 0.45f;
}

// (Re)tunes a low-pass to `cutoffLog2` when it moved enough to matter.
void tuneLowPass(ma_lpf& lpf, float& applied, float cutoffLog2, uint32_t sampleRate, bool fresh)
{
    if (!fresh && std::fabs(cutoffLog2 - applied) < 0.005f)
        return;
    const double hz = std::min(std::exp2(double(cutoffLog2)), double(maxCutoff(sampleRate)));
    const ma_lpf_config config = ma_lpf_config_init(ma_format_f32, kChannels, sampleRate, hz, kFilterOrder);
    ma_lpf_reinit(&config, &lpf);
    if (fresh)
        ma_lpf_clear_cache(&lpf); // forget old samples from when it was last used
    applied = cutoffLog2;
}

// (The high-pass's leftover state, unlike the low-pass's, dies away within a few
// milliseconds, so it needs no clearing.)
void tuneHighPass(ma_hpf& hpf, float& applied, float cutoffLog2, uint32_t sampleRate, bool fresh)
{
    if (!fresh && std::fabs(cutoffLog2 - applied) < 0.005f)
        return;
    const double hz = std::min(std::exp2(double(cutoffLog2)), double(maxCutoff(sampleRate)));
    const ma_hpf_config config = ma_hpf_config_init(ma_format_f32, kChannels, sampleRate, hz, 2);
    ma_hpf_reinit(&config, &hpf);
    applied = cutoffLog2;
}

// Multiplies stereo frames by a gain ramping linearly from `from` to `to`.
void applyGainRamp(float* frames, ma_uint32 count, float from, float to)
{
    const float step = (to - from) / float(count);
    for (ma_uint32 i = 0; i < count; ++i) {
        const float g = from + step * float(i + 1);
        frames[i * 2] *= g;
        frames[i * 2 + 1] *= g;
    }
}

// --- Bus node -------------------------------------------------------------------

void processBus(ma_node* node, const float** in, ma_uint32*, float** out, ma_uint32* frameCountOut)
{
    BusNode& b = *reinterpret_cast<BusNode*>(node);
    const ma_uint32 total = *frameCountOut;
    for (ma_uint32 done = 0; done < total;) {
        const ma_uint32 n = std::min(kBlock, total - done);
        const float dt = float(n) / float(b.sampleRate);
        const float* src = in[0] + done * kChannels;
        float* dst = out[0] + done * kChannels;
        std::memcpy(dst, src, n * kChannels * sizeof(float));

        // High-pass, then low-pass.
        const float hp = b.highPass.advance(dt);
        if (hp > kHighPassBypass) {
            tuneHighPass(b.hpf, b.hpfApplied, hp, b.sampleRate, !b.hpfActive);
            b.hpfActive = true;
            ma_hpf_process_pcm_frames(&b.hpf, dst, dst, n);
        } else {
            b.hpfActive = false;
        }
        const float lp = b.lowPass.advance(dt);
        if (lp < kLowPassBypass) {
            tuneLowPass(b.lpf, b.lpfApplied, lp, b.sampleRate, !b.lpfActive);
            b.lpfActive = true;
            ma_lpf_process_pcm_frames(&b.lpf, dst, dst, n);
        } else {
            b.lpfActive = false;
        }

        // Echo: a delay line whose output is fed back into itself.
        const float mix = b.echoMix.advance(dt);
        const float feedback = std::clamp(b.echoFeedback.advance(dt), 0.0f, 0.95f);
        const float delay = b.echoDelay.advance(dt);
        if (b.echoBuffer && (mix > 1e-5f || b.echoMixApplied > 1e-5f)) {
            if (b.echoStale) {
                std::memset(b.echoBuffer, 0, size_t(b.echoFrames) * kChannels * sizeof(float));
                b.echoStale = false;
            }
            const float delayFrames = std::clamp(delay * float(b.sampleRate), 1.0f, float(b.echoFrames - 2));
            const float mixStep = (mix - b.echoMixApplied) / float(n);
            for (ma_uint32 i = 0; i < n; ++i) {
                // Read `delayFrames` behind the write position, between two samples.
                float readPos = float(b.echoWrite) - delayFrames;
                if (readPos < 0.0f)
                    readPos += float(b.echoFrames);
                const uint32_t i0 = uint32_t(readPos) % b.echoFrames;
                const uint32_t i1 = (i0 + 1) % b.echoFrames;
                const float frac = readPos - std::floor(readPos);
                const float g = b.echoMixApplied + mixStep * float(i + 1);
                for (ma_uint32 c = 0; c < kChannels; ++c) {
                    const float delayed = b.echoBuffer[i0 * kChannels + c] * (1.0f - frac) +
                                          b.echoBuffer[i1 * kChannels + c] * frac;
                    const float x = dst[i * kChannels + c];
                    b.echoBuffer[b.echoWrite * kChannels + c] = x + delayed * feedback;
                    dst[i * kChannels + c] = x + delayed * g;
                }
                b.echoWrite = (b.echoWrite + 1) % b.echoFrames;
            }
            b.echoMixApplied = mix;
        } else {
            b.echoMixApplied = 0.0f;
            b.echoStale = true; // whatever is in the buffer is out of date now
        }
        done += n;
    }
}

ma_node_vtable g_busVtable = {
    processBus, nullptr, 1, 1,
    MA_NODE_FLAG_CONTINUOUS_PROCESSING, // keep running so echo tails finish after the sounds stop
};

// --- Voice node -----------------------------------------------------------------

void processVoice(ma_node* node, const float** in, ma_uint32*, float** out, ma_uint32* frameCountOut)
{
    VoiceNode& v = *reinterpret_cast<VoiceNode*>(node);
    const ma_uint32 total = *frameCountOut;
    for (ma_uint32 done = 0; done < total;) {
        const ma_uint32 n = std::min(kBlock, total - done);
        const float dt = float(n) / float(v.sampleRate);
        float* dry = out[0] + done * kChannels;
        float* send = out[1] + done * kChannels;
        std::memcpy(dry, in[0] + done * kChannels, n * kChannels * sizeof(float));

        const float lp = v.lowPass.advance(dt);
        if (lp < kLowPassBypass) {
            tuneLowPass(v.lpf, v.lpfApplied, lp, v.sampleRate, !v.lpfActive);
            v.lpfActive = true;
            ma_lpf_process_pcm_frames(&v.lpf, dry, dry, n);
        } else {
            v.lpfActive = false;
        }

        const float amount = std::max(v.send.advance(dt), 0.0f);
        std::memcpy(send, dry, n * kChannels * sizeof(float));
        applyGainRamp(send, n, v.sendApplied, amount);
        v.sendApplied = amount;
        done += n;
    }
}

ma_node_vtable g_voiceVtable = {processVoice, nullptr, 1, 2, 0};

// --- Reverb node ----------------------------------------------------------------

void processReverb(ma_node* node, const float** in, ma_uint32*, float** out, ma_uint32* frameCountOut)
{
    ReverbNode& r = *reinterpret_cast<ReverbNode*>(node);
    const ma_uint32 total = *frameCountOut;
    for (ma_uint32 done = 0; done < total;) {
        const ma_uint32 n = std::min(kBlock, total - done);
        const float dt = float(n) / float(r.sampleRate);
        float* dst = out[0] + done * kChannels;

        const float level = std::max(r.level.advance(dt), 0.0f);
        const float room = r.roomSize.advance(dt);
        const float damping = r.damping.advance(dt);
        const float width = r.width.advance(dt);
        if (r.level.idleAt(0.0f) && r.levelApplied <= 0.0f) {
            // Off: output silence and, once, forget the old tail.
            std::memset(dst, 0, n * kChannels * sizeof(float));
            if (!r.silent) {
                verblib_mute(&r.verb);
                r.silent = true;
            }
        } else {
            if (room != r.roomApplied) {
                verblib_set_room_size(&r.verb, std::clamp(room, 0.0f, 1.0f));
                r.roomApplied = room;
            }
            if (damping != r.dampingApplied) {
                verblib_set_damping(&r.verb, std::clamp(damping, 0.0f, 1.0f));
                r.dampingApplied = damping;
            }
            if (width != r.widthApplied) {
                verblib_set_width(&r.verb, std::clamp(width, 0.0f, 1.0f));
                r.widthApplied = width;
            }
            verblib_process(&r.verb, in[0] + done * kChannels, dst, n);
            applyGainRamp(dst, n, r.levelApplied, level);
            r.silent = false;
        }
        r.levelApplied = level;
        done += n;
    }
}

ma_node_vtable g_reverbVtable = {
    processReverb, nullptr, 1, 1,
    MA_NODE_FLAG_CONTINUOUS_PROCESSING, // the tail keeps ringing after the sounds stop
};

ma_result initNode(ma_node_graph* graph, ma_node_vtable* vtable, ma_node_base* base)
{
    static const ma_uint32 channels[2] = {kChannels, kChannels};
    ma_node_config config = ma_node_config_init();
    config.vtable = vtable;
    config.pInputChannels = channels;
    config.pOutputChannels = channels;
    return ma_node_init(graph, &config, nullptr, base);
}

} // namespace

// --- SmoothParam ----------------------------------------------------------------

void SmoothParam::reset(float value)
{
    target.store(value);
    glide.store(0.0f);
    current = value;
}

void SmoothParam::set(float value, float glideSeconds)
{
    glide.store(std::max(glideSeconds, 0.0f), std::memory_order_relaxed);
    target.store(value, std::memory_order_release);
}

float SmoothParam::advance(float dt)
{
    const float t = target.load(std::memory_order_acquire);
    const float g = glide.load(std::memory_order_relaxed);
    if (g <= 0.0f) {
        current = t;
    } else {
        // Exponential approach: ~99% of the way after `glide` seconds.
        current += (t - current) * (1.0f - std::exp(-dt * 5.0f / g));
        if (std::fabs(t - current) <= 1e-4f * std::max(1.0f, std::fabs(t)))
            current = t;
    }
    return current;
}

bool SmoothParam::idleAt(float value) const
{
    return current == value && target.load(std::memory_order_relaxed) == value;
}

float hzToLog2(float hz)
{
    return std::log2(std::clamp(hz, 10.0f, 24000.0f));
}

// --- Init / uninit ----------------------------------------------------------------

ma_result initBusNode(ma_node_graph* graph, uint32_t sampleRate, BusNode* node)
{
    node->sampleRate = sampleRate;
    const ma_lpf_config lpfConfig = ma_lpf_config_init(ma_format_f32, kChannels, sampleRate, 1000.0, kFilterOrder);
    const ma_hpf_config hpfConfig = ma_hpf_config_init(ma_format_f32, kChannels, sampleRate, 100.0, 2);
    if (ma_lpf_init(&lpfConfig, nullptr, &node->lpf) != MA_SUCCESS)
        return MA_ERROR;
    if (ma_hpf_init(&hpfConfig, nullptr, &node->hpf) != MA_SUCCESS)
        return MA_ERROR;
    node->lowPass.reset(hzToLog2(20000.0f));
    node->highPass.reset(hzToLog2(10.0f));
    node->echoDelay.reset(0.35f);
    node->echoFeedback.reset(0.35f);
    node->echoMix.reset(0.0f);
    node->echoFrames = uint32_t(kMaxEchoSeconds * float(sampleRate)) + 4;
    node->echoBuffer = new float[size_t(node->echoFrames) * kChannels]();
    return initNode(graph, &g_busVtable, &node->base);
}

void uninitBusNode(BusNode* node)
{
    ma_node_uninit(&node->base, nullptr);
    ma_lpf_uninit(&node->lpf, nullptr);
    ma_hpf_uninit(&node->hpf, nullptr);
    delete[] node->echoBuffer;
    node->echoBuffer = nullptr;
}

ma_result initVoiceNode(ma_node_graph* graph, uint32_t sampleRate, float lowPassHz, float send, VoiceNode* node)
{
    node->sampleRate = sampleRate;
    const ma_lpf_config config = ma_lpf_config_init(ma_format_f32, kChannels, sampleRate, 1000.0, kFilterOrder);
    if (ma_lpf_init(&config, nullptr, &node->lpf) != MA_SUCCESS)
        return MA_ERROR;
    node->lowPass.reset(hzToLog2(lowPassHz));
    node->send.reset(std::max(send, 0.0f));
    node->sendApplied = std::max(send, 0.0f);
    return initNode(graph, &g_voiceVtable, &node->base);
}

void uninitVoiceNode(VoiceNode* node)
{
    ma_node_uninit(&node->base, nullptr);
    ma_lpf_uninit(&node->lpf, nullptr);
}

ma_result initReverbNode(ma_node_graph* graph, uint32_t sampleRate, ReverbNode* node)
{
    node->sampleRate = sampleRate;
    if (verblib_initialize(&node->verb, sampleRate, kChannels) == 0)
        return MA_INVALID_ARGS; // sample rate outside 22.05-176.4 kHz
    verblib_set_wet(&node->verb, 1.0f / verblib_scalewet); // full wet; our own level scales it
    verblib_set_dry(&node->verb, 0.0f);                    // the dry sound takes the other path
    node->level.reset(0.0f);
    node->roomSize.reset(0.5f);
    node->damping.reset(0.5f);
    node->width.reset(1.0f);
    return initNode(graph, &g_reverbVtable, &node->base);
}

void uninitReverbNode(ReverbNode* node)
{
    ma_node_uninit(&node->base, nullptr);
}

} // namespace audio::detail
