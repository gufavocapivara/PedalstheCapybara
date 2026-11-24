// plugin_editor.h - Editor de pedais/amps Lua dentro do app.
//
// Recursos:
//   - Testar o script com as imagens em memoria (preview imediato);
//   - Salvar como plugin em pasta (Plugins/<nome>.lua + .ptc + <nome>_assets/);
//   - Exportar um pacote .pctp (ZIP com manifesto, script e imagens);
//   - Importar uma pasta de plugin para editar;
//   - Gerenciar as imagens usadas pelo script.
#pragma once
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "portable-file-dialogs.h"

#include "lua_pedal.h"
#include "plugin_package.h"

namespace gp {

    class PluginEditor {
    public:
        using AddPedalFn = std::function<void(std::unique_ptr<Pedal>)>;
        using ReloadFn = std::function<void()>;

        char code[65536];
        char name[64] = "My Fuzz";
        char desc[256] = "Simple Lua fuzz pedal";

        AddPedalFn addPedal;     // ligado pelo main: adiciona/ troca o pedal na cadeia
        ReloadFn reload;         // ligado pelo main: recarrega a biblioteca de plugins
        TextureCache* textures = nullptr;

        PluginEditor() {
            std::snprintf(code, sizeof(code), "%s", kTemplate);
        }

        void draw(Pedalboard& board, PluginManager& pm) {
            drawToolbar(board, pm);
            ImGui::Spacing();
            ImGui::SetNextItemWidth(ES(200));
            ImGui::InputTextWithHint("##nome", "Plugin name", name, sizeof(name));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##desc", "Description", desc, sizeof(desc));

            if (!msg.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, msgOk ? ImVec4(0.35f, 0.85f, 0.5f, 1)
                                                            : ImVec4(1, 0.4f, 0.35f, 1));
                ImGui::TextWrapped("%s", msg.c_str());
                ImGui::PopStyleColor();
            }

            const float bottom = ImGui::GetContentRegionAvail().y;
            const float assetsW = ES(220);
            ImGui::BeginChild("##editor_assets", ImVec2(assetsW, bottom), ImGuiChildFlags_Borders);
            drawAssets();
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##editor_code", ImVec2(0, bottom), ImGuiChildFlags_Borders);
            ImGui::InputTextMultiline("##code", code, sizeof(code), ImVec2(-1, -1),
                                      ImGuiInputTextFlags_AllowTabInput);
            ImGui::EndChild();
        }

        // Carrega um plugin da biblioteca (pasta, .pctp ou .pp) para editar.
        void openPlugin(const PluginInfo& info) {
            namespace fs = std::filesystem;
            assets.clear();
            assetsFolder.clear();
            assetRevision++;
            std::snprintf(name, sizeof(name), "%s", info.name.c_str());
            std::snprintf(desc, sizeof(desc), "%s", info.description.c_str());

            if (info.package) {
                std::string script;
                std::string err;
                if (!info.package->readText(info.script, script, err)) {
                    setMsg(false, "Could not read the script: " + err);
                    return;
                }
                std::snprintf(code, sizeof(code), "%s", script.c_str());
                for (const auto& entry : info.package->imageEntries(info.rootPrefix)) {
                    std::vector<unsigned char> bytes;
                    if (!info.package->readEntry(entry, bytes, err)) continue;
                    std::string rel = entry;
                    if (!info.rootPrefix.empty() && rel.size() > info.rootPrefix.size() + 1)
                        rel = rel.substr(info.rootPrefix.size() + 1);
                    if (rel.rfind("assets/", 0) == 0) rel = rel.substr(7);
                    assets[rel] = std::move(bytes);
                }
                setMsg(true, "Package plugin loaded. Images: " + std::to_string(assets.size()) + ".");
                return;
            }

            if (info.scriptPath.empty() || !fs::exists(info.scriptPath)) {
                setMsg(false, "The script file was not found.");
                return;
            }
            std::string script;
            if (!readFile(info.scriptPath, script)) {
                setMsg(false, "Could not read " + info.scriptPath.filename().string() + ".");
                return;
            }
            std::snprintf(code, sizeof(code), "%s", script.c_str());
            loadAssetsFromFolder(info.scriptPath.parent_path());
            setMsg(true, "Plugin loaded. Images: " + std::to_string(assets.size()) + ".");
        }

        // Usado pelo drag-and-drop do main.
        bool addImageFile(const std::filesystem::path& path) {
            std::vector<unsigned char> bytes;
            if (!readFile(path, bytes)) return false;
            std::string key = path.filename().string();
            for (int i = 2; assets.count(key); ++i)
                key = path.stem().string() + "_" + std::to_string(i) + path.extension().string();
            assets[key] = std::move(bytes);
            assetRevision++;
            setMsg(true, "Image added: " + key);
            return true;
        }

        void importFolderDialog() {
            const std::string folder = pfd::select_folder("Import a plugin folder").result();
            if (!folder.empty()) importFolder(std::filesystem::path(folder));
        }

        bool importFolder(const std::filesystem::path& folder) {
            namespace fs = std::filesystem;
            std::error_code ec;
            if (!fs::is_directory(folder, ec)) { setMsg(false, "Not a folder."); return false; }

            fs::path script;
            for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                if (it->is_directory(ec)) {
                    const std::string d = lowerAscii(it->path().filename().string());
                    if (d == "assets" || d == "images" || d == "img" ||
                        (d.size() > 7 && d.compare(d.size() - 7, 7, "_assets") == 0) ||
                        d == "build" || d == ".git" || d == ".vs")
                        it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file(ec)) continue;
                if (lowerAscii(it->path().extension().string()) == ".lua") { script = it->path(); break; }
            }
            if (script.empty()) { setMsg(false, "No .lua file found in that folder."); return false; }

            std::string text;
            if (!readFile(script, text)) { setMsg(false, "Could not read the Lua script."); return false; }
            std::snprintf(code, sizeof(code), "%s", text.c_str());

            std::string pluginName = script.stem().string();
            std::string pluginDesc;
            fs::path manifest = script.parent_path() / (script.stem().string() + ".ptc");
            if (!fs::exists(manifest, ec)) {
                for (fs::directory_iterator it(script.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
                    if (it->is_regular_file(ec) && lowerAscii(it->path().extension().string()) == ".ptc") {
                        manifest = it->path();
                        break;
                    }
                }
            }
            if (fs::exists(manifest, ec)) {
                std::string mt;
                if (readFile(manifest, mt)) {
                    const PackageManifest m = parseManifest(mt);
                    if (!m.name.empty()) pluginName = m.name;
                    pluginDesc = m.description;
                }
            }
            std::snprintf(name, sizeof(name), "%s", pluginName.c_str());
            std::snprintf(desc, sizeof(desc), "%s", pluginDesc.c_str());

            assets.clear();
            assetsFolder = script.parent_path();
            assetRevision++;
            loadAssetsFromFolder(assetsFolder);
            setMsg(true, "Folder imported. Images: " + std::to_string(assets.size()) + ".");
            return true;
        }

    private:
        std::string msg;
        bool msgOk = false;
        std::filesystem::path assetsFolder;
        LuaAssetMap assets;
        int assetRevision = 0;
        std::string selectedAsset;

        static float ES(float v) { return v * (ImGui::GetFontSize() / 16.0f); }

        void setMsg(bool ok, std::string m) { msgOk = ok; msg = std::move(m); }

        static bool readFile(const std::filesystem::path& path, std::string& out) {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }

        static bool readFile(const std::filesystem::path& path, std::vector<unsigned char>& out) {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }

        static std::string slugify(const std::string& s) {
            std::string o;
            for (unsigned char c : s) {
                if (c < 128 && std::isalnum(c)) o += (char)std::tolower(c);
                else if (!o.empty() && o.back() != '_') o += '_';
            }
            while (!o.empty() && o.back() == '_') o.pop_back();
            return o.empty() ? "plugin" : o;
        }

        void loadAssetsFromFolder(const std::filesystem::path& folder) {
            namespace fs = std::filesystem;
            std::error_code ec;
            for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const std::string ext = lowerAscii(it->path().extension().string());
                if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") continue;
                std::vector<unsigned char> bytes;
                if (!readFile(it->path(), bytes)) continue;
                std::string rel = fs::relative(it->path(), folder, ec).generic_string();
                if (ec) { ec.clear(); rel = it->path().filename().string(); }
                // "assets/face.png", "images/face.png" and "myplugin_assets/face.png"
                // all become "face.png", which is what loadimage("face.png") expects.
                const auto slash = rel.find('/');
                if (slash != std::string::npos) {
                    const std::string first = lowerAscii(rel.substr(0, slash));
                    if (first == "assets" || first == "images" || first == "img" ||
                        (first.size() > 7 && first.compare(first.size() - 7, 7, "_assets") == 0))
                        rel = rel.substr(slash + 1);
                }
                assets[rel] = std::move(bytes);
            }
        }

        void drawToolbar(Pedalboard& board, PluginManager& pm) {
            if (ImGui::Button("Test")) test(board);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Validate and add an enabled test pedal to the chain");
            ImGui::SameLine();
            if (ImGui::Button("Stop Test")) removeTest(board);
            ImGui::SameLine(0, ES(18));
            if (ImGui::Button("Save to Plugins")) compileFolder(pm);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save as a folder plugin: .lua + .ptc + <name>_assets/");
            ImGui::SameLine();
            if (ImGui::Button("Export .pctp")) exportPackage();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create one ZIP package with the script and all images");
            ImGui::SameLine(0, ES(18));
            if (ImGui::Button("Import Folder")) importFolderDialog();
            ImGui::SameLine(0, ES(18));
            if (ImGui::Button("Pedal Template")) {
                std::snprintf(code, sizeof(code), "%s", kTemplate);
                setMsg(true, "Pedal template loaded.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Amp Template")) {
                std::snprintf(code, sizeof(code), "%s", kAmpTemplate);
                std::snprintf(name, sizeof(name), "%s", "My Amp");
                std::snprintf(desc, sizeof(desc), "%s", "Lua amp with images and controls");
                setMsg(true, "Amp template loaded. Amps live in the same signal chain as pedals.");
            }
        }

        void drawAssets() {
            ImGui::TextDisabled("IMAGES");
            if (ImGui::Button("Add images...", ImVec2(-1, 0))) {
                auto files = pfd::open_file("Add images", "",
                    { "Images", "*.png *.jpg *.jpeg", "All files", "*" },
                    pfd::opt::multiselect).result();
                int added = 0;
                for (const auto& f : files) if (addImageFile(f)) ++added;
                if (!files.empty()) setMsg(true, std::to_string(added) + " image(s) added.");
            }
            if (ImGui::Button("Import folder...", ImVec2(-1, 0))) importFolderDialog();
            if (!assetsFolder.empty()) {
                ImGui::TextDisabled("%s", assetsFolder.filename().string().c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", assetsFolder.string().c_str());
            }
            ImGui::Separator();

            const float previewH = assets.empty() ? 0.0f : ES(118);
            ImGui::BeginChild("##asset_list", ImVec2(0, -previewH - (assets.empty() ? 0.0f : ES(6))),
                              ImGuiChildFlags_None);
            if (assets.empty()) {
                ImGui::TextWrapped("No images. Add face.png, knob.png, led.png or a footswitch image.");
            }
            std::string removeKey;
            for (auto& kv : assets) {
                const bool sel = (selectedAsset == kv.first);
                ImGui::PushID(kv.first.c_str());
                if (ImGui::Selectable(kv.first.c_str(), sel)) selectedAsset = kv.first;
                ImGui::SameLine(0, 0);
                ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - ES(20));
                if (ImGui::SmallButton("x")) removeKey = kv.first;
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (!removeKey.empty()) {
                assets.erase(removeKey);
                if (selectedAsset == removeKey) selectedAsset.clear();
                assetRevision++;
                setMsg(true, "Image removed: " + removeKey);
            }

            if (!assets.empty()) {
                auto it = assets.find(selectedAsset);
                if (it == assets.end() && !assets.empty()) it = assets.begin();
                if (it != assets.end()) {
                    ImGui::Separator();
                    ImGui::TextDisabled("%s", it->first.c_str());
                    if (textures) {
                        const std::string key = "editorasset:" + it->first + ":" + std::to_string(assetRevision);
                        TextureId id = textures->loadFromMemory(key, it->second.data(), it->second.size());
                        auto inf = textures->info(id);
                        if (inf.valid()) {
                            const float maxW = ImGui::GetContentRegionAvail().x;
                            const float sc = std::min(maxW / std::max(1, inf.w), previewH / std::max(1, inf.h));
                            ImGui::Image((ImTextureID)(intptr_t)inf.gl_id,
                                         ImVec2(inf.w * sc, inf.h * sc));
                        } else {
                            ImGui::TextDisabled("Could not decode this image.");
                        }
                    }
                }
            }
        }

        void removeTest(Pedalboard& board) {
            for (int i = 0; i < board.size(); ++i)
                if (auto* lp = dynamic_cast<LuaPedal*>(board.at(i)))
                    if (lp->isTest) { board.remove(i); return; }
        }

        std::shared_ptr<LuaAssetMap> assetSnapshot() const {
            if (assets.empty()) return nullptr;
            return std::make_shared<LuaAssetMap>(assets);
        }

        void test(Pedalboard& board) {
            LuaPedal probe(std::string(code), "=editor", nullptr, assetSnapshot());
            if (!probe.error.empty()) { setMsg(false, probe.error); return; }
            removeTest(board);
            auto p = std::make_unique<LuaPedal>(std::string(code), "=editor", textures, assetSnapshot());
            if (!p->error.empty()) { setMsg(false, p->error); return; }
            p->isTest = true;
            p->category = p->bar.empty() ? (p->isAmp ? "Amps" : "Plugins") : p->bar;
            p->enabled = true;
            if (addPedal) addPedal(std::move(p));
            else if (board.add(std::move(p)) < 0) {
                setMsg(false, "Pedalboard is full (maximum 15 pedals).");
                return;
            }
            setMsg(true, probe.isAmp
                ? "Test amp added to the chain. Amps are normal pedals now: drag to reorder, on/off in the chain."
                : "Test passed: pedal added to the chain and enabled.");
        }

        bool compileFolder(PluginManager& pm) {
            namespace fs = std::filesystem;
            LuaPedal probe(std::string(code), "=editor", nullptr, assetSnapshot());
            if (!probe.error.empty()) { setMsg(false, "Compilation failed: " + probe.error); return false; }

            const std::string slug = slugify(name);
            std::error_code ec;
            fs::create_directories(pm.dir(), ec);

            fs::path luaPath = pm.dir() / (slug + ".lua");
            fs::path ptcPath = pm.dir() / (slug + ".ptc");
            fs::path assetsPath = pm.dir() / (slug + "_assets");
            if (fs::exists(luaPath, ec) || fs::exists(ptcPath, ec)) {
                int n = 2;
                while (fs::exists(pm.dir() / (slug + "_" + std::to_string(n) + ".lua"), ec) ||
                       fs::exists(pm.dir() / (slug + "_" + std::to_string(n) + ".ptc"), ec))
                    ++n;
                const std::string s = slug + "_" + std::to_string(n);
                luaPath = pm.dir() / (s + ".lua");
                ptcPath = pm.dir() / (s + ".ptc");
                assetsPath = pm.dir() / (s + "_assets");
            }

            {
                std::ofstream lua(luaPath, std::ios::binary);
                if (!lua) { setMsg(false, "Could not save to the Plugins folder."); return false; }
                lua << code;
            }
            std::string icon;
            for (const auto& kv : assets) {
                if (!isImageFile(kv.first)) continue;
                icon = (assetsPath.filename() / kv.first).generic_string();
                break;
            }
            {
                std::ofstream ptc(ptcPath, std::ios::binary);
                if (!ptc) { setMsg(false, "Could not save the .ptc manifest."); return false; }
                ptc << "NAME=" << name << "\n"
                    << "DESCRIPTION=" << desc << "\n"
                    << "VERSION=1.0\n"
                    << "SCRIPT=" << luaPath.filename().string() << "\n";
                if (!icon.empty()) ptc << "ICON=" << icon << "\n";
            }
            for (const auto& kv : assets) {
                const fs::path out = assetsPath / fs::path(kv.first);
                fs::create_directories(out.parent_path(), ec);
                std::ofstream f(out, std::ios::binary);
                if (f) f.write(reinterpret_cast<const char*>(kv.second.data()), (std::streamsize)kv.second.size());
            }

            if (reload) reload();
            setMsg(true, "Saved " + ptcPath.filename().string() + " + " + luaPath.filename().string() +
                         " (" + std::to_string(assets.size()) + " image(s)). It is now in the library.");
            return true;
        }

        void exportPackage() {
            LuaPedal probe(std::string(code), "=editor", nullptr, assetSnapshot());
            if (!probe.error.empty()) { setMsg(false, "Export failed: " + probe.error); return; }

            const std::string slug = slugify(name);
            const std::string file = pfd::save_file("Export .pctp package",
                slug + ".pctp",
                { "Pedals the Capybara plugin (*.pctp)", "*.pctp" }).result();
            if (file.empty()) return;

            std::vector<PackageFile> files;
            std::string icon;
            for (const auto& kv : assets) {
                if (!isImageFile(kv.first)) continue;
                icon = "assets/" + kv.first;
                break;
            }
            {
                std::string manifest = "NAME=" + std::string(name) + "\n"
                    + "DESCRIPTION=" + std::string(desc) + "\n"
                    + "VERSION=1.0\n"
                    + "SCRIPT=" + slug + ".lua\n";
                if (!icon.empty()) manifest += "ICON=" + icon + "\n";
                PackageFile f;
                f.name = "plugin.ptc";
                f.data.assign(manifest.begin(), manifest.end());
                files.push_back(std::move(f));
            }
            {
                PackageFile f;
                f.name = slug + ".lua";
                f.data.assign(code, code + std::strlen(code));
                files.push_back(std::move(f));
            }
            for (const auto& kv : assets) {
                PackageFile f;
                f.name = "assets/" + kv.first;
                f.data = kv.second;
                files.push_back(std::move(f));
            }

            std::string error;
            if (!writePackage(file, files, error)) { setMsg(false, "Export failed: " + error); return; }
            setMsg(true, "Package created: " + file + " (" + std::to_string(files.size()) + " files).");
        }

        static constexpr const char* kAmpTemplate = R"LUA(-- Lua amp: an amp is a normal pedal in the signal chain.
-- Images: put face.png/knob.png/led.png in the editor's IMAGES list.
local function main()
    local art = loadassets("myamp")
    local face = art.loadimage("face.png")
    local knob = art.loadimage("knob.png")
    local led  = art.loadimage("led.png")
    local foot = art.loadimage("footswitch.png")

    local amp = newamp("My Amp")
    amp.instance(BUTTONSPIN, "clean / drive", 0, 1, 0)
    amp.instance(SLIDER, "gain", 0, 1, 0.5)
    amp.instance(SLIDER, "bass", 0, 1, 0.5)
    amp.instance(SLIDER, "middle", 0, 1, 0.5)
    amp.instance(SLIDER, "treble", 0, 1, 0.5)
    amp.instance(SLIDER, "volume", 0, 1, 0.7)

    amp.color(20, 20, 24)
    amp.size(330, 300)
    amp.oversample(true)
    if face ~= 0 then amp.basetexture(face) end
    if knob ~= 0 then amp.knobtexture(knob) end
    if led ~= 0 then amp.ledtexture(led) end
    if foot ~= 0 then amp.footswitchtexture(foot) end

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
-- Put images in the editor's IMAGES list; they travel inside the .pctp.
local function main()
    local nab = newassetsbar("Drives")
    local art = loadassets("myfuzz")
    local face = art.loadimage("face.png")
    local knob = art.loadimage("knob.png")
    local led  = art.loadimage("led.png")
    local foot = art.loadimage("footswitch.png")

    local newpedal = np("Lua Fuzz")
    newpedal.instance(SLIDER, "drive", 0, 1, 0.5)
    newpedal.instance(SLIDER, "level", 0, 1, 0.5)
    newpedal.color(220, 60, 40)
    newpedal.linkbar(nab)
    newpedal.oversample(true)
    if face ~= 0 then newpedal.basetexture(face) end
    if knob ~= 0 then newpedal.knobtexture(knob) end
    if led ~= 0 then newpedal.ledtexture(led) end
    if foot ~= 0 then newpedal.footswitchtexture(foot) end

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
