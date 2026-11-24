// amps.h - Amplificadores nativos do app e gerenciamento de Amp.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>
#include "pedals.h"

namespace gp {

    class LuaPedal;

    // Marshall Plexi 1959 - modelo compacto de referência.
    class Marshall : public Pedal {
        Param *modo, *gain, *bass, *mid, *treble, *master;
        Biquad preHP, dcOut, eqBass, eqMid, eqTreble, spkPeak, postLP;
        Oversampler4x os;
        float modeMix = 0.0f;

    public:
        Marshall() : Pedal("Marshall Plexi 1959") {
            modo = add("modo", 0.0f, 1.0f, 0.0f);
            modo->labels = { "CLEAN", "DRIVE" };
            modo->discrete = true;
            gain = add("gain", 0.0f, 1.0f, 0.5f);
            bass = add("bass", 0.0f, 1.0f, 0.5f);
            mid = add("mid", 0.0f, 1.0f, 0.5f);
            treble = add("treble", 0.0f, 1.0f, 0.5f);
            master = add("master", 0.0f, 1.0f, 0.6f);
            category = "Amps";
            isAmp = true;
            color[0] = 0.15f; color[1] = 0.15f; color[2] = 0.15f;
            hasColor = true;
        }

        void prepare(float sr) override {
            Pedal::prepare(sr);
            os.init(sampleRate);
            preHP.highpass(sampleRate, 55.0f);
            dcOut.highpass(sampleRate, 22.0f);
            spkPeak.peaking(sampleRate, 110.0f, 1.4f, 2.0f);
            postLP.lowpass(sampleRate, 6000.0f);
            modeMix = modo->get() >= 0.5f ? 1.0f : 0.0f;
            reset();
        }

        void reset() override {
            preHP.reset(); dcOut.reset();
            eqBass.reset(); eqMid.reset(); eqTreble.reset();
            spkPeak.reset(); postLP.reset(); os.reset();
        }

        void process(float* x, int n) override {
            const float b = bass->nextBlock(n, sampleRate);
            const float m = mid->nextBlock(n, sampleRate);
            const float t = treble->nextBlock(n, sampleRate);
            const float gainV = gain->nextBlock(n, sampleRate);
            const float vol = master->nextBlock(n, sampleRate) * 1.3f;
            eqBass.shelf(sampleRate, 110.0f, (b - 0.5f) * 24.0f, false);
            eqMid.peaking(sampleRate, 700.0f, 0.75f, (m - 0.5f) * 24.0f);
            eqTreble.shelf(sampleRate, 3200.0f, (t - 0.5f) * 24.0f, true);

            const float cleanG = 0.8f + gainV * 2.2f;
            const float driveG = 4.0f + gainV * 20.0f;
            const float cleanMk = 1.8f / cleanG;
            const float driveMk = 1.0f / (0.8f + 0.9f * std::sqrt(driveG));
            const float target = modo->get() >= 0.5f ? 1.0f : 0.0f;
            const float mixStep = 1.0f - std::exp(-1.0f / (0.015f * sampleRate));
            for (int i = 0; i < n; ++i) {
                modeMix += (target - modeMix) * mixStep;
                const float g = cleanG + (driveG - cleanG) * modeMix;
                const float mk = cleanMk + (driveMk - cleanMk) * modeMix;
                float s = preHP.process(x[i]) * g;
                s = os.process(s, [](float v) {
                    return v >= 0.0f ? std::tanh(v) : std::tanh(v * 0.88f) * 0.94f;
                });
                s = dcOut.process(s * mk);
                s = eqBass.process(s);
                s = eqMid.process(s);
                s = eqTreble.process(s);
                s = spkPeak.process(s);
                s = postLP.process(s);
                x[i] = s * vol;
            }
        }
    };

    // JCM800 2203 gray-box: cascaded asymmetric stages, presence, sag and speaker filtering.
    // Noise handling is explicit: 60 Hz notch plus adjustable hysteretic input gate.
    class JCM8002203 : public Pedal {
        Param *preamp, *bass, *mid, *treble, *presence, *master, *sag, *gate;
        Biquad inputHP, inputHum, preLP, coupling1, coupling2, toneBass, toneMid, toneTreble;
        Biquad dcBlock, speakerRes, speakerLP, presenceShelf;
        Oversampler4x osV1a, osV1b, osPower;
        float sagEnv = 0.0f;
        float gateEnv = 0.0f, gateGain = 1.0f;
        bool gateOpen = true;

        static float triode(float x, float drive, float asymmetry) {
            const float v = x * drive;
            return v >= 0.0f ? std::tanh(v) : asymmetry * std::tanh(v / asymmetry);
        }

    public:
        JCM8002203() : Pedal("Marshall JCM800 2203") {
            preamp = add("preamp", 0.0f, 1.0f, 0.58f);
            bass = add("bass", 0.0f, 1.0f, 0.50f);
            mid = add("mid", 0.0f, 1.0f, 0.55f);
            treble = add("treble", 0.0f, 1.0f, 0.58f);
            presence = add("presence", 0.0f, 1.0f, 0.45f);
            master = add("master", 0.0f, 1.0f, 0.55f);
            sag = add("sag", 0.0f, 1.0f, 0.25f);
            gate = add("gate", -80.0f, -30.0f, -55.0f);
            category = "Amps";
            isAmp = true;
            color[0] = 0.20f; color[1] = 0.19f; color[2] = 0.18f;
            hasColor = true;
        }

        void prepare(float sr) override {
            Pedal::prepare(sr);
            osV1a.init(sampleRate); osV1b.init(sampleRate); osPower.init(sampleRate);
            inputHP.highpass(sampleRate, 45.0f);
            inputHum.notch(sampleRate, 60.0f, 18.0f);
            preLP.lowpass(sampleRate, 8000.0f);
            coupling1.highpass(sampleRate, 12.0f);
            coupling2.highpass(sampleRate, 18.0f);
            dcBlock.highpass(sampleRate, 24.0f);
            speakerRes.peaking(sampleRate, 105.0f, 1.0f, 2.5f);
            speakerLP.lowpass(sampleRate, 7200.0f);
            presenceShelf.shelf(sampleRate, 3500.0f, 0.0f, true);
            updateTone(bass->get(), mid->get(), treble->get());
            reset();
        }

        void reset() override {
            inputHP.reset(); inputHum.reset(); preLP.reset(); coupling1.reset(); coupling2.reset();
            toneBass.reset(); toneMid.reset(); toneTreble.reset(); dcBlock.reset();
            speakerRes.reset(); speakerLP.reset(); presenceShelf.reset(); osV1a.reset(); osV1b.reset(); osPower.reset();
            sagEnv = 0.0f; gateEnv = 0.0f; gateGain = 1.0f; gateOpen = true;
        }

        void process(float* x, int n) override {
            const float b = bass->nextBlock(n, sampleRate);
            const float m = mid->nextBlock(n, sampleRate);
            const float t = treble->nextBlock(n, sampleRate);
            const float presenceV = presence->nextBlock(n, sampleRate);
            const float pg = preamp->nextBlock(n, sampleRate);
            const float out = master->nextBlock(n, sampleRate);
            const float sagV = sag->nextBlock(n, sampleRate);
            const float gateDb = gate->nextBlock(n, sampleRate);
            updateTone(b, m, t);

            const float presenceDb = (presenceV - 0.5f) * 8.0f;
            speakerLP.lowpass(sampleRate, 5200.0f + presenceV * 4200.0f);
            speakerRes.peaking(sampleRate, 105.0f, 1.0f, 2.5f - presenceV * 1.2f);
            presenceShelf.shelf(sampleRate, 3500.0f, presenceDb, true);
            const float sagDepth = sagV * 0.24f;
            const float atk = 1.0f - std::exp(-1.0f / (0.012f * sampleRate));
            const float rel = 1.0f - std::exp(-1.0f / (0.180f * sampleRate));
            const float gateAtk = 1.0f - std::exp(-1.0f / (0.003f * sampleRate));
            const float gateRel = 1.0f - std::exp(-1.0f / (0.110f * sampleRate));
            const float preGain = 1.5f + 11.0f * pg * pg;
            const float level = dbToLin(-20.0f + out * 24.0f);

            for (int i = 0; i < n; ++i) {
                float s = inputHum.process(inputHP.process(x[i]));
                const float detector = std::fabs(s);
                gateEnv += (detector - gateEnv) * (detector > gateEnv ? gateAtk : gateRel);
                const float gateLevelDb = 20.0f * std::log10(gateEnv + 1e-7f);
                if (!gateOpen && gateLevelDb > gateDb + 2.0f) gateOpen = true;
                else if (gateOpen && gateLevelDb < gateDb - 4.0f) gateOpen = false;
                const float gateTarget = gateOpen ? 1.0f : 0.002f;
                gateGain += (gateTarget - gateGain) * (gateTarget > gateGain ? gateAtk : gateRel);
                s *= gateGain;

                s = osV1a.process(preLP.process(s) * preGain, [](float v) { return triode(v, 1.55f, 0.84f); });
                s = coupling1.process(s);
                s = osV1b.process(s * (1.15f + 3.6f * pg), [](float v) { return triode(v, 2.0f, 0.62f); });
                s = coupling2.process(s);
                s = std::tanh(s * 1.05f) * 0.90f;
                s = toneBass.process(s);
                s = toneMid.process(s);
                s = toneTreble.process(s);
                s = osPower.process(s * (1.25f + 1.8f * pg), [](float v) {
                    return v >= 0.0f ? std::tanh(v * 1.20f) : std::tanh(v * 0.90f) * 0.96f;
                });
                const float a = std::fabs(s);
                sagEnv += (a - sagEnv) * (a > sagEnv ? atk : rel);
                s *= 1.0f - sagDepth * std::clamp(sagEnv, 0.0f, 1.0f);
                s = dcBlock.process(s);
                s = speakerRes.process(s);
                s = speakerLP.process(s);
                s *= dbToLin(presenceDb * 0.5f);
                x[i] = s * level;
            }
        }

    private:
        void updateTone(float b, float m, float t) {
            toneBass.shelf(sampleRate, 125.0f, (b - 0.5f) * 24.0f, false);
            toneMid.peaking(sampleRate, 720.0f, 0.72f, (m - 0.5f) * 20.0f);
            toneTreble.shelf(sampleRate, 3300.0f, (t - 0.5f) * 24.0f, true);
        }
    };

    struct AmpAsset {
        std::string name;
        std::string source;   // "Native", nome do pack ou "Plugin"
        std::string file;     // .ptc/.pctp/.pp de origem
        std::function<std::unique_ptr<Pedal>()> make;
        bool fromPlugin = false;
    };

    class AmpLibrary {
    public:
        AmpLibrary() {
            add("Marshall Plexi 1959", [] { return std::make_unique<Marshall>(); });
            add("Marshall JCM800 2203", [] { return std::make_unique<JCM8002203>(); });
        }
        void add(std::string name, std::function<std::unique_ptr<Pedal>()> f,
                 bool fromPlugin = false, std::string source = "Native", std::string file = {}) {
            amps_.push_back({ std::move(name), std::move(source), std::move(file),
                              std::move(f), fromPlugin });
        }
        void clearPlugins() {
            amps_.erase(std::remove_if(amps_.begin(), amps_.end(),
                [](const AmpAsset& x) { return x.fromPlugin; }), amps_.end());
        }
        const std::vector<AmpAsset>& all() const { return amps_; }
        std::unique_ptr<Pedal> create(int i) const {
            if (i < 0 || i >= (int)amps_.size()) return nullptr;
            auto p = amps_[i].make();
            if (p) {
                p->category = "Amps";
                p->isAmp = true;
            }
            return p;
        }
    private:
        std::vector<AmpAsset> amps_;
    };

}  // namespace gp