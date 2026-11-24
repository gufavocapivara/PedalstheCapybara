// ui_shared.h - estado e declaracoes compartilhadas pela interface.
//
// Antes tudo isso vivia no topo de um unico main.cpp de 76 KB. Agora cada
// area da UI tem seu .cpp; este header so mantem o que e usado por mais de
// um arquivo (estado global, cores, helpers pequenos e assinaturas).
#pragma once

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

using namespace gp;
namespace fs = std::filesystem;

// ---- estado global ---------------------------------------------------------
extern float g_scale;
inline float S(float v) { return v * g_scale; }

extern ImFont* fontReg;
extern ImFont* fontSemi;
extern ImFont* fontBold;
extern ImFont* fontSmall;

namespace pal {
    inline const ImU32 bg      = IM_COL32(13, 15, 19, 255);
    inline const ImU32 panel   = IM_COL32(20, 23, 28, 255);
    inline const ImU32 panel2  = IM_COL32(26, 30, 37, 255);
    inline const ImU32 card    = IM_COL32(31, 36, 44, 255);
    inline const ImU32 border  = IM_COL32(44, 51, 62, 255);
    inline const ImU32 text    = IM_COL32(232, 235, 240, 255);
    inline const ImU32 dim     = IM_COL32(148, 157, 170, 255);
    inline const ImU32 accent  = IM_COL32(233, 161, 59, 255);
    inline const ImU32 accent2 = IM_COL32(78, 201, 176, 255);
    inline const ImU32 green   = IM_COL32(70, 200, 120, 255);
    inline const ImU32 red     = IM_COL32(224, 91, 76, 255);
    inline const ImU32 yellow  = IM_COL32(232, 197, 71, 255);
    inline const ImU32 shadow  = IM_COL32(0, 0, 0, 80);
}

inline ImU32 WithAlpha(ImU32 c, int a) { return (c & 0x00FFFFFFu) | ((ImU32)a << 24); }

inline void TextAt(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 col, const char* txt) {
    dl->AddText(f, f->FontSize, pos, col, txt);
}
inline void TextAtShadow(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 col, const char* txt) {
    dl->AddText(f, f->FontSize, ImVec2(pos.x + S(1), pos.y + S(1)), IM_COL32(0, 0, 0, 170), txt);
    dl->AddText(f, f->FontSize, pos, col, txt);
}
inline void TextCentered(ImDrawList* dl, ImFont* f, ImVec2 ctr, ImU32 col, const char* txt) {
    ImVec2 t = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, txt);
    dl->AddText(f, f->FontSize, ImVec2(ctr.x - t.x * 0.5f, ctr.y - t.y * 0.5f), col, txt);
}

inline ImVec4 catColorVec(const std::string& c) {
    if (c == "Drive") return ImVec4(0.93f, 0.55f, 0.22f, 1);
    if (c == "Modulation") return ImVec4(0.68f, 0.45f, 0.95f, 1);
    if (c == "Time/Space") return ImVec4(0.30f, 0.66f, 0.98f, 1);
    if (c == "Dinâmica") return ImVec4(0.35f, 0.82f, 0.55f, 1);
    if (c == "Amps") return ImVec4(0.95f, 0.72f, 0.30f, 1);
    return ImVec4(0.56f, 0.62f, 0.72f, 1);
}

inline bool ContainsNoCase(const std::string& hay, const char* needle) {
    if (!needle || !*needle) return true;
    std::string n(needle);
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    };
    return lower(hay).find(lower(n)) != std::string::npos;
}

// Cor de destaque do card. Pedais nativos guardam uma cor de fundo escura em
// p.color, que nao serve como destaque; nesse caso usamos a cor da categoria.
inline ImU32 AccentFor(const Pedal& p) {
    if (p.hasColor) {
        const float mx = std::max(p.color[0], std::max(p.color[1], p.color[2]));
        if (mx >= 0.45f) {
            const float k = std::min(1.0f, 0.9f / mx);
            return ImGui::GetColorU32(ImVec4(p.color[0] * k, p.color[1] * k, p.color[2] * k, 1.0f));
        }
    }
    return ImGui::GetColorU32(catColorVec(p.category));
}

inline constexpr float kSlotW = 26.0f;
inline constexpr float kKnobDia = 38.0f;

// ---- tipos compartilhados --------------------------------------------------
struct BoardOps {
    int remove = -1;
    int moveFrom = -1, moveTo = -1;
    int addAsset = -1, addAmp = -1, addAt = -1;
};

struct CardSize { float w = 0, h = 0; };

struct HeaderRefs {
    AudioIO* audio;
    Engine* engine;
    bool* wantDevices;
    bool* showEditor;
    bool* showPlugins;
    float meterIn, meterOut;
};

struct Toast {
    std::string text;
    bool error = false;
    double until = 0.0;
};

extern Toast g_toast;
extern std::vector<std::string> g_droppedPaths;

// ---- tema e fontes ---------------------------------------------------------
void setupStyle();
void loadFonts(float scale);

// ---- widgets ---------------------------------------------------------------
void FormatParam(const Param& p, float v, char* out, size_t n);
void DrawKnobVisual(ImDrawList* dl, ImVec2 ctr, float r, float t, ImU32 accent, bool hovered, bool active);
void DrawTexCover(ImDrawList* dl, TextureId id, ImVec2 a, ImVec2 b, float rounding, ImU32 tint);
void DrawTexFit(ImDrawList* dl, TextureId id, ImVec2 a, ImVec2 b, ImU32 tint);
bool DrawKnobTexture(ImDrawList* dl, TextureId id, ImVec2 ctr, float r, float t, ImU32 tint);
void Knob(Param& p, ImU32 accent, float dia, const TextureSet* tex = nullptr);
void ButtonSpin(Param& p, ImU32 accent, float width);
void DrawParam(Param& p, ImU32 accent, int widget, float knobDia, const TextureSet* tex = nullptr);
void MeterBar(const char* label, float lvl, float w, float h);
bool MiniKnob(const char* label, float* value, float mn, float mx, ImU32 accent, float dia);

// ---- cards da cadeia -------------------------------------------------------
int ParamColumns(const Pedal& p, int maxCols);
int ParamRows(const Pedal& p, int cols);
CardSize SizeFor(const Pedal& p);
bool PowerButton(const char* id, bool on, ImVec2 size, ImU32 accent, TextureId footTex);
void DropSlot(int pos, float w, float cardH, BoardOps& ops, bool last);
void PedalCard(Pedal& p, int pos, BoardOps& ops);

// ---- biblioteca, cabecalho, utilidades -------------------------------------
bool AssetRow(int index, const char* name, const char* sub, ImU32 color,
              bool error, bool selected, const char* payloadType);
void openUrl(const char* url);
void openFolder(const fs::path& p);
void DrawHeader(const ImVec2& pos, const ImVec2& size, HeaderRefs& r);
bool loadLogoIcon(std::vector<unsigned char>& px, int N);
void buildWindowIcon(std::vector<unsigned char>& px, int N);

// ---- notificacoes ----------------------------------------------------------
void ShowToast(const std::string& text, bool error = false);
void DrawToast(const ImGuiViewport* vp, float statusH);
