// assets.h - Biblioteca de pedais disponíveis (painel "assets" da interface).
// Depois, pedais escritos em Lua entram aqui com add(...) usando uma fábrica LuaPedal.
//
// [FIX] Includes diretos de <string> e <vector>.
// [FIX] create() verifica nullptr retornado pelo make().
#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include "pedals.h"

namespace gp {

    struct PedalAsset {
        std::string name, category;
        std::string source;        // "Native", nome do pack ou "Plugin"
        std::string file;          // .ptc/.pctp/.pp de origem
        std::function<std::unique_ptr<Pedal>()> make;
        bool fromPlugin = false;   // true = veio de um plugin .ptc/.pctp/.pp
    };

    class AssetLibrary {
    public:
        AssetLibrary() {
            add("Compressor", "Dinâmica", [] { return std::make_unique<Compressor>(); });
            add("Noise Gate", "Dinâmica", [] { return std::make_unique<NoiseGate>(); });
            add("Overdrive", "Drive", [] { return std::make_unique<Drive>(Drive::Overdrive, "Overdrive"); });
            add("Distortion", "Drive", [] { return std::make_unique<Drive>(Drive::Distortion, "Distortion"); });
            add("Fuzz", "Drive", [] { return std::make_unique<Drive>(Drive::Fuzz, "Fuzz"); });
            add("Chorus", "Modulation", [] { return std::make_unique<Chorus>(); });
            add("Tremolo", "Modulation", [] { return std::make_unique<Tremolo>(); });
            add("Delay", "Time/Space", [] { return std::make_unique<Delay>(); });
            add("Reverb", "Time/Space", [] { return std::make_unique<Reverb>(); });
        }
        void add(std::string name, std::string category, std::function<std::unique_ptr<Pedal>()> f,
            bool fromPlugin = false, std::string source = "Native", std::string file = {}) {
            assets_.push_back({ std::move(name), std::move(category), std::move(source),
                                std::move(file), std::move(f), fromPlugin });
        }
        // remove só os assets vindos de plugins (usado ao reler a pasta Plugins)
        void clearPlugins() {
            assets_.erase(std::remove_if(assets_.begin(), assets_.end(),
                [](const PedalAsset& x) { return x.fromPlugin; }), assets_.end());
        }
        const std::vector<PedalAsset>& all() const { return assets_; }
        std::unique_ptr<Pedal> create(int i) const {
            if (i < 0 || i >= (int)assets_.size()) return nullptr;
            auto p = assets_[i].make();
            if (!p) return nullptr;
            p->category = assets_[i].category;
            return p;
        }
    private:
        std::vector<PedalAsset> assets_;
    };

}  // namespace gp