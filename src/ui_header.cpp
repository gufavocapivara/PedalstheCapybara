#include "ui_shared.h"

void DrawHeader(const ImVec2& pos, const ImVec2& size, HeaderRefs& r) {
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

