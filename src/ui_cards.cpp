#include "ui_shared.h"

int ParamColumns(const Pedal& p, int maxCols) {
    int n = (int)p.params.size();
    if (n <= 0) return 1;
    return std::min(n, maxCols);
}
int ParamRows(const Pedal& p, int cols) {
    int n = (int)p.params.size();
    return (n + cols - 1) / std::max(1, cols);
}

CardSize SizeFor(const Pedal& p) {
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

bool PowerButton(const char* id, bool on, ImVec2 size, ImU32 accent, TextureId footTex) {
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

void DropSlot(int pos, float w, float cardH, BoardOps& ops, bool last) {
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

void PedalCard(Pedal& p, int pos, BoardOps& ops) {
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

