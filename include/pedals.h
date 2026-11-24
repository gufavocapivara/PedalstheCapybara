// pedals.h - Pedais de guitarra em tempo real.
// Regras do thread de áudio: sem alocação, sem locks, sem I/O.
// Toda memória é alocada em prepare(). Parâmetros são atomics suavizados.
//
// [FIX] Oversampler2x corrigido (segundo ramo agora processa o sinal de verdade).
// [FIX] Chorus/Delay/Reverb protegem contra sampleRate <= 0.
// [OPT] Parâmetros com next() inline e coeficiente constante.
// [OPT] Pedalboard com slots verificados antes do uso.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gp {

    constexpr float kPi = 3.14159265358979323846f;
    constexpr int kMaxBlock = 512;

    inline float dbToLin(float db) { return std::pow(10.0f, db * 0.05f); }

    // ---------------------------------------------------------------- Parâmetro
    struct Param {
        std::string name;
        float minV, maxV, defV;
        std::atomic<float> target;
        float cur;  // só o thread de áudio mexe
        // Botões/toggles (BUTTONSPIN) não podem ser suavizados: um valor
        // fracionário durante a transição quebra índices de modo no script.
        // Escrito apenas na construção, antes do áudio começar.
        bool discrete = false;

        // Nomes das posições para botões seletores (ex.: Clean/Crunch/OD1/OD2).
        // Vazio = botão ON/OFF ou contador numérico.
        std::vector<std::string> labels;

        Param(std::string n, float mn, float mx, float def)
            : name(std::move(n)), minV(mn), maxV(mx), defV(def), target(def), cur(def) {
        }

        void set(float v) { target.store(std::clamp(v, minV, maxV), std::memory_order_relaxed); }
        float get() const { return target.load(std::memory_order_relaxed); }

        // valor suavizado (evita "zipper noise" ao girar o knob)
        float next(float coef = 0.002f) {
            if (discrete) { cur = get(); return cur; }
            cur += (get() - cur) * coef;
            return cur;
        }

        // Para plugins em bloco: next() era chamado 1x por bloco, então o
        // coeficiente fixo levava segundos para chegar ao valor do knob.
        // Aqui o coeficiente é calculado pelo tempo real do bloco.
        float nextBlock(int n, float sr, float ms = 20.0f) {
            if (discrete) { cur = get(); return cur; }
            if (sr <= 0.0f || n <= 0) return next();
            float coef = 1.0f - std::exp(-(float)n / (sr * ms * 0.001f));
            cur += (get() - cur) * coef;
            return cur;
        }
    };

    // ------------------------------------------------------------------ Biquad
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void lowpass(float fs, float f, float q = 0.70710678f) {
            if (fs <= 0.0f) return;
            f = std::min(f, fs * 0.49f);
            float w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
            float al = s / (2 * q), a0 = 1 + al;
            b0 = (1 - c) * 0.5f / a0; b1 = (1 - c) / a0; b2 = b0;
            a1 = -2 * c / a0; a2 = (1 - al) / a0;
        }
        void highpass(float fs, float f, float q = 0.70710678f) {
            if (fs <= 0.0f) return;
            f = std::min(f, fs * 0.49f);
            float w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
            float al = s / (2 * q), a0 = 1 + al;
            b0 = (1 + c) * 0.5f / a0; b1 = -(1 + c) / a0; b2 = b0;
            a1 = -2 * c / a0; a2 = (1 - al) / a0;
        }
        // Notch estreito: remove zumbido de rede (50/60 Hz) sem comer o baixo.
        void notch(float fs, float f, float q = 12.0f) {
            if (fs <= 0.0f) return;
            f = std::min(f, fs * 0.49f);
            float w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
            float al = s / (2 * q), a0 = 1 + al;
            b0 = 1 / a0; b1 = -2 * c / a0; b2 = 1 / a0;
            a1 = -2 * c / a0; a2 = (1 - al) / a0;
        }
        // Banda de EQ (RBJ): usada no tone stack do amp nativo.
        void peaking(float fs, float f, float q, float db) {
            if (fs <= 0.0f) return;
            f = std::clamp(f, 20.0f, fs * 0.49f);
            q = std::max(q, 0.05f);
            float A = std::pow(10.0f, db / 40.0f);
            float w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
            float al = s / (2 * q), a0 = 1 + al / A;
            b0 = (1 + al * A) / a0;
            b1 = (-2 * c) / a0;
            b2 = (1 - al * A) / a0;
            a1 = (-2 * c) / a0;
            a2 = (1 - al / A) / a0;
        }
        // Shelf de graves/agudos (RBJ). Em 0 dB os coeficientes ficam unitários.
        void shelf(float fs, float f, float db, bool high) {
            if (fs <= 0.0f) return;
            f = std::clamp(f, 20.0f, fs * 0.49f);
            float A = std::pow(10.0f, db / 40.0f);
            float w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
            float al = s * 0.5f * std::sqrt(2.0f);
            float sq = 2.0f * std::sqrt(A) * al;
            if (high) {
                float a0 = (A + 1) - (A - 1) * c + sq;
                b0 = A * ((A + 1) + (A - 1) * c + sq) / a0;
                b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
                b2 = A * ((A + 1) + (A - 1) * c - sq) / a0;
                a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
                a2 = ((A + 1) - (A - 1) * c - sq) / a0;
            } else {
                float a0 = (A + 1) + (A - 1) * c + sq;
                b0 = A * ((A + 1) - (A - 1) * c + sq) / a0;
                b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0;
                b2 = A * ((A + 1) - (A - 1) * c - sq) / a0;
                a1 = -2 * ((A - 1) + (A + 1) * c) / a0;
                a2 = ((A + 1) + (A - 1) * c - sq) / a0;
            }
        }
        float process(float x) {
            float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1 = z2 = 0; }
    };

    // Oversampling 4x: distorção gera harmônicos acima de Nyquist que voltam como
    // "chiado metálico" (aliasing). Aqui o sinal sobe para 4x em dois estágios 2x
    // (Butterworth de 4ª ordem em cada sentido), é distorcido e desce filtrado.
    //
    // O zero-stuffing insere um zero entre as amostras; o ganho 2 em cada estágio
    // compensa a perda de energia. Os filtros têm memória, então as amostras
    // "vazias" saem interpoladas.
    struct Oversampler4x {
        Biquad up1[2], up2[2], dn1[2], dn2[2];

        void init(float fs) {
            if (fs <= 0.0f) return;
            const float fc = fs * 0.45f;
            up1[0].lowpass(fs * 2, fc, 0.54119610f); up1[1].lowpass(fs * 2, fc, 1.30656296f);
            up2[0].lowpass(fs * 4, fc, 0.54119610f); up2[1].lowpass(fs * 4, fc, 1.30656296f);
            dn1[0].lowpass(fs * 2, fc, 0.54119610f); dn1[1].lowpass(fs * 2, fc, 1.30656296f);
            dn2[0].lowpass(fs * 4, fc, 0.54119610f); dn2[1].lowpass(fs * 4, fc, 1.30656296f);
            reset();
        }
        void reset() {
            for (auto& b : up1) b.reset();
            for (auto& b : up2) b.reset();
            for (auto& b : dn1) b.reset();
            for (auto& b : dn2) b.reset();
        }

        template <class F>
        float process(float x, F&& shaper) {
            // ---- sobe para 4x (dois estágios 2x) ----
            const float a0 = up1[1].process(up1[0].process(2.0f * x));
            const float a1 = up1[1].process(up1[0].process(0.0f));
            float b0 = up2[1].process(up2[0].process(2.0f * a0));
            float b1 = up2[1].process(up2[0].process(0.0f));
            float b2 = up2[1].process(up2[0].process(2.0f * a1));
            float b3 = up2[1].process(up2[0].process(0.0f));

            b0 = shaper(b0); b1 = shaper(b1); b2 = shaper(b2); b3 = shaper(b3);

            // ---- desce 4x -> 2x -> 1x, filtrando e decimando ----
            const float c0 = dn2[1].process(dn2[0].process(b0));
            dn2[1].process(dn2[0].process(b1));
            const float c2 = dn2[1].process(dn2[0].process(b2));
            dn2[1].process(dn2[0].process(b3));
            const float y = dn1[1].process(dn1[0].process(c0));
            dn1[1].process(dn1[0].process(c2));
            return y;
        }
    };

    // ------------------------------------------------------------- Base Pedal
    class Pedal {
    public:
        explicit Pedal(std::string n) : name(std::move(n)) {}
        virtual ~Pedal() = default;

        virtual void prepare(float sr) { sampleRate = sr > 0 ? sr : 48000.0f; blend = 0; active = false; }
        virtual void process(float* x, int n) = 0;  // in-place, n <= kMaxBlock
        virtual void reset() {}                      // limpa caudas (delay, reverb...)

        // Liga/desliga com crossfade de 8 ms: sem "clique" ao pisar no pedal.
        void run(float* x, int n) {
            if (n <= 0) return;
            const bool on = enabled.load(std::memory_order_relaxed);
            if (!on && blend <= 0.0f) {
                if (active) { reset(); active = false; }
                return;
            }
            active = true;
            std::copy(x, x + n, tmp);
            process(tmp, n);
            const float step = 1.0f / (0.008f * sampleRate);
            for (int i = 0; i < n; ++i) {
                blend = std::clamp(blend + (on ? step : -step), 0.0f, 1.0f);
                x[i] = x[i] * (1.0f - blend) + tmp[i] * blend;
            }
        }

        Param* find(const std::string& n) {
            for (auto& p : params) if (p->name == n) return p.get();
            return nullptr;
        }

        std::string name;
        std::string category;  // preenchido pela AssetLibrary (cor/agrupamento na UI)
        float color[3] = { 0.6f, 0.6f, 0.65f };  // RGB 0..1 (definido por pedais Lua)
        bool hasColor = false;
        // Um amp e apenas um pedal especial: fica na mesma cadeia, pode ser
        // arrastado e so pode existir um por pedalboard (o Pedalboard::add
        // substitui o anterior automaticamente).
        bool isAmp = false;
        std::atomic<bool> enabled{ false };
        std::vector<std::unique_ptr<Param>> params;

    protected:
        Param* add(const std::string& n, float mn, float mx, float def) {
            params.push_back(std::make_unique<Param>(n, mn, mx, def));
            return params.back().get();
        }
        float sampleRate = 48000.0f;

    private:
        float blend = 0.0f;
        bool active = false;
        float tmp[kMaxBlock];
    };

    // ------------------------------------------------------------- Noise Gate
    // Gate com histerese, hold e atenuação real (range). O modelo antigo fechava
    // só -29 dB e sem hold, então sobrava chiado audível entre as notas.
    class NoiseGate : public Pedal {
        Param* thresh, * release, * range;
        float env = 0, gain = 0, hold = 0;
        bool open = false;
    public:
        NoiseGate() : Pedal("Noise Gate") {
            thresh = add("threshold", -80.0f, -20.0f, -50.0f);  // dBFS RMS
            release = add("release", 20.0f, 800.0f, 180.0f);    // ms
            range = add("range", 0.0f, 80.0f, 60.0f);           // dB atenuados no silêncio
        }
        void prepare(float sr) override {
            Pedal::prepare(sr);
            reset();
        }
        void reset() override { env = 0; gain = 0; hold = 0; open = false; }
        void process(float* x, int n) override {
            const float openT = dbToLin(thresh->get());
            const float closeT = openT * 0.63f;             // histerese de ~4 dB
            const float floorG = dbToLin(-range->get());    // 0 dB = gate desligado
            const float atk = 1.0f - std::exp(-1.0f / (0.0015f * sampleRate));
            const float rel = 1.0f - std::exp(-1.0f / (release->get() * 0.001f * sampleRate));
            // Detector RMS (média quadrática com ataque = release): previsível
            // para ruído e sem abrir com o pico de cada amostra.
            const float detCoef = 1.0f - std::exp(-1.0f / (0.015f * sampleRate));
            const int holdSamples = (int)(0.060f * sampleRate);
            for (int i = 0; i < n; ++i) {
                const float sq = x[i] * x[i];
                env += (sq - env) * detCoef;
                const float lvl = std::sqrt(env);
                if (!open && lvl > openT) { open = true; hold = holdSamples; }
                else if (open) {
                    if (lvl < closeT) { if (hold > 0) --hold; else open = false; }
                    else hold = holdSamples;
                }
                const float target = open ? 1.0f : floorG;
                gain += (target - gain) * (target > gain ? atk : rel);
                x[i] *= gain;
            }
        }
    };

    // ------------------------------------------------------------- Compressor
    class Compressor : public Pedal {
        Param* thresh, * ratio, * makeup;
        float env = 0;
    public:
        Compressor() : Pedal("Compressor") {
            thresh = add("threshold", -50.0f, 0.0f, -22.0f);
            ratio = add("ratio", 1.0f, 20.0f, 4.0f);
            makeup = add("makeup", 0.0f, 24.0f, 6.0f);
        }
        void reset() override { env = 0; }
        void process(float* x, int n) override {
            const float atk = 1.0f - std::exp(-1.0f / (0.005f * sampleRate));
            const float rel = 1.0f - std::exp(-1.0f / (0.150f * sampleRate));
            const float th = thresh->get(), r = ratio->get();
            const float invR = 1.0f - 1.0f / r;
            for (int i = 0; i < n; ++i) {
                float a = std::fabs(x[i]);
                env += (a - env) * (a > env ? atk : rel);
                float db = 20.0f * std::log10(env + 1e-6f);
                float over = db - th;
                float gr = over > 0 ? -over * invR : 0.0f;
                x[i] *= dbToLin(gr + makeup->next());
            }
        }
    };

    // ------------------------------------------- Drive (Overdrive/Dist/Fuzz)
    // Ganho máximo contido + filtro antes e depois do clipping: é isso que
    // evita o "chiado" (harmônicos altos + ruído de entrada amplificado).
    class Drive : public Pedal {
    public:
        enum Mode { Overdrive, Distortion, Fuzz };
    private:
        Mode mode;
        Param* drive, * tone, * level;
        Biquad preHP, preLP, toneLP, dc;
        Oversampler4x os;
    public:
        Drive(Mode m, const char* n) : Pedal(n), mode(m) {
            drive = add("drive", 0.0f, 1.0f, 0.5f);
            tone = add("tone", 0.0f, 1.0f, 0.5f);
            level = add("level", 0.0f, 1.0f, 0.5f);
        }
        void prepare(float sr) override {
            Pedal::prepare(sr);
            os.init(sampleRate);
            preHP.highpass(sampleRate, mode == Fuzz ? 45.0f : (mode == Distortion ? 90.0f : 130.0f));
            // Limita a banda que entra no clipper: menos harmônicos gerados,
            // menos aliasing e menos chiado.
            preLP.lowpass(sampleRate, mode == Fuzz ? 6500.0f : (mode == Distortion ? 7000.0f : 8000.0f));
            dc.highpass(sampleRate, 24.0f);
            reset();
        }
        void reset() override { preHP.reset(); preLP.reset(); toneLP.reset(); dc.reset(); os.reset(); }

        void process(float* x, int n) override {
            // tone recalcula o filtro 1x por bloco (economia de CPU)
            toneLP.lowpass(sampleRate, 1200.0f * std::pow(6.5f, tone->get()));
            const float maxGain = mode == Overdrive ? 24.0f : (mode == Distortion ? 70.0f : 90.0f);
            for (int i = 0; i < n; ++i) {
                const float d = drive->next();
                const float g = 1.0f + d * d * maxGain;
                const float lvl = dbToLin(-18.0f + level->next() * 20.0f);
                float s = preHP.process(preLP.process(x[i])) * g;
                float y;
                switch (mode) {
                case Overdrive:
                    y = os.process(s, [](float v) { return std::tanh(v); });
                    break;
                case Distortion:
                    y = os.process(s, [](float v) {
                        return v > 0 ? std::tanh(v * 1.6f) : std::tanh(v) * 0.95f;
                        });
                    break;
                default:  // Fuzz
                    y = os.process(s, [](float v) {
                        return v > 0 ? 1.0f - std::exp(-v) : -0.7f * (1.0f - std::exp(v * 1.3f));
                        });
                    break;
                }
                y = dc.process(y);
                x[i] = toneLP.process(y) * lvl;
            }
        }
    };

    // ------------------------------------------------------------------ Chorus
    class Chorus : public Pedal {
        Param* rate, * depth, * mix;
        std::vector<float> buf;
        int w = 0;
        float phase = 0;
    public:
        Chorus() : Pedal("Chorus") {
            rate = add("rate", 0.1f, 5.0f, 0.8f);
            depth = add("depth", 0.0f, 8.0f, 3.0f);  // ms
            mix = add("mix", 0.0f, 1.0f, 0.5f);
        }
        void prepare(float sr) override {
            Pedal::prepare(sr);
            buf.assign(int(sampleRate * 0.05f) + 8, 0.0f);
            w = 0; phase = 0;
        }
        void reset() override { std::fill(buf.begin(), buf.end(), 0.0f); }
        void process(float* x, int n) override {
            const int size = (int)buf.size();
            if (size < 4) return;
            for (int i = 0; i < n; ++i) {
                phase += rate->get() / sampleRate; if (phase >= 1) phase -= 1;
                float d = (12.0f + depth->next() * std::sin(2 * kPi * phase)) * 0.001f * sampleRate;
                int di = (int)d; float fr = d - di;
                int i0 = w - di; if (i0 < 0) i0 += size;
                int i1 = i0 - 1; if (i1 < 0) i1 += size;
                float wet = buf[i0] * (1 - fr) + buf[i1] * fr;
                buf[w] = x[i]; w = (w + 1) % size;
                float m = mix->next();
                x[i] = x[i] * (1 - m) + wet * m;
            }
        }
    };

    // ----------------------------------------------------------------- Tremolo
    class Tremolo : public Pedal {
        Param* rate, * depth;
        float phase = 0;
    public:
        Tremolo() : Pedal("Tremolo") {
            rate = add("rate", 0.5f, 12.0f, 5.0f);
            depth = add("depth", 0.0f, 1.0f, 0.5f);
        }
        void reset() override { phase = 0; }
        void process(float* x, int n) override {
            for (int i = 0; i < n; ++i) {
                phase += rate->get() / sampleRate; if (phase >= 1) phase -= 1;
                x[i] *= 1.0f - depth->next() * (0.5f + 0.5f * std::sin(2 * kPi * phase));
            }
        }
    };

    // ------------------------------------------------------------------- Delay
    class Delay : public Pedal {
        Param* time, * feedback, * mix;
        std::vector<float> buf;
        int w = 0;
        float lp = 0;
    public:
        Delay() : Pedal("Delay") {
            time = add("time", 50.0f, 1000.0f, 380.0f);  // ms
            feedback = add("feedback", 0.0f, 0.9f, 0.35f);
            mix = add("mix", 0.0f, 1.0f, 0.3f);
        }
        void prepare(float sr) override {
            Pedal::prepare(sr);
            buf.assign(int(sampleRate * 1.1f) + 8, 0.0f);
            w = 0; lp = 0;
        }
        void reset() override { std::fill(buf.begin(), buf.end(), 0.0f); lp = 0; }
        void process(float* x, int n) override {
            const int size = (int)buf.size();
            if (size < 4) return;
            for (int i = 0; i < n; ++i) {
                float d = time->next(0.0004f) * 0.001f * sampleRate;
                d = std::min(d, (float)(size - 2));
                int di = (int)d; float fr = d - di;
                int i0 = w - di; if (i0 < 0) i0 += size;
                int i1 = i0 - 1; if (i1 < 0) i1 += size;
                float wet = buf[i0] * (1 - fr) + buf[i1] * fr;
                lp += (wet - lp) * 0.35f;  // repetições mais escuras, estilo fita
                buf[w] = x[i] + lp * feedback->next();
                w = (w + 1) % size;
                x[i] += wet * mix->next();
            }
        }
    };

    // ------------------------------------------------------------------ Reverb
    class Reverb : public Pedal {
        struct Comb {
            std::vector<float> b; int i = 0; float store = 0;
            float process(float in, float fb, float damp) {
                float o = b[i];
                store = o * (1 - damp) + store * damp;
                b[i] = in + store * fb;
                if (++i >= (int)b.size()) i = 0;
                return o;
            }
        };
        struct Allpass {
            std::vector<float> b; int i = 0;
            float process(float in) {
                float bo = b[i], o = bo - in;
                b[i] = in + bo * 0.5f;
                if (++i >= (int)b.size()) i = 0;
                return o;
            }
        };
        Param* size, * damp, * mix;
        Comb combs[8];
        Allpass aps[4];
    public:
        Reverb() : Pedal("Reverb") {
            size = add("size", 0.0f, 1.0f, 0.5f);
            damp = add("damp", 0.0f, 1.0f, 0.4f);
            mix = add("mix", 0.0f, 1.0f, 0.25f);
        }
        void prepare(float sr) override {
            Pedal::prepare(sr);
            static const int cT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            static const int aT[4] = { 556, 441, 341, 225 };
            float k = sampleRate / 44100.0f;
            for (int j = 0; j < 8; ++j) {
                combs[j].b.assign(std::max(1, int(cT[j] * k)), 0.0f);
                combs[j].i = 0; combs[j].store = 0;
            }
            for (int j = 0; j < 4; ++j) {
                aps[j].b.assign(std::max(1, int(aT[j] * k)), 0.0f);
                aps[j].i = 0;
            }
        }
        void reset() override {
            for (auto& c : combs) { std::fill(c.b.begin(), c.b.end(), 0.0f); c.store = 0; }
            for (auto& a : aps) std::fill(a.b.begin(), a.b.end(), 0.0f);
        }
        void process(float* x, int n) override {
            if (combs[0].b.empty() || aps[0].b.empty()) return;
            const float fb = 0.70f + size->get() * 0.28f, dm = damp->get() * 0.8f;
            for (int i = 0; i < n; ++i) {
                float in = x[i] * 0.015f, o = 0;
                for (auto& c : combs) o += c.process(in, fb, dm);
                for (auto& a : aps) o = a.process(o);
                x[i] += o * 3.0f * mix->next();
            }
        }
    };

    // -------------------------------------------------------------- Pedalboard
    // Cadeia dinâmica: a UI adiciona, remove e reordena pedais enquanto o áudio roda.
    // A ordem é publicada num único atomic<uint64_t> (15 slots x 4 bits, 0xF = fim),
    // então o thread de áudio sempre enxerga uma cadeia consistente, sem locks.
    // Pedais removidos só são destruídos ~1 s depois (gc), quando o áudio já largou eles.
    // O amp é um pedal como qualquer outro: fica na mesma cadeia, pode ser
    // reordenado/desligado e o Pedalboard garante que exista no maximo um.
    class Pedalboard {
    public:
        static constexpr int kSlots = 15;

        void prepare(float sr) {
            sampleRate = sr > 0 ? sr : 48000.0f;
            for (auto& s : slots) if (s.p) s.p->prepare(sampleRate);
        }

        // ---- thread da UI ----
        // Adiciona um pedal. Se for um amp e ja existir um amp na cadeia, o
        // antigo e removido e o novo ocupa o lugar dele (ou `pos`, se dado).
        int add(std::unique_ptr<Pedal> p, int pos = -1) {
            if (!p) return -1;
            if (p->isAmp) {
                const int existing = ampIndex();
                if (existing >= 0) {
                    if (pos < 0) pos = existing;
                    remove(existing);
                    if (pos > existing) --pos;
                }
            }
            int id = -1;
            for (int i = 0; i < kSlots; ++i) if (!slots[i].used) { id = i; break; }
            if (id < 0) return -1;
            p->prepare(sampleRate);
            slots[id].p = std::move(p);
            slots[id].used = true;
            slots[id].dying = false;

            if (pos < 0 || pos > orderSize) pos = orderSize;
            for (int i = orderSize; i > pos; --i) order[i] = order[i - 1];
            order[pos] = id;
            ++orderSize;
            publish();
            return id;
        }
        void remove(int pos) {
            if (pos < 0 || pos >= orderSize) return;
            const int id = order[pos];
            for (int i = pos; i + 1 < orderSize; ++i) order[i] = order[i + 1];
            --orderSize;
            publish();
            slots[id].dying = true;
            slots[id].freeAt = now() + 1.0;
        }
        void move(int from, int to) {
            if (from < 0 || from >= orderSize || to < 0 || to > orderSize) return;
            const int id = order[from];
            for (int i = from; i + 1 < orderSize; ++i) order[i] = order[i + 1];
            --orderSize;
            if (from < to) --to;
            to = std::clamp(to, 0, orderSize);
            for (int i = orderSize; i > to; --i) order[i] = order[i - 1];
            order[to] = id;
            ++orderSize;
            publish();
        }
        void clear() { while (orderSize > 0) remove(orderSize - 1); }

        // Encerramento: o áudio já foi parado, então pode destruir tudo agora.
        // (clear() + gc() esperaria 1 s e as texturas seriam liberadas depois do
        // contexto OpenGL, causando crash no shutdown.)
        void destroyAllNow() {
            chain.store(0xFFFFFFFFFFFFFFFFull, std::memory_order_release);
            orderSize = 0;
            for (auto& s : slots) {
                s.p.reset();
                s.used = false;
                s.dying = false;
            }
        }

        void gc() {
            const double t = now();
            for (auto& s : slots)
                if (s.dying && t >= s.freeAt) { s.p.reset(); s.used = false; s.dying = false; }
        }
        int size() const { return orderSize; }
        Pedal* at(int pos) { return (pos >= 0 && pos < orderSize) ? slots[order[pos]].p.get() : nullptr; }

        int ampIndex() const {
            for (int i = 0; i < orderSize; ++i) {
                const Pedal* p = slots[order[i]].p.get();
                if (p && p->isAmp) return i;
            }
            return -1;
        }
        Pedal* amp() {
            const int i = ampIndex();
            return i >= 0 ? at(i) : nullptr;
        }

        // ---- thread de áudio ----
        void process(float* x, int n) {
            const uint64_t c = chain.load(std::memory_order_acquire);
            for (int off = 0; off < n; off += kMaxBlock) {
                int cnt = std::min(kMaxBlock, n - off);
                for (int k = 0; k < kSlots; ++k) {
                    unsigned id = (unsigned)((c >> (4 * k)) & 0xF);
                    if (id == 0xF || id >= (unsigned)kSlots) break;
                    if (Pedal* p = slots[id].p.get()) p->run(x + off, cnt);
                }
            }
        }

    private:
        struct Slot { std::unique_ptr<Pedal> p; bool used = false, dying = false; double freeAt = 0; };
        Slot slots[kSlots];
        int order[kSlots] = {};  // ordem fixa: sem realocação nem iteradores no pedalboard
        int orderSize = 0;
        std::atomic<uint64_t> chain{ ~0ull };
        float sampleRate = 48000.0f;

        static double now() {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }
        void publish() {
            uint64_t v = ~0ull;
            for (int k = 0; k < orderSize && k < kSlots; ++k) {
                v &= ~(0xFull << (4 * k));
                v |= (uint64_t)order[k] << (4 * k);
            }
            chain.store(v, std::memory_order_release);
        }
    };

}  // namespace gp