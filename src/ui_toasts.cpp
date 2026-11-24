#include "ui_shared.h"

// --------------------------------------------------------------------- toasts
Toast g_toast;
std::vector<std::string> g_droppedPaths;

void ShowToast(const std::string& text, bool error) {
    g_toast.text = text;
    g_toast.error = error;
    g_toast.until = glfwGetTime() + (error ? 7.0 : 4.0);
}

void DrawToast(const ImGuiViewport* vp, float statusH) {
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

