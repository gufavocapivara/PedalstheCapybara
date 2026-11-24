#include "ui_shared.h"

// ------------------------------------------------------------------ assets UI
bool AssetRow(int index, const char* name, const char* sub, ImU32 color,
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

