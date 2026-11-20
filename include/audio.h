// audio.h - Engine (entrada -> pedalboard -> saída) e gerenciamento dos dispositivos.
#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "pedals.h"

namespace gp {

    class Engine {
    public:
        Pedalboard board;  // contém pedais + amp no último slot
        std::atomic<float> inGain{ 1.0f }, master{ 0.4f };
        std::atomic<float> inPeak{ 0 }, outPeak{ 0 };
        // Carga de DSP medida no callback (0..100%). Só o thread de áudio escreve.
        std::atomic<float> cpuLoad{ 0.0f };

        void prepare(float sr);
        void process(float* out, const float* in, int frames);

    private:
        Biquad inputDC;
        float inCur = 1.0f, masterCur = 0.4f;
        float rate = 48000.0f;
    };

    class AudioIO {
    public:
        AudioIO();
        ~AudioIO();
        void refreshDevices();
        const std::vector<std::string>& inputs() const;
        const std::vector<std::string>& outputs() const;
        // bufferFrames: 128 / 256 / 512. Menor = menos latência, mais CPU.
        bool start(int in, int out, bool exclusive, int bufferFrames, std::string& err);
        void stop();
        bool running() const;
        unsigned sampleRate() const;
        int bufferSize() const;
        Engine& engine() { return engine_; }

    private:
        Engine engine_;
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

}  // namespace gp
