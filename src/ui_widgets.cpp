#include "ui_shared.h"

// -------------------------------------------------------------------- widgets
void FormatParam(const Param& p, float v, char* out, size_t n) {
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
void DrawKnobVisual(ImDrawList* dl, ImVec2 ctr, float r, float t, ImU32 accent,
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
void DrawTexCover(ImDrawList* dl, TextureId id, ImVec2 a, ImVec2 b,
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

void DrawTexFit(ImDrawList* dl, TextureId id, ImVec2 a, ImVec2 b, ImU32 tint) {
    if (!id) return;
    const auto inf = globalTextureCache().info(id);
    if (!inf.valid()) return;
    const float sc = std::min((b.x - a.x) / std::max(1, inf.w), (b.y - a.y) / std::max(1, inf.h));
    const float w = inf.w * sc, h = inf.h * sc;
    const ImVec2 p(a.x + ((b.x - a.x) - w) * 0.5f, a.y + ((b.y - a.y) - h) * 0.5f);
    dl->AddImage((ImTextureID)(intptr_t)inf.gl_id, p, ImVec2(p.x + w, p.y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
}

bool DrawKnobTexture(ImDrawList* dl, TextureId id, ImVec2 ctr, float r, float t, ImU32 tint) {
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

void Knob(Param& p, ImU32 accent, float dia, const TextureSet* tex) {
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

void ButtonSpin(Param& p, ImU32 accent, float width) {
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

void DrawParam(Param& p, ImU32 accent, int widget, float knobDia, const TextureSet* tex) {
    if (widget == gp::kButtonSpin || !p.labels.empty()) ButtonSpin(p, accent, knobDia + S(18));
    else Knob(p, accent, knobDia, tex);
}

// Medidor horizontal com pico.
void MeterBar(const char* label, float lvl, float w, float h) {
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

bool MiniKnob(const char* label, float* value, float mn, float mx, ImU32 accent, float dia) {
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

