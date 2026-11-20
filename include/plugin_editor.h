// plugin_editor.h - Editor de pedais Lua dentro do app: Testar e Compilar (gera .lua + .ptc).
//
// [FIX] #include <atomic> e <cctype>.
// [FIX] compile() não sobrescreve arquivos existentes sem confirmação.
// [OPT] Template atualizado para a API de processamento por bloco.
#pragma once
#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <string>
#include "imgui.h"
#include "lua_pedal.h"

namespace gp {

    class PluginEditor {
    public:
        using AmpSink = std::function<void(std::unique_ptr<Pedal>)>;

        char code[65536];
        char name[64] = "My Fuzz";
        char desc[256] = "Simple Lua fuzz pedal";
        AmpSink ampSink;  // ligado pelo main: troca o amp da pedalboard

        PluginEditor() {
            std::snprintf(code, sizeof(code), "%s", kTemplate);
        }

        using AmpReloader = std::function<void()>;
        AmpReloader ampReloader;  // recarrega a biblioteca de amps após compilar

        void draw(Pedalboard& board, PluginManager& pm, AssetLibrary& lib) {
            if (ImGui::Button("Test")) test(board);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Validate the script and add an enabled test pedal to the pedalboard");
            ImGui::SameLine();
            if (ImGui::Button("Compile and Create .ptc")) compile(pm, lib);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Validate and save Plugins/<name>.lua and .ptc, then add the pedal to the library");
            ImGui::SameLine();
            if (ImGui::Button("Remove Test")) removeTest(board);
            ImGui::SameLine(0, 18);
            if (ImGui::Button("Test as Amp")) testAmp();
            ImGui::SameLine();
            if (ImGui::Button("Compile Amp")) compile(pm, lib, true);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use newamp() in the script; it will appear in the Amps tab");
            ImGui::SameLine(0, 18);
            if (ImGui::Button("Amp Template")) {
                std::snprintf(code, sizeof(code), "%s", kAmpTemplate);
                std::snprintf(name, sizeof(name), "%s", "My Amp");
                std::snprintf(desc, sizeof(desc), "%s", "Lua amp with buttons and controls");
                setMsg(true, "Amp template loaded. Use size(w,h) to change the card size.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Pedal Template")) {
                std::snprintf(code, sizeof(code), "%s", kTemplate);
                setMsg(true, "Pedal template loaded.");
            }

            ImGui::Spacing();
            ImGui::SetNextItemWidth(190);
            ImGui::InputTextWithHint("##nome", "Plugin name", name, sizeof(name));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##desc", "Description", desc, sizeof(desc));

            for (int i = 0; i < board.size(); ++i)
                if (auto* lp = dynamic_cast<LuaPedal*>(board.at(i)))
                    if (lp->isTest && lp->broken.load(std::memory_order_acquire))
                        ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Test pedal runtime error: %s", lp->rtErr);

            if (!msg.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, msgOk ? ImVec4(0.35f, 0.85f, 0.5f, 1) : ImVec4(1, 0.4f, 0.35f, 1));
                ImGui::TextWrapped("%s", msg.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::InputTextMultiline("##code", code, sizeof(code), ImVec2(-1, -1), ImGuiInputTextFlags_AllowTabInput);
        }

        void openFile(const std::filesystem::path& p, const std::string& n, const std::string& d) {
            std::ifstream f(p, std::ios::binary);
            if (!f) { setMsg(false, "Could not open " + p.string()); return; }
            std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            std::snprintf(code, sizeof(code), "%s", s.c_str());
            std::snprintf(name, sizeof(name), "%s", n.c_str());
            std::snprintf(desc, sizeof(desc), "%s", d.c_str());
            setMsg(true, "Plugin carregado no editor.");
        }

    private:
        std::string msg;
        bool msgOk = false;

        void setMsg(bool ok, std::string m) { msgOk = ok; msg = std::move(m); }

        static std::string slugify(const std::string& s) {
            std::string o;
            for (unsigned char c : s) {
                if (c < 128 && std::isalnum(c)) o += (char)std::tolower(c);
                else if (!o.empty() && o.back() != '_') o += '_';
            }
            while (!o.empty() && o.back() == '_') o.pop_back();
            return o.empty() ? "plugin" : o;
        }

        void removeTest(Pedalboard& board) {
            for (int i = 0; i < board.size(); ++i)
                if (auto* lp = dynamic_cast<LuaPedal*>(board.at(i)))
                    if (lp->isTest) { board.remove(i); return; }
        }

        void testAmp() {
            auto p = std::make_unique<LuaPedal>(std::string(code), "=editor");
            if (!p->error.empty()) { setMsg(false, p->error); return; }
            if (!p->isAmp) { setMsg(false, "Use newamp(\"Name\") in the script (instead of np) to test it as an amp."); return; }
            p->enabled = true;
            if (ampSink) ampSink(std::move(p));
            setMsg(true, "Test amp is active as the last stage in the signal chain.");
        }

        void test(Pedalboard& board) {
            auto p = std::make_unique<LuaPedal>(std::string(code), "=editor");
            if (!p->error.empty()) { setMsg(false, p->error); return; }
            removeTest(board);
            p->isTest = true;
            p->category = p->bar.empty() ? "Plugins" : p->bar;
            p->enabled = true;
            if (board.add(std::move(p)) < 0) { setMsg(false, "Pedalboard is full (maximum 15 pedals)."); return; }
            setMsg(true, "Test passed: pedal added to the end of the pedalboard and enabled.");
        }

        void compile(PluginManager& pm, AssetLibrary& lib, bool wantAmp = false) {
            namespace fs = std::filesystem;
            LuaPedal probe{ std::string(code), "=editor" };
            if (!probe.error.empty()) { setMsg(false, "Compilation failed: " + probe.error); return; }
            if (wantAmp && !probe.isAmp) {
                setMsg(false, "Compilation failed: use newamp(\"Name\") in the script to create an amp.");
                return;
            }
            if (!wantAmp && probe.isAmp) {
                setMsg(false, "This script creates an amp. Use \"Compile Amp\".");
                return;
            }

            const std::string slug = slugify(name);
            std::error_code ec;
            fs::create_directories(pm.dir(), ec);

            auto luaPath = pm.dir() / (slug + ".lua");
            auto ptcPath = pm.dir() / (slug + ".ptc");

            if (fs::exists(luaPath, ec) || fs::exists(ptcPath, ec)) {
                // não sobrescreve: gera com sufixo _2, _3, ...
                int n = 2;
                while (fs::exists(pm.dir() / (slug + "_" + std::to_string(n) + ".lua"), ec) ||
                    fs::exists(pm.dir() / (slug + "_" + std::to_string(n) + ".ptc"), ec))
                    ++n;
                std::string newSlug = slug + "_" + std::to_string(n);
                luaPath = pm.dir() / (newSlug + ".lua");
                ptcPath = pm.dir() / (newSlug + ".ptc");
            }

            {
                std::ofstream lua(luaPath, std::ios::binary);
                std::ofstream ptc(ptcPath, std::ios::binary);
                if (!lua || !ptc) { setMsg(false, "Could not save to the Plugins folder."); return; }
                lua << code;
                ptc << "NAME=" << name << "\n"
                    << "DESCRIPTION=" << desc << "\n"
                    << "SCRIPT=" << luaPath.filename().string() << "\n";
            }

            pm.refresh();
            loadLuaPlugins(pm, lib, &globalTextureCache());
            if (ampReloader) ampReloader();
            setMsg(true, "Plugin created: " + ptcPath.filename().string() + " (+ " + luaPath.filename().string() + "). "
                + (wantAmp ? std::string("Amp") : std::string("Pedal"))
                + " \"" + probe.name + "\" is available in the \""
                + (wantAmp ? std::string("Amps") : (probe.bar.empty() ? std::string("Plugins") : probe.bar))
                + "\" tab in the Library panel.");
        }

        static constexpr const char* kAmpTemplate = R"LUA(-- Lua amp: always the last stage in the signal chain.
local function main()
    local amp = newamp("My Amp")

    -- BUTTONSPIN becomes an ON/OFF button (min=0, max=1) or an integer +/- counter.
    amp.instance(BUTTONSPIN, "clean / drive", 0, 1, 0)
    amp.instance(SLIDER, "gain", 0, 1, 0.5)
    amp.instance(SLIDER, "bass", 0, 1, 0.5)
    amp.instance(SLIDER, "middle", 0, 1, 0.5)
    amp.instance(SLIDER, "treble", 0, 1, 0.5)
    amp.instance(SLIDER, "volume", 0, 1, 0.7)

    amp.color(20, 20, 24)                -- RGB 0..255
    amp.size(330, 300)                   -- card width x height (customizable)
    amp.oversample(true)                 -- prevents aliasing in distortion

    amp.process = function(x, n, drive, gain, bass, middle, treble, volume)
        local g = 0.5 + gain * (drive > 0.5 and 14.0 or 2.0)
        for i = 1, n do
            x[i] = math.tanh(x[i] * g) * volume
        end
        return x
    end
end

main()
)LUA";

        static constexpr const char* kTemplate = R"LUA(-- Lua pedal (block API: one call per audio block)
local function main()
    local nab = newassetsbar("Drives")        -- tab in the Library panel
    local newpedal = np("Lua Fuzz")           -- create the pedal

    -- instance(type, name, min, max, default)
    -- SLIDER = continuous knob; BUTTONSPIN = ON/OFF button (0..1) or +/- counter (integers)
    newpedal.instance(SLIDER, "drive", 0, 1, 0.5)
    newpedal.instance(SLIDER, "level", 0, 1, 0.5)

    newpedal.color(220, 60, 40)               -- RGB 0..255
    newpedal.linkbar(nab)
    newpedal.oversample(true)                 -- anti-aliasing for distortion/amps

    -- runs once per audio BLOCK (n samples); receives x[1..n] and the parameters.
    -- Return the same x table (modified in place) to output the signal.
    local lp = 0
    newpedal.process = function(x, n, drive, level)
        for i = 1, n do
            local y = math.tanh(x[i] * drive)
            lp = lp + (y - lp) * 0.3
            x[i] = lp * level
        end
        return x
    end
end

main()
)LUA";
    };

}  // namespace gp