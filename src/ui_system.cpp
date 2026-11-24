#include "ui_shared.h"

void openUrl(const char* url) {
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
void openFolder(const fs::path& p) {
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
bool loadLogoIcon(std::vector<unsigned char>& px, int N) {
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

void buildWindowIcon(std::vector<unsigned char>& px, int N) {
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

