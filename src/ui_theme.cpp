#include "ui_shared.h"

float g_scale = 1.0f;

ImFont* fontReg = nullptr;
ImFont* fontSemi = nullptr;
ImFont* fontBold = nullptr;
ImFont* fontSmall = nullptr;

// -------------------------------------------------------------------- estilo
void setupStyle() {
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

ImFont* tryFont(const char* path, float size) {
    if (!fs::exists(path)) return nullptr;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size, &cfg);
}

void loadFonts(float scale) {
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

