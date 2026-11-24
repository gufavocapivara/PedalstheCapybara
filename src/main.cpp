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

using namespace gp;
namespace fs = std::filesystem;

// ------------------------------------------------------------------ helpers
static float g_scale = 1.0f;
static inline float S(float v) { return v * g_scale; }

static ImFont* fontReg = nullptr;
static ImFont* fontSemi = nullptr;
static ImFont* fontBold = nullptr;
static ImFont* fontSmall = nullptr;

namespace pal {
    const ImU32 bg       = IM_COL32(13, 15, 19, 255);
    const ImU32 panel    = IM_COL32(20, 23, 28, 255);
    const ImU32 panel2   = IM_COL32(26, 30, 37, 255);
    const ImU32 card     = IM_COL32(31, 36, 44, 255);
    const ImU32 border   = IM_COL32(44, 51, 62, 255);
    const ImU32 text     = IM_COL32(232, 235, 240, 255);
    const ImU32 dim      = IM_COL32(148, 157, 170, 255);
    const ImU32 accent   = IM_COL32(233, 161, 59, 255);
    const ImU32 accent2  = IM_COL32(78, 201, 176, 255);
    const ImU32 green    = IM_COL32(70, 200, 120, 255);
    const ImU32 red      = IM_COL32(224, 91, 76, 255);
    const ImU32 yellow   = IM_COL32(232, 197, 71, 255);
    const ImU32 shadow   = IM_COL32(0, 0, 0, 80);
}

static ImU32 WithAlpha(ImU32 c, int a) { return (c & 0x00FFFFFFu) | ((ImU32)a << 24); }

static void TextAt(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 col, const char* txt) {
    dl->AddText(f, f->FontSize, pos, col, txt);
}
static void TextAtShadow(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 col, const char* txt) {
    dl->AddText(f, f->FontSize, ImVec2(pos.x + S(1), pos.y + S(1)), IM_COL32(0, 0, 0, 170), txt);
    dl->AddText(f, f->FontSize, pos, col, txt);
}
static void TextCentered(ImDrawList* dl, ImFont* f, ImVec2 ctr, ImU32 col, const char* txt) {
    ImVec2 t = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, txt);
    dl->AddText(f, f->FontSize, ImVec2(ctr.x - t.x * 0.5f, ctr.y - t.y * 0.5f), col, txt);
}

static ImVec4 catColorVec(const std::string& c) {
    if (c == "Drive") return ImVec4(0.93f, 0.55f, 0.22f, 1);
    if (c == "Modulation") return ImVec4(0.68f, 0.45f, 0.95f, 1);
    if (c == "Time/Space") return ImVec4(0.30f, 0.66f, 0.98f, 1);
    if (c == "Dinâmica") return ImVec4(0.35f, 0.82f, 0.55f, 1);
    if (c == "Amps") return ImVec4(0.95f, 0.72f, 0.30f, 1);
    return ImVec4(0.56f, 0.62f, 0.72f, 1);
}

static bool ContainsNoCase(const std::string& hay, const char* needle) {
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
static ImU32 AccentFor(const Pedal& p) {
    if (p.hasColor) {
        const float mx = std::max(p.color[0], std::max(p.color[1], p.color[2]));
        if (mx >= 0.45f) {
            const float k = std::min(1.0f, 0.9f / mx);
            return ImGui::GetColorU32(ImVec4(p.color[0] * k, p.color[1] * k, p.color[2] * k, 1.0f));
        }
    }
    return ImGui::GetColorU32(catColorVec(p.category));
}

// -------------------------------------------------------------------- estilo
static void setupStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = S(10); s.ChildRounding = S(8); s.FrameRounding = S(6);
    s.PopupRounding = S(8); s.GrabRounding = S(6); s.ScrollbarRounding = S(8);
    s.TabRounding = S(6);
    s.WindowPadding = ImVec2(S(12), S(10));
    s.FramePadding = ImVec2(S(10), S(6));
    s.ItemSpacing = ImVec2(S(8), S(7));
    s.ItemInnerSpacing = ImVec2(S(6), S(5));
    s.ScrollbarSize = S(12);
    s.GrabMinSize = S(12);
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 0;
    s.PopupBorderSize = 1;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.SeparatorTextBorderSize = 1;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.078f, 0.090f, 0.110f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(0.102f, 0.118f, 0.145f, 1);
    c[ImGuiCol_PopupBg] = ImVec4(0.090f, 0.105f, 0.128f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.173f, 0.200f, 0.243f, 1);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(0.125f, 0.145f, 0.176f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.165f, 0.190f, 0.228f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.196f, 0.225f, 0.270f, 1);
    c[ImGuiCol_TitleBg] = ImVec4(0.078f, 0.090f, 0.110f, 1);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.102f, 0.118f, 0.145f, 1);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.055f, 0.065f, 0.080f, 1);
    c[ImGuiCol_Button] = ImVec4(0.145f, 0.168f, 0.204f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.196f, 0.225f, 0.270f, 1);
    c[ImGuiCol_ButtonActive] = ImVec4(0.235f, 0.268f, 0.318f, 1);
    c[ImGuiCol_Header] = ImVec4(0.145f, 0.168f, 0.204f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.196f, 0.225f, 0.270f, 1);
    c[ImGuiCol_HeaderActive] = ImVec4(0.235f, 0.268f, 0.318f, 1);
    c[ImGuiCol_Separator] = ImVec4(0.173f, 0.200f, 0.243f, 1);
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.914f, 0.631f, 0.231f, 1);
    c[ImGuiCol_Tab] = ImVec4(0.102f, 0.118f, 0.145f, 1);
    c[ImGuiCol_TabHovered] = ImVec4(0.196f, 0.225f, 0.270f, 1);
    c[ImGuiCol_TabSelected] = ImVec4(0.173f, 0.200f, 0.243f, 1);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0.914f, 0.631f, 0.231f, 1);
    c[ImGuiCol_Text] = ImVec4(0.910f, 0.922f, 0.941f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(0.580f, 0.616f, 0.667f, 1);
    c[ImGuiCol_CheckMark] = ImVec4(0.914f, 0.631f, 0.231f, 1);
    c[ImGuiCol_SliderGrab] = ImVec4(0.914f, 0.631f, 0.231f, 1);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.969f, 0.702f, 0.337f, 1);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.055f, 0.065f, 0.080f, 0.20f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.235f, 0.268f, 0.318f, 1);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.310f, 0.353f, 0.416f, 1);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.275f, 0.784f, 0.470f, 1);
    c[ImGuiCol_NavHighlight] = ImVec4(0.914f, 0.631f, 0.231f, 1);
}

static ImFont* tryFont(const char* path, float size) {
    if (!fs::exists(path)) return nullptr;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size, &cfg);
}

static void loadFonts(float scale) {
    const char* regular[] = { "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\tahoma.ttf", "C:\\Windows\\Fonts\\arial.ttf" };
    const char* semi[] = { "C:\\Windows\\Fonts\\seguisb.ttf", "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf" };
    const char* bold[] = { "C:\\Windows\\Fonts\\segoeuib.ttf", "C:\\Windows\\Fonts\\seguisb.ttf", "C:\\Windows\\Fonts\\segoeui.ttf" };

    for (const char* p : regular) if (!fontReg && (fontReg = tryFont(p, 16.0f * scale))) break;
    for (const char* p : semi) if (!fontSemi && (fontSemi = tryFont(p, 16.0f * scale))) break;
    for (const char* p : bold) if (!fontBold && (fontBold = tryFont(p, 19.0f * scale))) break;
    fontSmall = tryFont(regular[0], 12.5f * scale);

    if (!fontReg) fontReg = ImGui::GetIO().Fonts->AddFontDefault();
    if (!fontSemi) fontSemi = fontReg;
    if (!fontBold) fontBold = fontSemi;
    if (!fontSmall) fontSmall = fontReg;
    ImGui::GetIO().FontDefault = fontReg;
}

// -------------------------------------------------------------------- widgets
static void FormatParam(const Param& p, float v, char* out, size_t n) {
    if (!p.labels.empty()) {
        int idx = std::clamp((int)std::lround(v - p.minV), 0, (int)p.labels.size() - 1);
        std::snprintf(out, n, "%s", p.labels[idx].c_str());
        return;
    }
    const std::string& nm = p.name;
    if (nm.find("threshold") != std::string::npos || nm == "gate" || nm.find("range") != std::string::npos)
        std::snprintf(out, n, "%.1f dB", v);
    else if (p.maxV - p.minV > 20.0f)
        std::snprintf(out, n, "%.0f", v);
    else
        std::snprintf(out, n, "%.2f", v);
}

// Desenha o knob (arco + ponteiro). t = 0..1
static void DrawKnobVisual(ImDrawList* dl, ImVec2 ctr, float r, float t, ImU32 accent,
                           bool hovered, bool active) {
    const float a0 = gp::kPi * 0.75f, a1 = gp::kPi * 2.25f;
    const float a = a0 + std::clamp(t, 0.0f, 1.0f) * (a1 - a0);
    dl->AddCircleFilled(ImVec2(ctr.x, ctr.y + S(1)), r, IM_COL32(0, 0, 0, 70), 40);
    dl->AddCircleFilled(ctr, r, active ? IM_COL32(44, 50, 60, 255) : IM_COL32(36, 41, 49, 255), 40);
    dl->AddCircle(ctr, r, hovered ? WithAlpha(accent, 170) : pal::border, 40, S(1));
    dl->PathArcTo(ctr, r - S(3), a0, a1, 40);
    dl->PathStroke(IM_COL32(58, 65, 77, 255), 0, S(3.5f));
    if (t > 0.001f) {
        dl->PathArcTo(ctr, r - S(3), a0, a, 40);
        dl->PathStroke(accent, 0, S(3.5f));
    }
    const ImVec2 p0(ctr.x + std::cos(a) * r * 0.22f, ctr.y + std::sin(a) * r * 0.22f);
    const ImVec2 p1(ctr.x + std::cos(a) * r * 0.78f, ctr.y + std::sin(a) * r * 0.78f);
    dl->AddLine(p0, p1, hovered ? pal::text : IM_COL32(215, 220, 228, 255), S(2.5f));
}

// ------------------------------------------------------------------- texturas
static void DrawTexCover(ImDrawList* dl, TextureId id, ImVec2 a, ImVec2 b,
                         float rounding, ImU32 tint) {
    if (!id) return;
    const auto inf = globalTextureCache().info(id);
    if (!inf.valid()) return;
    ImVec2 uv0(0, 0), uv1(1, 1);
    const float dstAspect = (b.x - a.x) / std::max(1.0f, b.y - a.y);
    const float srcAspect = (float)inf.w / std::max(1, inf.h);
    if (srcAspect > dstAspect) {
        const float f = dstAspect / srcAspect;
        uv0.x = (1.0f - f) * 0.5f;
        uv1.x = (1.0f + f) * 0.5f;
    } else {
        const float f = srcAspect / dstAspect;
        uv0.y = (1.0f - f) * 0.5f;
        uv1.y = (1.0f + f) * 0.5f;
    }
    dl->AddImageRounded((ImTextureID)(intptr_t)inf.gl_id, a, b, uv0, uv1, tint, rounding);
}

static void DrawTexFit(ImDrawList* dl, TextureId id, ImVec2 a, ImVec2 b, ImU32 tint) {
    if (!id) return;
    const auto inf = globalTextureCache().info(id);
    if (!inf.valid()) return;
    const float sc = std::min((b.x - a.x) / std::max(1, inf.w), (b.y - a.y) / std::max(1, inf.h));
    const float w = inf.w * sc, h = inf.h * sc;
    const ImVec2 p(a.x + ((b.x - a.x) - w) * 0.5f, a.y + ((b.y - a.y) - h) * 0.5f);
    dl->AddImage((ImTextureID)(intptr_t)inf.gl_id, p, ImVec2(p.x + w, p.y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
}

static bool DrawKnobTexture(ImDrawList* dl, TextureId id, ImVec2 ctr, float r, float t, ImU32 tint) {
    if (!id) return false;
    const auto inf = globalTextureCache().info(id);
    if (!inf.valid()) return false;
    const float ang = (std::clamp(t, 0.0f, 1.0f) - 0.5f) * gp::kPi * 1.5f;
    const float c = std::cos(ang), s = std::sin(ang);
    auto rot = [&](float x, float y) {
        return ImVec2(ctr.x + x * c - y * s, ctr.y + x * s + y * c);
    };
    const ImVec2 p1 = rot(-r, -r), p2 = rot(r, -r), p3 = rot(r, r), p4 = rot(-r, r);
    dl->AddImageQuad((ImTextureID)(intptr_t)inf.gl_id, p1, p2, p3, p4,
                     ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), tint);
    return true;
}

static void Knob(Param& p, ImU32 accent, float dia, const TextureSet* tex = nullptr) {
    const float width = dia + S(18);
    const float labelH = S(18);
    ImGui::PushID(&p);
    ImGui::BeginGroup();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##k", ImVec2(width, dia + labelH));
    ImGuiIO& io = ImGui::GetIO();
    float v = p.get();
    if (ImGui::IsItemActive() && io.MouseDelta.y != 0.0f) {
        float speed = io.KeyShift ? 0.0008f : 0.006f;
        p.set(v - io.MouseDelta.y * (p.maxV - p.minV) * speed);
        v = p.get();
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
        p.set(v + io.MouseWheel * (p.maxV - p.minV) * 0.02f);
        v = p.get();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { p.set(p.defV); v = p.get(); }
    const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hovered) {
        char buf[64]; FormatParam(p, v, buf, sizeof(buf));
        ImGui::SetTooltip("%s: %s\n(double-click = default, Shift = fine adjustment, mouse wheel = adjust)", p.name.c_str(), buf);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 ctr(pos.x + width * 0.5f, pos.y + dia * 0.5f);
    float r = dia * 0.5f - S(1);
    float t = (p.maxV > p.minV) ? (v - p.minV) / (p.maxV - p.minV) : 0.0f;
    bool drawn = false;
    if (tex && tex->knobShadow)
        DrawKnobTexture(dl, tex->knobShadow, ImVec2(ctr.x, ctr.y + S(2)), r * 1.08f, t, IM_COL32(255, 255, 255, 120));
    if (tex && tex->knob) {
        drawn = DrawKnobTexture(dl, tex->knob, ctr, r, t,
                                hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(240, 240, 240, 255));
    }
    if (!drawn) DrawKnobVisual(dl, ctr, r, t, accent, hovered, ImGui::IsItemActive());

    char val[64];
    FormatParam(p, v, val, sizeof(val));
    const char* label = hovered || ImGui::IsItemActive() ? val : p.name.c_str();
    ImU32 lcol = (hovered || ImGui::IsItemActive()) ? accent : pal::dim;
    TextCentered(dl, fontSmall, ImVec2(ctr.x, pos.y + dia + S(8)), lcol, label);
    ImGui::EndGroup();
    ImGui::PopID();
}

static void ButtonSpin(Param& p, ImU32 accent, float width) {
    const float h = S(26) + S(18);
    ImGui::PushID(&p);
    ImGui::BeginGroup();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##b", ImVec2(width, h));
    ImGuiIO& io = ImGui::GetIO();
    const float v = p.get();
    bool hovered = ImGui::IsItemHovered();

    const int n = (int)p.labels.size();
    int idx = n > 0 ? std::clamp((int)std::lround(v - p.minV), 0, n - 1) : 0;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        if (n > 0) p.set(p.minV + (float)((idx + 1) % n));
        else if (p.maxV - p.minV <= 1.5f) p.set(v >= (p.minV + p.maxV) * 0.5f ? p.minV : p.maxV);
        else p.set(std::floor(v) + 1.0f);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (n > 0) p.set(p.minV + (float)((idx + n - 1) % n));
        else if (p.maxV - p.minV > 1.5f) p.set(std::floor(v) - 1.0f);
    }
    if (hovered && io.MouseWheel != 0.0f) {
        if (n > 0) p.set(p.minV + (float)std::clamp(idx + (io.MouseWheel > 0 ? 1 : -1), 0, n - 1));
        else p.set(v + io.MouseWheel);
    }

    char txt[64];
    bool on = v >= (p.minV + p.maxV) * 0.5f;
    if (n > 0) std::snprintf(txt, sizeof(txt), "%s", p.labels[idx].c_str());
    else if (p.maxV - p.minV <= 1.5f) std::snprintf(txt, sizeof(txt), "%s", on ? "ON" : "OFF");
    else std::snprintf(txt, sizeof(txt), "%d", (int)std::lround(v));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = (n > 0 || on) ? WithAlpha(accent, hovered ? 110 : 80) : (hovered ? pal::panel2 : pal::card);
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + S(26)), bg, S(6));
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + S(26)),
                (n > 0 || on) ? WithAlpha(accent, 200) : pal::border, S(6), 0, S(1));
    TextCentered(dl, fontSemi, ImVec2(pos.x + width * 0.5f, pos.y + S(13)), pal::text, txt);
    TextCentered(dl, fontSmall, ImVec2(pos.x + width * 0.5f, pos.y + S(26) + S(8)), pal::dim, p.name.c_str());
    if (hovered) {
        char val[64]; FormatParam(p, p.get(), val, sizeof(val));
        ImGui::SetTooltip("%s: %s\n(click = next, right-click = previous, mouse wheel = adjust)",
                          p.name.c_str(), val);
    }
    ImGui::EndGroup();
    ImGui::PopID();
}

static void DrawParam(Param& p, ImU32 accent, int widget, float knobDia, const TextureSet* tex = nullptr) {
    if (widget == gp::kButtonSpin || !p.labels.empty()) ButtonSpin(p, accent, knobDia + S(18));
    else Knob(p, accent, knobDia, tex);
}

// Medidor horizontal com pico.
static void MeterBar(const char* label, float lvl, float w, float h) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float labelH = S(12);
    const float barY = p.y + labelH;
    const float barH = h - labelH;
    TextAt(dl, fontSmall, ImVec2(p.x, p.y - S(1)), pal::dim, label);
    dl->AddRectFilled(ImVec2(p.x, barY), ImVec2(p.x + w, barY + barH), IM_COL32(10, 12, 15, 255), S(3));
    float fill = std::clamp(lvl, 0.0f, 1.0f);
    if (fill > 0.0f) {
        ImU32 col = fill > 0.95f ? pal::red : (fill > 0.80f ? pal::yellow : pal::green);
        dl->AddRectFilled(ImVec2(p.x, barY), ImVec2(p.x + w * fill, barY + barH), col, S(3));
    }
    for (int i = 1; i < 8; ++i) {
        float x = p.x + w * (i / 8.0f);
        dl->AddLine(ImVec2(x, barY), ImVec2(x, barY + barH), IM_COL32(0, 0, 0, 90), S(1));
    }
}

static bool MiniKnob(const char* label, float* value, float mn, float mx, ImU32 accent, float dia) {
    ImGui::PushID(label);
    const float width = dia + S(8);
    const float totalH = dia + S(16);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##mk", ImVec2(width, totalH));
    ImGuiIO& io = ImGui::GetIO();
    bool changed = false;
    if (ImGui::IsItemActive() && io.MouseDelta.y != 0.0f) {
        float speed = io.KeyShift ? 0.0015f : 0.006f;
        *value = std::clamp(*value - io.MouseDelta.y * (mx - mn) * speed, mn, mx);
        changed = true;
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
        *value = std::clamp(*value + io.MouseWheel * (mx - mn) * 0.03f, mn, mx);
        changed = true;
    }
    bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 ctr(pos.x + width * 0.5f, pos.y + dia * 0.5f);
    float t = (mx > mn) ? (*value - mn) / (mx - mn) : 0.0f;
    DrawKnobVisual(dl, ctr, dia * 0.5f - S(1), t, accent, hovered, ImGui::IsItemActive());
    TextCentered(dl, fontSmall, ImVec2(ctr.x, pos.y + dia + S(6)), hovered ? accent : pal::dim, label);
    if (hovered) ImGui::SetTooltip("%s: %.2f", label, *value);
    ImGui::PopID();
    return changed;
}

// --------------------------------------------------------------------- cards
static const float kSlotW = 26.0f;
static const float kKnobDia = 38.0f;

struct BoardOps {
    int remove = -1;
    int moveFrom = -1, moveTo = -1;
    int addAsset = -1, addAmp = -1, addAt = -1;
};

static int ParamColumns(const Pedal& p, int maxCols) {
    int n = (int)p.params.size();
    if (n <= 0) return 1;
    return std::min(n, maxCols);
}
static int ParamRows(const Pedal& p, int cols) {
    int n = (int)p.params.size();
    return (n + cols - 1) / std::max(1, cols);
}

struct CardSize { float w = 0, h = 0; };

static CardSize SizeFor(const Pedal& p) {
    const auto* lp = dynamic_cast<const LuaPedal*>(&p);
    const float pad = S(10);
    float w = (lp && lp->uiW > 0.0f) ? lp->uiW : (p.isAmp ? S(300) : S(196));
    w = std::clamp(w, S(180), S(620));
    const int cols = std::max(1, std::min(5, (int)((w - pad * 2.0f) / (kKnobDia + S(18)))));
    const int rows = ParamRows(p, cols);
    float h = S(104) + rows * S(64) + S(48);
    if (lp && lp->uiH > 0.0f) h = std::max(h, lp->uiH);
    h = std::clamp(h, S(170), S(700));
    return { w, h };
}

static bool PowerButton(const char* id, bool on, ImVec2 size, ImU32 accent, TextureId footTex) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = on ? (hovered ? IM_COL32(52, 170, 96, 255) : IM_COL32(40, 140, 80, 255))
                  : (hovered ? pal::panel2 : pal::card);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg, S(7));
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y),
                on ? IM_COL32(90, 220, 140, 255) : pal::border, S(7), 0, S(1));
    if (footTex) {
        DrawTexFit(dl, footTex, ImVec2(p.x + S(3), p.y + S(3)),
                   ImVec2(p.x + size.y - S(2), p.y + size.y - S(3)),
                   on ? IM_COL32(255, 255, 255, 255) : IM_COL32(190, 190, 190, 255));
        TextCentered(dl, fontSemi, ImVec2(p.x + size.x * 0.58f, p.y + size.y * 0.5f),
                     on ? IM_COL32(240, 255, 245, 255) : pal::dim, on ? "LIGADO" : "BYPASS");
    } else {
        ImVec2 led(p.x + S(14), p.y + size.y * 0.5f);
        dl->AddCircleFilled(led, S(4), on ? IM_COL32(180, 255, 200, 255) : IM_COL32(90, 96, 108, 255), 16);
        if (on) dl->AddCircleFilled(led, S(7), IM_COL32(120, 255, 170, 40), 16);
        TextCentered(dl, fontSemi, ImVec2(p.x + size.x * 0.5f, p.y + size.y * 0.5f),
                     on ? IM_COL32(240, 255, 245, 255) : pal::dim, on ? "LIGADO" : "BYPASS");
    }
    (void)accent;
    return clicked;
}

static void DropSlot(int pos, float w, float cardH, BoardOps& ops, bool last) {
    ImGui::PushID(3000 + pos);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##slot", ImVec2(w, cardH));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool dragging = ImGui::GetDragDropPayload() != nullptr;
    const float y = p0.y + cardH * 0.5f;
    ImU32 line = dragging ? pal::accent : pal::border;
    dl->AddLine(ImVec2(p0.x, y), ImVec2(p0.x + (last ? S(34) : w), y), line, S(2));
    if (!last) {
        dl->AddTriangleFilled(ImVec2(p0.x + w - S(2), y - S(4)),
                              ImVec2(p0.x + w - S(2), y + S(4)),
                              ImVec2(p0.x + w + S(3), y), line);
    }
    if (dragging) {
        dl->AddRectFilled(ImVec2(p0.x + w * 0.5f - S(2), p0.y + S(8)),
                          ImVec2(p0.x + w * 0.5f + S(2), p0.y + cardH - S(8)),
                          WithAlpha(pal::accent, 190), S(2));
    }
    if (last) {
        const char* hint = "drag a pedal\nfrom the library\nhere";
        TextAt(dl, fontSmall, ImVec2(p0.x + S(44), y - S(22)), pal::dim, hint);
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("PEDAL")) {
            ops.moveFrom = *(const int*)pl->Data; ops.moveTo = pos;
        }
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET")) {
            ops.addAsset = *(const int*)pl->Data; ops.addAt = pos;
        }
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("AMP")) {
            ops.addAmp = *(const int*)pl->Data; ops.addAt = pos;
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
}

static void PedalCard(Pedal& p, int pos, BoardOps& ops) {
    const CardSize sz = SizeFor(p);
    const float W = sz.w, H = sz.h;
    const ImU32 col = AccentFor(p);
    const bool on = p.enabled.load();
    const float pad = S(10);
    const auto* lp = dynamic_cast<const LuaPedal*>(&p);
    const TextureSet* tex = lp ? &lp->tex : nullptr;
    const float headerH = S(28);
    const float footerH = S(38);

    ImGui::PushID(&p);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + W, p0.y + H);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p0.x, p0.y + S(4)), ImVec2(p1.x, p1.y + S(4)), pal::shadow, S(12));
    dl->AddRectFilled(p0, p1, pal::card, S(12));

    const bool hasBase = tex && tex->base;
    if (hasBase) {
        DrawTexCover(dl, tex->base, p0, p1, S(12), IM_COL32(255, 255, 255, 255));
        dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 55), S(12));
    }
    dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + S(5)), col, S(12), ImDrawFlags_RoundCornersTop);
    dl->AddRect(p0, p1, on ? WithAlpha(col, 220) : pal::border, S(12), 0, S(1.5f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, S(8)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::SetCursorScreenPos(p0);
    ImGui::BeginChild("##card", ImVec2(W, H), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* cdl = ImGui::GetWindowDrawList();

    // ---- cabecalho (arrastar) ----
    ImVec2 hp = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##drag", ImVec2(W - pad * 2 - S(58), headerH));
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("PEDAL", &pos, sizeof(int));
        ImGui::Text("%s", p.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to reorder");

    float nameX = hp.x;
    if (p.isAmp) {
        ImVec2 bp(hp.x, hp.y + S(4));
        cdl->AddRectFilled(bp, ImVec2(bp.x + S(38), bp.y + S(17)), WithAlpha(col, 70), S(4));
        cdl->AddRect(bp, ImVec2(bp.x + S(38), bp.y + S(17)), WithAlpha(col, 220), S(4), 0, S(1));
        TextCentered(cdl, fontSmall, ImVec2(bp.x + S(19), bp.y + S(8.5f)), col, "AMP");
        nameX += S(46);
    }
    if (tex && tex->label) {
        DrawTexFit(cdl, tex->label, ImVec2(nameX, hp.y + S(1)),
                   ImVec2(hp.x + W - pad * 2 - S(58), hp.y + headerH - S(1)),
                   on ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, 220));
    } else {
        TextAtShadow(cdl, fontSemi, ImVec2(nameX, hp.y + S(4)), on ? pal::text : pal::dim, p.name.c_str());
    }

    // Botão fechar no canto direito; LED à esquerda dele.
    const float closeSz = S(18);
    const float rightEdge = p0.x + W - pad;
    const float headerTop = p0.y + S(8);
    const ImVec2 xp(rightEdge - closeSz, headerTop + (headerH - closeSz) * 0.5f);
    const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(xp);
    ImGui::InvisibleButton("##x", ImVec2(closeSz, closeSz));
    const bool xh = ImGui::IsItemHovered();
    const bool xClicked = ImGui::IsItemClicked();
    ImGui::SetCursorScreenPos(savedCursor);

    const ImVec2 xCtr(xp.x + closeSz * 0.5f, xp.y + closeSz * 0.5f);
    if (xh) cdl->AddCircleFilled(xCtr, closeSz * 0.58f, IM_COL32(120, 50, 50, 160), 16);
    const float xr = S(3.8f);
    const ImU32 xCol = xh ? pal::red : pal::dim;
    cdl->AddLine(ImVec2(xCtr.x - xr, xCtr.y - xr), ImVec2(xCtr.x + xr, xCtr.y + xr), xCol, S(1.7f));
    cdl->AddLine(ImVec2(xCtr.x - xr, xCtr.y + xr), ImVec2(xCtr.x + xr, xCtr.y - xr), xCol, S(1.7f));
    if (xClicked) ops.remove = pos;

    const ImVec2 ledCtr(xp.x - S(14), xCtr.y);
    if (tex && tex->led) {
        DrawTexFit(cdl, tex->led, ImVec2(ledCtr.x - S(9), ledCtr.y - S(9)),
                   ImVec2(ledCtr.x + S(9), ledCtr.y + S(9)),
                   on ? IM_COL32(255, 255, 255, 255) : IM_COL32(95, 100, 110, 255));
    } else {
        cdl->AddCircleFilled(ledCtr, S(5), on ? pal::green : IM_COL32(74, 80, 92, 255), 18);
        if (on) cdl->AddCircleFilled(ledCtr, S(9), IM_COL32(90, 230, 140, 45), 18);
    }

    // ---- knobs ----
    const float bodyH = std::max(S(20), H - headerH - footerH - S(20));
    ImGui::SetCursorPosY(S(8) + headerH);
    ImGui::BeginChild("##body", ImVec2(0, bodyH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    const int cols = std::max(1, std::min(5, (int)((W - pad * 2.0f) / (kKnobDia + S(18)))));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(2), S(6)));
    int i = 0;
    for (auto& prm : p.params) {
        if (i % cols != 0) ImGui::SameLine(0, S(2));
        int kind = (lp && i < (int)lp->widgets.size()) ? lp->widgets[i] : gp::kSlider;
        DrawParam(*prm, col, kind, kKnobDia, tex);
        ++i;
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    // ---- rodape ----
    ImGui::SetCursorPosY(H - footerH);
    if (PowerButton("##power", on, ImVec2(W - pad * 2, S(32)), col, tex ? tex->footswitch : 0))
        p.enabled = !on;

    ImGui::EndChild();

    // overlay/arte por cima do card inteiro (nao bloqueia cliques)
    if (tex && tex->overlay) {
        DrawTexCover(dl, tex->overlay, p0, p1, S(12), IM_COL32(255, 255, 255, 205));
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

// ------------------------------------------------------------------ assets UI
static bool AssetRow(int index, const char* name, const char* sub, ImU32 color,
                     bool error, bool selected, const char* payloadType) {
    ImGui::PushID(index);
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = S(42);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##row", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool dbl = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered || selected) {
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                          selected ? WithAlpha(pal::accent, 30) : pal::panel2, S(6));
    }
    ImVec2 ip(p.x + S(7), p.y + S(8));
    dl->AddRectFilled(ip, ImVec2(ip.x + S(26), ip.y + S(26)), error ? pal::red : color, S(5));
    char initial[2] = { (char)std::toupper((unsigned char)(name && *name ? *name : '?')), 0 };
    TextCentered(dl, fontSemi, ImVec2(ip.x + S(13), ip.y + S(13)), IM_COL32(20, 22, 26, 255), initial);
    TextAt(dl, fontReg, ImVec2(p.x + S(42), p.y + S(5)), pal::text, name);
    TextAt(dl, fontSmall, ImVec2(p.x + S(42), p.y + S(23)), error ? pal::red : pal::dim, sub ? sub : "");
    if (hovered) ImGui::SetTooltip("Double-click to add\nor drag to the signal chain");
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(payloadType, &index, sizeof(int));
        ImGui::Text("+ %s", name);
        ImGui::EndDragDropSource();
    }
    ImGui::PopID();
    return dbl;
}

// -------------------------------------------------------------------- header
static void openUrl(const char* url);
static void openFolder(const fs::path& p);

struct HeaderRefs {
    AudioIO* audio;
    Engine* engine;
    bool* wantDevices;
    bool* showEditor;
    bool* showPlugins;
    float meterIn, meterOut;
};

static void DrawHeader(const ImVec2& pos, const ImVec2& size, HeaderRefs& r) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##header", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), pal::panel);
    dl->AddLine(ImVec2(pos.x, pos.y + size.y - 1), ImVec2(pos.x + size.x, pos.y + size.y - 1), pal::border);

    // ---- logo (capivara) ----
    static const TextureId logoTex = globalTextureCache().load("Assets/icon.png");
    const float logo = S(42);
    ImVec2 lp(pos.x + S(16), pos.y + (size.y - logo) * 0.5f);

    if (logoTex) {
        auto inf = globalTextureCache().info(logoTex);
        float sc = logo / (float)std::max(inf.w, inf.h);
        float w = inf.w * sc, h = inf.h * sc;
        ImVec2 a(lp.x + (logo - w) * 0.5f, lp.y + (logo - h) * 0.5f);
        dl->AddImage((ImTextureID)(intptr_t)inf.gl_id, a, ImVec2(a.x + w, a.y + h));
    } else {
        dl->AddRectFilled(lp, ImVec2(lp.x + logo, lp.y + logo), pal::accent, S(11));
        ImVec2 lc(lp.x + logo * 0.5f, lp.y + logo * 0.5f);
        dl->AddCircleFilled(ImVec2(lc.x - logo * 0.17f, lc.y - logo * 0.24f), logo * 0.10f, IM_COL32(46, 34, 24, 255), 16);
        dl->AddCircleFilled(ImVec2(lc.x + logo * 0.17f, lc.y - logo * 0.24f), logo * 0.10f, IM_COL32(46, 34, 24, 255), 16);
        dl->AddCircleFilled(ImVec2(lc.x, lc.y + logo * 0.02f), logo * 0.29f, IM_COL32(46, 34, 24, 255), 28);
        dl->AddCircleFilled(ImVec2(lc.x, lc.y + logo * 0.17f), logo * 0.15f, IM_COL32(138, 106, 74, 255), 20);
        dl->AddCircleFilled(ImVec2(lc.x - logo * 0.11f, lc.y - logo * 0.03f), logo * 0.035f, pal::accent, 12);
        dl->AddCircleFilled(ImVec2(lc.x + logo * 0.11f, lc.y - logo * 0.03f), logo * 0.035f, pal::accent, 12);
        dl->AddCircleFilled(ImVec2(lc.x, lc.y + logo * 0.13f), logo * 0.045f, IM_COL32(46, 34, 24, 255), 12);
    }
    TextAtShadow(dl, fontBold, ImVec2(lp.x + logo + S(12), pos.y + S(12)), pal::text, "PEDALS THE CAPYBARA");
    TextAt(dl, fontSmall, ImVec2(lp.x + logo + S(13), pos.y + S(38)), pal::dim, "real-time guitar rig");

    // ---- lado direito ----
    float x = pos.x + size.x - S(16);
    const float cy = pos.y + size.y * 0.5f;

    {
        ImVec2 bsize(S(104), S(30));
        x -= bsize.x;
        ImGui::SetCursorScreenPos(ImVec2(x, cy - bsize.y * 0.5f));
        if (ImGui::Button("Donate", bsize)) openUrl("https://github.com/sponsors/gufavocapivara?frequency=one-time&sponsor=gufavocapivara");
        x -= S(8);
    }
    {
        ImVec2 bsize(S(86), S(30));
        x -= bsize.x;
        ImGui::SetCursorScreenPos(ImVec2(x, cy - bsize.y * 0.5f));
        if (ImGui::Button("Editor", bsize)) *r.showEditor = !*r.showEditor;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lua pedal/amp editor and .pctp exporter");
        x -= S(8);
    }
    {
        ImVec2 bsize(S(92), S(30));
        x -= bsize.x;
        ImGui::SetCursorScreenPos(ImVec2(x, cy - bsize.y * 0.5f));
        if (ImGui::Button("Plugins", bsize)) *r.showPlugins = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Import packages, create a .pp pack, see errors");
        x -= S(8);
    }
    {
        ImVec2 bsize(S(88), S(30));
        x -= bsize.x;
        ImGui::SetCursorScreenPos(ImVec2(x, cy - bsize.y * 0.5f));
        if (ImGui::Button("Audio", bsize)) *r.wantDevices = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Audio devices");
        x -= S(14);
    }
    {
        float m = r.engine->master.load();
        x -= S(52);
        ImGui::SetCursorScreenPos(ImVec2(x, cy - S(20)));
        if (MiniKnob("MASTER", &m, 0.0f, 1.5f, pal::accent, S(34))) r.engine->master.store(m);
        x -= S(4);
        float g = r.engine->inGain.load();
        x -= S(52);
        ImGui::SetCursorScreenPos(ImVec2(x, cy - S(20)));
        if (MiniKnob("GAIN", &g, 0.0f, 2.0f, pal::accent2, S(34))) r.engine->inGain.store(g);
        x -= S(14);
    }
    {
        x -= S(92);
        ImGui::SetCursorScreenPos(ImVec2(x, cy - S(14)));
        MeterBar("OUT", r.meterOut, S(92), S(28));
        x -= S(10);
        x -= S(92);
        ImGui::SetCursorScreenPos(ImVec2(x, cy - S(14)));
        MeterBar("IN", r.meterIn, S(92), S(28));
        x -= S(14);
    }
    {
        float w = S(146), h = S(30);
        x -= w;
        ImVec2 sp(x, cy - h * 0.5f);
        ImGui::SetCursorScreenPos(sp);
        ImGui::InvisibleButton("##status", ImVec2(w, h));
        if (ImGui::IsItemClicked()) *r.wantDevices = true;
        bool run = r.audio->running();
        ImU32 dot = run ? pal::green : pal::red;
        dl->AddRectFilled(sp, ImVec2(sp.x + w, sp.y + h),
                          run ? IM_COL32(28, 60, 44, 255) : IM_COL32(64, 30, 30, 255), S(15));
        dl->AddRect(sp, ImVec2(sp.x + w, sp.y + h), WithAlpha(dot, 120), S(15), 0, S(1));
        dl->AddCircleFilled(ImVec2(sp.x + S(14), sp.y + h * 0.5f), S(4.5f), dot, 16);
        char txt[96];
        if (run) {
            float ms = r.audio->sampleRate() ? (float)r.audio->bufferSize() / r.audio->sampleRate() * 1000.0f : 0.0f;
            std::snprintf(txt, sizeof(txt), "%u Hz  %.1f ms", r.audio->sampleRate(), ms);
        } else {
            std::snprintf(txt, sizeof(txt), "Audio stopped");
        }
        TextAt(dl, fontSmall, ImVec2(sp.x + S(26), sp.y + h * 0.5f - fontSmall->FontSize * 0.5f), pal::text, txt);
    }

    ImGui::End();
}

// ---------------------------------------------------------------------- main
static void openUrl(const char* url) {
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + std::string(url) + "\"";
    std::system(cmd.c_str());
#else
    std::string cmd = "xdg-open \"" + std::string(url) + "\" &";
    std::system(cmd.c_str());
#endif
}
static void openFolder(const fs::path& p) {
    std::error_code ec;
    auto abs = fs::absolute(p, ec);
    if (ec) return;
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", abs.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + abs.string() + "\"";
    std::system(cmd.c_str());
#else
    std::string cmd = "xdg-open \"" + abs.string() + "\" &";
    std::system(cmd.c_str());
#endif
}

// Le o logo.png e reduz para N x N (mantendo proporcao, fundo transparente).
static bool loadLogoIcon(std::vector<unsigned char>& px, int N) {
    int w = 0, h = 0, ch = 0;
    unsigned char* src = stbi_load("Assets/icon.png", &w, &h, &ch, 4);
    if (!src) return false;

    px.assign((size_t)N * N * 4, 0);
    const float sc = (float)N / (float)std::max(w, h);
    const float ox = (N - w * sc) * 0.5f, oy = (N - h * sc) * 0.5f;

    for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
        float sx0 = (x - ox) / sc, sx1 = (x + 1 - ox) / sc;
        float sy0 = (y - oy) / sc, sy1 = (y + 1 - oy) / sc;
        if (sx1 <= 0 || sy1 <= 0 || sx0 >= w || sy0 >= h) continue;

        int x0 = std::max(0, (int)std::floor(sx0));
        int y0 = std::max(0, (int)std::floor(sy0));
        int x1 = std::min(w, std::max(x0 + 1, (int)std::ceil(sx1)));
        int y1 = std::min(h, std::max(y0 + 1, (int)std::ceil(sy1)));

        float r = 0, g = 0, b = 0, a = 0; int cnt = 0;
        for (int yy = y0; yy < y1; ++yy) for (int xx = x0; xx < x1; ++xx) {
            const unsigned char* s = &src[((size_t)yy * w + xx) * 4];
            float al = s[3] / 255.0f;
            r += s[0] * al; g += s[1] * al; b += s[2] * al; a += al; ++cnt;
        }
        unsigned char* p = &px[((size_t)y * N + x) * 4];
        if (a > 0.0f && cnt > 0) {
            p[0] = (unsigned char)(r / a);
            p[1] = (unsigned char)(g / a);
            p[2] = (unsigned char)(b / a);
            p[3] = (unsigned char)(a / cnt * 255.0f);
        }
    }
    stbi_image_free(src);
    return true;
}

static void buildWindowIcon(std::vector<unsigned char>& px, int N) {
    if (loadLogoIcon(px, N)) return;

    px.assign((size_t)N * N * 4, 0);
    auto inRound = [&](float x, float y, float r) {
        float cx = std::clamp(x, r, (float)N - r), cy = std::clamp(y, r, (float)N - r);
        return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
    };
    auto inEllipse = [&](float x, float y, float cx, float cy, float rx, float ry) {
        float dx = (x - cx) / rx, dy = (y - cy) / ry;
        return dx * dx + dy * dy <= 1.0f;
    };
    for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
        float fx = x + 0.5f, fy = y + 0.5f;
        unsigned char* p = &px[((size_t)y * N + x) * 4];
        if (!inRound(fx, fy, N * 0.22f)) continue;
        p[0] = 0xE9; p[1] = 0xA1; p[2] = 0x3B; p[3] = 255;
        bool dark = inEllipse(fx, fy, N * 0.30f, N * 0.30f, N * 0.11f, N * 0.11f) ||
            inEllipse(fx, fy, N * 0.70f, N * 0.30f, N * 0.11f, N * 0.11f) ||
            inEllipse(fx, fy, N * 0.5f, N * 0.54f, N * 0.30f, N * 0.27f);
        if (dark) { p[0] = 0x2E; p[1] = 0x22; p[2] = 0x18; }
        if (inEllipse(fx, fy, N * 0.5f, N * 0.70f, N * 0.18f, N * 0.12f)) { p[0] = 0x8A; p[1] = 0x6A; p[2] = 0x4A; }
        if (inEllipse(fx, fy, N * 0.38f, N * 0.49f, N * 0.035f, N * 0.045f) ||
            inEllipse(fx, fy, N * 0.62f, N * 0.49f, N * 0.035f, N * 0.045f)) {
            p[0] = 0xE9; p[1] = 0xA1; p[2] = 0x3B;
        }
        if (inEllipse(fx, fy, N * 0.5f, N * 0.65f, N * 0.05f, N * 0.035f)) { p[0] = 0x2E; p[1] = 0x22; p[2] = 0x18; }
    }
}

// --------------------------------------------------------------------- toasts
struct Toast {
    std::string text;
    bool error = false;
    double until = 0.0;
};
static Toast g_toast;
static std::vector<std::string> g_droppedPaths;

static void ShowToast(const std::string& text, bool error = false) {
    g_toast.text = text;
    g_toast.error = error;
    g_toast.until = glfwGetTime() + (error ? 7.0 : 4.0);
}

static void DrawToast(const ImGuiViewport* vp, float statusH) {
    if (g_toast.text.empty() || glfwGetTime() > g_toast.until) return;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - S(16), vp->Pos.y + vp->Size.y - statusH - S(14)),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          g_toast.error ? ImVec4(0.30f, 0.10f, 0.10f, 0.96f) : ImVec4(0.08f, 0.14f, 0.11f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          g_toast.error ? ImVec4(0.85f, 0.30f, 0.25f, 1) : ImVec4(0.30f, 0.75f, 0.45f, 1));
    ImGui::Begin("##toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PushTextWrapPos(S(360));
    ImGui::TextWrapped("%s", g_toast.text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::End();
    ImGui::PopStyleColor(2);
}

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
