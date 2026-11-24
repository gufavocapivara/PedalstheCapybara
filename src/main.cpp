// Pedals the Capybara - interface (Dear ImGui + GLFW + OpenGL3)
//
// Layout: menu -> header (logo, status, meters, gain/master)
//         -> library | signal chain (pedals + amp together) -> status bar.
//         The Lua editor and the plugin manager open as floating windows.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#  include <shellapi.h>
#  pragma comment(lib, "shell32.lib")
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <GL/gl.h>

#include "stb_image.h"

#include "assets.h"
#include "audio.h"
#include "plugin_editor.h"
#include "plugins.h"
#include "lua_pedal.h"
#include "amps.h"
#include "textures.h"
#include "portable-file-dialogs.h"
#include "ui_shared.h"

// ---------------------------------------------------------------------- main
int main() {
#if defined(_WIN32)
    SetProcessDPIAware();
#endif
    if (!glfwInit()) return 1;

    float scaleX = 1.0f, scaleY = 1.0f;
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor())
        glfwGetMonitorContentScale(mon, &scaleX, &scaleY);
    g_scale = std::clamp(scaleX > 0.0f ? scaleX : 1.0f, 1.0f, 2.5f);

    int workX = 0, workY = 0, workW = 1920, workH = 1080;
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor())
        glfwGetMonitorWorkarea(mon, &workX, &workY, &workW, &workH);

    int winW = std::min((int)(1440 * g_scale), (int)(workW * 0.94f));
    int winH = std::min((int)(880 * g_scale), (int)(workH * 0.94f));

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(winW, winH, "Pedals the Capybara", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwSetWindowSizeLimits(window, std::min((int)(1080 * g_scale), workW), std::min((int)(680 * g_scale), workH),
                            GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowPos(window, workX + (workW - winW) / 2, workY + (workH - winH) / 2);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths) {
        for (int i = 0; i < count; ++i) if (paths[i]) g_droppedPaths.emplace_back(paths[i]);
    });

    {
        std::vector<unsigned char> icon;
        buildWindowIcon(icon, 64);
        GLFWimage img;
        img.width = 64; img.height = 64; img.pixels = icon.data();
        glfwSetWindowIcon(window, 1, &img);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    loadFonts(g_scale);
    setupStyle();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    AudioIO audio;
    AssetLibrary library;
    AmpLibrary ampLibrary;
    Engine& engine = audio.engine();
    Pedalboard& board = engine.board;

    PluginManager pluginManager;
    TextureCache& texCache = globalTextureCache();

    auto reloadPlugins = [&] {
        pluginManager.refresh();
        loadLuaPlugins(pluginManager, library, ampLibrary, &texCache);
    };
    reloadPlugins();

    // Cadeia inicial: gate ligado, drive/delay prontos para ligar e um amp.
    for (const char* n : { "Noise Gate", "Overdrive", "Delay" })
        for (int i = 0; i < (int)library.all().size(); ++i)
            if (library.all()[i].name == n) {
                auto p = library.create(i);
                if (!p) continue;
                p->enabled = (std::string(n) == "Noise Gate");
                board.add(std::move(p));
            }
    if (auto a = ampLibrary.create(0)) {
        a->enabled = true;
        board.add(std::move(a));
    }

    static PluginEditor editor;
    editor.textures = &texCache;
    editor.reload = reloadPlugins;
    editor.addPedal = [&](std::unique_ptr<Pedal> p) {
        if (board.add(std::move(p)) < 0) ShowToast("The signal chain is full (maximum 15 slots).", true);
    };

    auto importPath = [&](const fs::path& source) {
        std::string err;
        fs::path installed;
        if (pluginManager.importFile(source, err, &installed)) {
            reloadPlugins();
            ShowToast("Imported: " + (installed.empty() ? source.filename().string() : installed.filename().string()));
        } else {
            ShowToast("Import failed: " + err, true);
        }
    };

    auto importDialog = [&] {
        auto files = pfd::open_file("Import plugin or plugin pack", "",
            { "Pedals the Capybara", "*.pctp *.pp *.ptc *.lua",
              "Plugin package (*.pctp)", "*.pctp",
              "Plugin pack (*.pp)", "*.pp",
              "All files", "*" },
            pfd::opt::multiselect).result();
        for (const auto& f : files) importPath(f);
    };

    auto createPackDialog = [&] {
        const std::string folder = pfd::select_folder("Select the folder with your plugins").result();
        if (folder.empty()) return;
        const fs::path src(folder);
        const std::string def = src.filename().string() + ".pp";
        const std::string out = pfd::save_file("Save plugin pack (.pp)", def,
            { "Plugin pack (*.pp)", "*.pp" }).result();
        if (out.empty()) return;
        std::string err;
        int count = 0;
        if (pluginManager.createPackFromFolder(src, out, src.filename().string(), "", err, &count)) {
            ShowToast("Pack created with " + std::to_string(count) + " plugin(s): " + fs::path(out).filename().string());
            importPath(out);
        } else {
            ShowToast("Could not create pack: " + err, true);
        }
    };

    float assetsW = S(288);
    char filter[64] = "";
    bool wantOpenDevices = false;
    int inSel = 0, outSel = 0;
    bool exclusive = false;
    int bufferSel = 1;
    const int kBuffers[] = { 128, 256, 512 };
    std::string audioErr;
    float meterIn = 0, meterOut = 0;
    bool wantQuit = false;
    bool showGuiPlugins = false;
    bool showEditor = false;
    bool showAbout = false;

    // Sobe o audio automaticamente com os dispositivos padrao.
    if (!audio.start(inSel - 1, outSel - 1, exclusive, kBuffers[bufferSel], audioErr)) {
        wantOpenDevices = true;
        ShowToast("Audio could not start: " + audioErr, true);
    }

    while (!glfwWindowShouldClose(window) && !wantQuit) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        board.gc();

        // Arquivos arrastados para a janela.
        if (!g_droppedPaths.empty()) {
            for (const auto& path : g_droppedPaths) {
                const std::string ext = lowerAscii(fs::path(path).extension().string());
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                    showEditor = true;
                    if (!editor.addImageFile(path)) ShowToast("Could not read image: " + path, true);
                } else {
                    importPath(path);
                }
            }
            g_droppedPaths.clear();
        }

        float menuH = 0;
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12), S(6)));
            if (ImGui::BeginMainMenuBar()) {
                menuH = ImGui::GetWindowSize().y;
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("New pedalboard")) board.clear();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Import plugin / package...")) importDialog();
                    if (ImGui::MenuItem("Create plugin pack (.pp)...")) createPackDialog();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Exit")) wantQuit = true;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Audio")) {
                    if (ImGui::MenuItem("Configure devices...")) wantOpenDevices = true;
                    if (ImGui::MenuItem(audio.running() ? "Stop audio" : "Start audio")) {
                        if (audio.running()) audio.stop();
                        else if (!audio.start(inSel - 1, outSel - 1, exclusive, kBuffers[bufferSel], audioErr))
                            wantOpenDevices = true;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Plugins")) {
                    if (ImGui::MenuItem("Lua Editor", nullptr, &showEditor)) {}
                    if (ImGui::MenuItem("Installed Plugins", nullptr, &showGuiPlugins)) {}
                    ImGui::Separator();
                    if (ImGui::MenuItem("Import .pctp / .pp...")) importDialog();
                    if (ImGui::MenuItem("Create plugin pack (.pp)...")) createPackDialog();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Open Plugins Folder")) openFolder(pluginManager.dir());
                    if (ImGui::MenuItem("Open Packs Folder")) openFolder(pluginManager.packsDir());
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help")) {
                    if (ImGui::MenuItem("About")) showAbout = true;
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }
            ImGui::PopStyleVar();
        }

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float headerH = S(72);
        const float statusH = S(26);
        ImVec2 areaPos(vp->Pos.x, vp->Pos.y + menuH);
        float areaH = vp->Size.y - menuH;
        float bodyY = areaPos.y + headerH;
        float bodyH = areaH - headerH - statusH;
        float bodyW = vp->Size.x;

        meterIn = std::max(meterIn * 0.90f, engine.inPeak.load());
        meterOut = std::max(meterOut * 0.90f, engine.outPeak.load());

        HeaderRefs hr;
        hr.audio = &audio;
        hr.engine = &engine;
        hr.wantDevices = &wantOpenDevices;
        hr.showEditor = &showEditor;
        hr.showPlugins = &showGuiPlugins;
        hr.meterIn = std::clamp((20.0f * std::log10(meterIn + 1e-6f) + 60.0f) / 60.0f, 0.0f, 1.0f);
        hr.meterOut = std::clamp((20.0f * std::log10(meterOut + 1e-6f) + 60.0f) / 60.0f, 0.0f, 1.0f);
        DrawHeader(areaPos, ImVec2(vp->Size.x, headerH), hr);

        const ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoSavedSettings;

        BoardOps ops;

        // ---- painel de assets ----
        assetsW = std::clamp(assetsW, S(230), bodyW * 0.48f);
        ImGui::SetNextWindowPos(ImVec2(areaPos.x, bodyY));
        ImGui::SetNextWindowSize(ImVec2(assetsW, bodyH));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.078f, 0.090f, 0.110f, 1));
        ImGui::Begin("##assets", nullptr, panelFlags);
        ImGui::PushFont(fontSemi);
        ImGui::TextUnformatted("LIBRARY");
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("  double-click or drag");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filtro", "search by name, category or pack...", filter, sizeof(filter));
        ImGui::Spacing();

        auto drawPedalGroup = [&](bool onlyPlugins) {
            std::vector<std::string> cats;
            for (auto& a : library.all()) {
                if (onlyPlugins && !a.fromPlugin) continue;
                if (std::find(cats.begin(), cats.end(), a.category) == cats.end()) cats.push_back(a.category);
            }
            for (auto& cat : cats) {
                ImVec4 cc = catColorVec(cat);
                ImGui::PushStyleColor(ImGuiCol_Text, cc);
                ImGui::TextUnformatted(cat.c_str());
                ImGui::PopStyleColor();
                for (int i = 0; i < (int)library.all().size(); ++i) {
                    const PedalAsset& a = library.all()[i];
                    if (onlyPlugins && !a.fromPlugin) continue;
                    if (a.category != cat) continue;
                    if (!ContainsNoCase(a.name, filter) && !ContainsNoCase(a.category, filter) &&
                        !ContainsNoCase(a.source, filter)) continue;
                    const std::string sub = a.fromPlugin
                        ? (a.source.empty() ? std::string("Lua plugin") : "Lua plugin - " + a.source)
                        : a.category;
                    if (AssetRow(i, a.name.c_str(), sub.c_str(), ImGui::GetColorU32(cc), false, false, "ASSET")) {
                        ops.addAsset = i; ops.addAt = -1;
                    }
                }
                ImGui::Spacing();
            }
        };

        auto drawAmpGroup = [&](bool onlyPlugins) {
            bool any = false;
            for (int i = 0; i < (int)ampLibrary.all().size(); ++i) {
                const AmpAsset& a = ampLibrary.all()[i];
                if (onlyPlugins && !a.fromPlugin) continue;
                if (!ContainsNoCase(a.name, filter) && !ContainsNoCase(a.source, filter)) continue;
                any = true;
                const std::string sub = a.fromPlugin ? "Amp - " + a.source : "Native amp";
                if (AssetRow(10000 + i, a.name.c_str(), sub.c_str(),
                             ImGui::GetColorU32(catColorVec("Amps")), false, false, "AMP")) {
                    ops.addAmp = i; ops.addAt = -1;
                }
            }
            if (!any) ImGui::TextDisabled(onlyPlugins ? "No plugin amps found." : "No amps found.");
        };

        if (ImGui::BeginTabBar("##abas", ImGuiTabBarFlags_FittingPolicyScroll)) {
            if (ImGui::BeginTabItem("All")) {
                ImGui::BeginChild("##lista_all", ImVec2(0, -S(36)), ImGuiChildFlags_None);
                drawPedalGroup(false);
                ImVec4 cc = catColorVec("Amps");
                ImGui::PushStyleColor(ImGuiCol_Text, cc);
                ImGui::TextUnformatted("Amps");
                ImGui::PopStyleColor();
                drawAmpGroup(false);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Pedals")) {
                ImGui::BeginChild("##listapedais", ImVec2(0, -S(36)), ImGuiChildFlags_None);
                drawPedalGroup(false);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Amps")) {
                ImGui::TextDisabled("Amps are normal pedals: drag one into the chain.");
                ImGui::BeginChild("##listaamps", ImVec2(0, -S(36)), ImGuiChildFlags_None);
                drawAmpGroup(false);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("My Plugins")) {
                ImGui::BeginChild("##listaplugins", ImVec2(0, -S(36)), ImGuiChildFlags_None);
                drawPedalGroup(true);
                drawAmpGroup(true);
                if (!std::any_of(library.all().begin(), library.all().end(),
                                 [](const PedalAsset& a) { return a.fromPlugin; }) &&
                    !std::any_of(ampLibrary.all().begin(), ampLibrary.all().end(),
                                 [](const AmpAsset& a) { return a.fromPlugin; }))
                    ImGui::TextDisabled("No plugins yet. Import a .pctp/.pp or use the editor.");
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Packs")) {
                std::map<std::string, int> packs;
                for (const auto& p : pluginManager.all())
                    if (!p.packName.empty()) packs[p.packName]++;
                ImGui::BeginChild("##listapacks", ImVec2(0, -S(36)), ImGuiChildFlags_None);
                if (packs.empty()) {
                    ImGui::TextDisabled("No plugin packs installed.");
                    ImGui::TextWrapped("A .pp pack is a ZIP with several plugins. Use \"Create pack\" to turn a folder into one.");
                }
                for (const auto& kv : packs) {
                    ImGui::PushID(kv.first.c_str());
                    ImGui::TextUnformatted(kv.first.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%d plugin%s)", kv.second, kv.second == 1 ? "" : "s");
                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(6), S(5)));
        const float third = (ImGui::GetContentRegionAvail().x - S(8)) / 3.0f;
        if (ImGui::Button("Import...", ImVec2(third, 0))) importDialog();
        ImGui::SameLine();
        if (ImGui::Button("Create pack...", ImVec2(third, 0))) createPackDialog();
        ImGui::SameLine();
        if (ImGui::Button("Manage...", ImVec2(third, 0))) showGuiPlugins = true;
        ImGui::PopStyleVar();
        ImGui::End();
        ImGui::PopStyleColor();

        // ---- cadeia ----
        ImGui::SetNextWindowPos(ImVec2(areaPos.x + assetsW, bodyY));
        ImGui::SetNextWindowSize(ImVec2(bodyW - assetsW, bodyH));
        ImGui::Begin("##main", nullptr, panelFlags | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(fontSemi);
        ImGui::TextUnformatted("SIGNAL CHAIN");
        ImGui::PopFont();
        ImGui::SameLine(0, S(14));
        ImGui::TextDisabled("guitar -> pedals + amp -> speakers");
        ImGui::SameLine(0, S(24));
        ImGui::TextDisabled("%d slot%s", board.size(), board.size() == 1 ? "" : "s");
        ImGui::SameLine(0, S(20));
        if (board.ampIndex() < 0) {
            if (ImGui::SmallButton("Add an amp")) ImGui::OpenPopup("##quickamp");
            if (ImGui::BeginPopup("##quickamp")) {
                for (int i = 0; i < (int)ampLibrary.all().size(); ++i)
                    if (ImGui::MenuItem(ampLibrary.all()[i].name.c_str())) {
                        ops.addAmp = i; ops.addAt = -1;
                    }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(no amp yet)");
        } else {
            ImGui::TextDisabled("AMP: %s", board.amp()->name.c_str());
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - S(60));
        if (ImGui::SmallButton("Clear")) board.clear();
        ImGui::Spacing();

        float maxCardH = S(230);
        for (int i = 0; i < board.size(); ++i) maxCardH = std::max(maxCardH, SizeFor(*board.at(i)).h);
        float boardH = maxCardH + S(34);
        ImGui::BeginChild("##board", ImVec2(0, boardH), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        if (board.size() == 0) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 a = ImGui::GetCursorScreenPos();
            ImVec2 b = ImVec2(a.x + ImGui::GetContentRegionAvail().x - S(4), a.y + maxCardH);
            dl->AddRect(a, b, pal::border, S(10), 0, S(1.5f));
            TextCentered(dl, fontSemi, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f - S(10)), pal::dim,
                         "Your signal chain is empty");
            TextCentered(dl, fontSmall, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f + S(12)), pal::dim,
                         "drag a pedal or amp from the library, or double-click it");
        }
        for (int pos = 0; pos < board.size(); ++pos) {
            DropSlot(pos, S(kSlotW), maxCardH, ops, false);
            ImGui::SameLine(0, 0);
            PedalCard(*board.at(pos), pos, ops);
            ImGui::SameLine(0, 0);
        }
        DropSlot(board.size(), S(230), maxCardH, ops, true);
        ImGui::EndChild();

        if (board.ampIndex() < 0) {
            ImGui::Spacing();
            ImGui::TextDisabled("Tip: an amp is a normal pedal here. Open the Amps tab and double-click one; it is placed in the chain and can be dragged anywhere.");
        }
        ImGui::End();

        // ---- divisor ----
        ImGui::SetNextWindowPos(ImVec2(areaPos.x + assetsW - S(3), bodyY));
        ImGui::SetNextWindowSize(ImVec2(S(6), bodyH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##divisor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
        ImGui::InvisibleButton("##d", ImVec2(S(6), bodyH));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) assetsW += io.MouseDelta.x;
        ImGui::End();
        ImGui::PopStyleVar();

        // ---- barra de status ----
        {
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - statusH));
            ImGui::SetNextWindowSize(ImVec2(vp->Size.x, statusH));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(12), 0));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.065f, 0.080f, 1));
            ImGui::Begin("##status", nullptr, panelFlags);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            char left[200];
            if (audio.running())
                std::snprintf(left, sizeof(left), "%.0f Hz  |  buffer %d  |  CPU %.1f%%  |  %d slots  |  %d plugins",
                    (float)audio.sampleRate(), audio.bufferSize(), engine.cpuLoad.load(), board.size(),
                    (int)pluginManager.all().size());
            else
                std::snprintf(left, sizeof(left), "audio stopped  |  %d slots  |  %d plugins",
                    board.size(), (int)pluginManager.all().size());
            TextAt(dl, fontSmall, ImVec2(p.x + S(12), p.y + (statusH - fontSmall->FontSize) * 0.5f), pal::dim, left);
            char right[300];
            std::snprintf(right, sizeof(right), "Plugins: %s", pluginManager.dir().string().c_str());
            float tw = fontSmall->CalcTextSizeA(fontSmall->FontSize, FLT_MAX, 0.0f, right).x;
            TextAt(dl, fontSmall, ImVec2(p.x + vp->Size.x - S(12) - tw, p.y + (statusH - fontSmall->FontSize) * 0.5f),
                   pal::dim, right);
            ImGui::End();
        }

        // ---- editor (janela flutuante) ----
        if (showEditor) {
            ImGui::SetNextWindowSize(ImVec2(S(980), S(660)), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f),
                                    ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            if (ImGui::Begin("Pedal Editor (Lua)", &showEditor, ImGuiWindowFlags_NoCollapse)) {
                editor.draw(board, pluginManager);
            }
            ImGui::End();
        }

        // ---- dispositivos ----
        if (wantOpenDevices) { audio.refreshDevices(); ImGui::OpenPopup("Audio devices"); wantOpenDevices = false; }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Audio devices", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            auto combo = [](const char* label, int& sel, const std::vector<std::string>& names) {
                const char* cur = (sel <= 0 || sel > (int)names.size()) ? "System Default" : names[sel - 1].c_str();
                ImGui::SetNextItemWidth(S(380));
                if (ImGui::BeginCombo(label, cur)) {
                    if (ImGui::Selectable("System Default", sel == 0)) sel = 0;
                    for (int i = 0; i < (int)names.size(); ++i)
                        if (ImGui::Selectable(names[i].c_str(), sel == i + 1)) sel = i + 1;
                    ImGui::EndCombo();
                }
            };
            if (inSel > (int)audio.inputs().size()) inSel = 0;
            if (outSel > (int)audio.outputs().size()) outSel = 0;
            combo("Input (guitar)", inSel, audio.inputs());
            combo("Output (speakers)", outSel, audio.outputs());

            const char* bufNames[] = { "128 frames (lower latency)", "256 frames (recommended)", "512 frames (more stable)" };
            ImGui::SetNextItemWidth(S(380));
            if (ImGui::BeginCombo("Buffer", bufNames[bufferSel])) {
                for (int i = 0; i < 3; ++i)
                    if (ImGui::Selectable(bufNames[i], bufferSel == i)) bufferSel = i;
                ImGui::EndCombo();
            }
            ImGui::Checkbox("Exclusive mode (lower latency, requires an audio interface)", &exclusive);
            ImGui::Spacing();
            ImGui::TextDisabled("With speakers, your pickups may cause feedback. Start with the Master volume low.");
            if (!audioErr.empty()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1), "%s", audioErr.c_str());
            ImGui::Separator();
            if (ImGui::Button(audio.running() ? "Restart" : "Start", ImVec2(S(120), 0))) {
                audioErr.clear();
                if (audio.start(inSel - 1, outSel - 1, exclusive, kBuffers[bufferSel], audioErr))
                    ImGui::CloseCurrentPopup();
                else
                    ShowToast("Audio could not start: " + audioErr, true);
            }
            ImGui::SameLine();
            if (audio.running() && ImGui::Button("Stop", ImVec2(S(120), 0))) audio.stop();
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(S(120), 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- gerenciador de plugins ----
        if (showGuiPlugins) {
            static ImGuiTextFilter pluginFilter;
            static int selected = -1;
            ImGui::SetNextWindowSize(ImVec2(S(620), S(560)), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Installed Plugins & Packs", &showGuiPlugins, ImGuiWindowFlags_NoCollapse)) {
                if (ImGui::Button("Import .pctp / .pp...")) importDialog();
                ImGui::SameLine();
                if (ImGui::Button("Create .pp from folder...")) createPackDialog();
                ImGui::SameLine();
                if (ImGui::Button("Refresh")) {
                    reloadPlugins();
                    selected = -1;
                    ShowToast("Plugin library reloaded.");
                }
                ImGui::SameLine();
                if (ImGui::Button("Open folder")) openFolder(pluginManager.dir());

                pluginFilter.Draw("##busca", -1);
                ImGui::Separator();
                ImGui::TextDisabled("%s", pluginManager.dir().string().c_str());
                ImGui::TextDisabled("Formats: .pctp (one plugin), .pp (pack), .ptc/.lua (folder plugin)");

                const auto& plist = pluginManager.all();
                if (ImGui::BeginChild("##listaPlugins", ImVec2(0, -S(190)), ImGuiChildFlags_Borders)) {
                    if (plist.empty()) ImGui::TextDisabled("No plugins found.");
                    for (int i = 0; i < (int)plist.size(); ++i) {
                        const auto& p = plist[i];
                        if (!pluginFilter.PassFilter(p.name.c_str()) &&
                            !pluginFilter.PassFilter(p.description.c_str()) &&
                            !pluginFilter.PassFilter(p.packName.c_str()) &&
                            !pluginFilter.PassFilter(p.file.c_str())) continue;
                        const bool bad = !p.status.empty() && p.status != "ok";
                        ImGui::PushStyleColor(ImGuiCol_Text, bad ? ImVec4(0.95f, 0.42f, 0.36f, 1)
                                                                 : ImGui::GetStyleColorVec4(ImGuiCol_Text));
                        char label[256];
                        std::snprintf(label, sizeof(label), "%s%s###p%d", p.name.c_str(),
                                      p.isAmp ? "  [AMP]" : "", i);
                        if (ImGui::Selectable(label, selected == i)) selected = i;
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s\n%s\nSource: %s\nStatus: %s",
                                p.file.c_str(),
                                p.description.empty() ? "(no description)" : p.description.c_str(),
                                p.sourceLabel().c_str(),
                                p.status.empty() ? "not loaded" : p.status.c_str());
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::Separator();
                if (selected >= 0 && selected < (int)plist.size()) {
                    const auto& p = plist[selected];
                    ImGui::PushFont(fontSemi);
                    ImGui::TextUnformatted(p.name.c_str());
                    ImGui::PopFont();
                    ImGui::SameLine();
                    if (p.isAmp) ImGui::TextDisabled("[AMP]");
                    ImGui::TextDisabled("%s  |  %s%s%s", p.file.c_str(), p.sourceLabel().c_str(),
                        p.packName.empty() ? "" : "  |  pack: ", p.packName.c_str());
                    if (!p.author.empty() || !p.version.empty())
                        ImGui::TextDisabled("Author: %s   Version: %s", p.author.c_str(), p.version.c_str());
                    ImGui::TextWrapped("%s", p.description.empty() ? "(no description)" : p.description.c_str());
                    if (!p.status.empty() && p.status != "ok")
                        ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Error: %s", p.status.c_str());
                    else
                        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.5f, 1), "Loaded and ready.");
                    if (!p.script.empty() && ImGui::Button("Edit in Editor")) {
                        editor.openPlugin(p);
                        showEditor = true;
                    }
                    ImGui::SameLine();
                    const bool packItem = p.fromPack && !p.packFile.empty();
                    const char* delLabel = packItem ? "Delete pack..." : "Delete...";
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.16f, 0.14f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.20f, 0.18f, 1.0f));
                    if (ImGui::Button(delLabel)) ImGui::OpenPopup("Delete plugin?");
                    ImGui::PopStyleColor(2);
                    if (ImGui::BeginPopupModal("Delete plugin?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        if (packItem)
                            ImGui::TextWrapped("Delete pack '%s' (%s) and all plugins inside it?", p.packName.c_str(), p.packFile.c_str());
                        else
                            ImGui::TextWrapped("Delete plugin '%s' (%s)?", p.name.c_str(), p.file.c_str());
                        ImGui::TextDisabled("This removes files from disk. Plugins already in the chain keep working until removed.");
                        ImGui::Spacing();
                        if (ImGui::Button("Delete", ImVec2(S(120), 0))) {
                            std::string err;
                            bool ok = packItem ? pluginManager.removePack(p.packFile, err)
                                               : pluginManager.removePlugin(p, err);
                            if (ok) {
                                reloadPlugins();
                                selected = -1;
                                ShowToast(packItem ? "Pack deleted." : "Plugin deleted.");
                            } else {
                                ShowToast("Delete failed: " + err, true);
                            }
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel", ImVec2(S(120), 0))) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                } else {
                    ImGui::TextDisabled("Select a plugin to see details and edit its script.");
                }
            }
            ImGui::End();
        }

        // ---- sobre ----
        if (showAbout) ImGui::OpenPopup("About");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("About", &showAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushFont(fontBold);
            ImGui::TextUnformatted("Pedals the Capybara");
            ImGui::PopFont();
            ImGui::TextDisabled("Real-time pedalboard, amps and Lua plugins.");
            ImGui::Separator();
            ImGui::Text("Drag cards to reorder. The amp is a normal card in the chain.");
            ImGui::Text("Knob: drag vertically, mouse wheel, Shift = fine, double-click = default.");
            ImGui::Text("Plugins: .pctp (one plugin) or .pp (a pack of plugins), both ZIP files.");
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(S(120), 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- aplica operacoes ----
        if (ops.remove >= 0) board.remove(ops.remove);
        else if (ops.moveFrom >= 0) board.move(ops.moveFrom, ops.moveTo);
        else if (ops.addAsset >= 0) {
            if (auto p = library.create(ops.addAsset)) { p->enabled = true; board.add(std::move(p), ops.addAt); }
        } else if (ops.addAmp >= 0) {
            if (auto a = ampLibrary.create(ops.addAmp)) {
                a->enabled = true;
                if (board.add(std::move(a), ops.addAt) < 0) ShowToast("The signal chain is full.", true);
            }
        }

        DrawToast(vp, statusH);

        // ---- render ----
        ImGui::Render();
        int w, h; glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.051f, 0.059f, 0.075f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    audio.stop();
    board.destroyAllNow();       // libera texturas com o contexto OpenGL ainda vivo
    texCache.clear();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
