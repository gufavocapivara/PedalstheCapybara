// audio.cpp - Engine e AudioIO (miniaudio).
//
// [OPT] process() com loops separados (entrada, board, saída) para melhor vetorização.
// [OPT] softLimit inline e branchless no caso comum.
// [FIX] Mede a carga real de DSP para mostrar na interface.
// [FIX] Tamanho de buffer configurável (128/256/512).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#include "miniaudio.h"

#include "audio.h"
#include <algorithm>
#include <chrono>
#include <cmath>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#define GP_FTZ() _mm_setcsr(_mm_getcsr() | 0x8040)
#else
#define GP_FTZ() ((void)0)
#endif

namespace gp {

    void Engine::prepare(float sr) {
        rate = sr > 0.0f ? sr : 48000.0f;
        board.prepare(rate);
        board.prepareAmp(rate);
        inputDC.highpass(rate, 40.0f);
        cpuLoad.store(0.0f);
    }

    static inline float softLimit(float x) {
        // Limita só o que passaria de 0.85; abaixo disso é transparente.
        float a = std::fabs(x);
        if (a <= 0.85f) return x;
        float y = 0.85f + 0.15f * std::tanh((a - 0.85f) / 0.15f);
        return x < 0 ? -y : y;
    }

    void Engine::process(float* out, const float* in, int frames) {
        GP_FTZ();
        const auto t0 = std::chrono::steady_clock::now();
        float mono[kMaxBlock];
        float ip = 0, op = 0;

        for (int off = 0; off < frames; off += kMaxBlock) {
            int n = std::min(kMaxBlock, frames - off);
            const float ig = inGain.load(std::memory_order_relaxed);

            // 1) entrada + DC + ganho suavizado
            for (int i = 0; i < n; ++i) {
                inCur += (ig - inCur) * 0.002f;
                float s = inputDC.process(in ? in[off + i] : 0.0f) * inCur;
                float a = std::fabs(s);
                if (a > ip) ip = a;
                mono[i] = s;
            }

            // 2) pedalboard + amp (o amp é o último pedal da cadeia)
            board.process(mono, n);

            // 3) master + limitador + saída estéreo
            const float mg = master.load(std::memory_order_relaxed);
            for (int i = 0; i < n; ++i) {
                masterCur += (mg - masterCur) * 0.002f;
                float y = softLimit(mono[i] * masterCur);
                float a = std::fabs(y);
                if (a > op) op = a;
                out[2 * (off + i)] = y;
                out[2 * (off + i) + 1] = y;
            }
        }
        inPeak.store(ip, std::memory_order_relaxed);
        outPeak.store(op, std::memory_order_relaxed);

        if (frames > 0) {
            const double dt = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
            const float load = (float)(dt * (double)rate / (double)frames * 100.0);
            cpuLoad.store(cpuLoad.load(std::memory_order_relaxed) * 0.90f + load * 0.10f,
                          std::memory_order_relaxed);
        }
    }

    // ----------------------------------------------------------------- AudioIO
    struct AudioIO::Impl {
        ma_context ctx;
        bool ctxOk = false;
        ma_device dev;
        bool devOk = false;
        int buffer = 256;
        std::vector<ma_device_info> capInfos, pbInfos;
        std::vector<std::string> inNames, outNames;
    };

    static void dataCallback(ma_device* dev, void* out, const void* in, ma_uint32 frames) {
        static_cast<Engine*>(dev->pUserData)->process(static_cast<float*>(out), static_cast<const float*>(in), (int)frames);
    }

    AudioIO::AudioIO() : impl_(new Impl) {
        impl_->ctxOk = ma_context_init(nullptr, 0, nullptr, &impl_->ctx) == MA_SUCCESS;
        refreshDevices();
    }

    AudioIO::~AudioIO() {
        stop();
        if (impl_->ctxOk) ma_context_uninit(&impl_->ctx);
    }

    void AudioIO::refreshDevices() {
        impl_->capInfos.clear(); impl_->pbInfos.clear();
        impl_->inNames.clear(); impl_->outNames.clear();
        if (!impl_->ctxOk) return;
        ma_device_info* pb = nullptr, * cap = nullptr;
        ma_uint32 pbCount = 0, capCount = 0;
        if (ma_context_get_devices(&impl_->ctx, &pb, &pbCount, &cap, &capCount) != MA_SUCCESS) return;
        for (ma_uint32 i = 0; i < capCount; ++i) {
            impl_->capInfos.push_back(cap[i]);
            impl_->inNames.push_back(cap[i].name);
        }
        for (ma_uint32 i = 0; i < pbCount; ++i) {
            impl_->pbInfos.push_back(pb[i]);
            impl_->outNames.push_back(pb[i].name);
        }
    }

    const std::vector<std::string>& AudioIO::inputs() const { return impl_->inNames; }
    const std::vector<std::string>& AudioIO::outputs() const { return impl_->outNames; }
    bool AudioIO::running() const { return impl_->devOk; }
    unsigned AudioIO::sampleRate() const { return impl_->devOk ? impl_->dev.sampleRate : 0; }
    int AudioIO::bufferSize() const { return impl_->buffer; }

    void AudioIO::stop() {
        if (impl_->devOk) { ma_device_uninit(&impl_->dev); impl_->devOk = false; }
    }

    bool AudioIO::start(int in, int out, bool exclusive, int bufferFrames, std::string& err) {
        stop();
        if (!impl_->ctxOk) { err = "Failed to initialize the audio system."; return false; }

        impl_->buffer = std::clamp(bufferFrames, 64, 2048);
        ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
        cfg.capture.pDeviceID = (in >= 0 && in < (int)impl_->capInfos.size()) ? &impl_->capInfos[in].id : nullptr;
        cfg.capture.format = ma_format_f32;
        cfg.capture.channels = 1;
        cfg.playback.pDeviceID = (out >= 0 && out < (int)impl_->pbInfos.size()) ? &impl_->pbInfos[out].id : nullptr;
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.sampleRate = 48000;
        // 256 frames = 5,3 ms por callback. 128 reduz a latência mas exige
        // uma máquina/interface que aguente o amp Lua com oversampling.
        cfg.periodSizeInFrames = (ma_uint32)impl_->buffer;
        cfg.periods = 3;
        cfg.performanceProfile = ma_performance_profile_low_latency;
        cfg.noClip = MA_TRUE;
        cfg.dataCallback = dataCallback;
        cfg.pUserData = &engine_;
        if (exclusive) cfg.capture.shareMode = cfg.playback.shareMode = ma_share_mode_exclusive;

        if (ma_device_init(&impl_->ctx, &cfg, &impl_->dev) != MA_SUCCESS) {
            err = "Could not open the audio devices. Try another device or turn off exclusive mode.";
            return false;
        }
        impl_->devOk = true;
        engine_.prepare((float)impl_->dev.sampleRate);
        if (ma_device_start(&impl_->dev) != MA_SUCCESS) {
            err = "Failed to start audio.";
            stop();
            return false;
        }
        return true;
    }

}  // namespace gp
