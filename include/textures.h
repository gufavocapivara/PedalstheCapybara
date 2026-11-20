// textures.h - Carregamento e cache de texturas de pedais Lua.
//
// [FIX] Info agora expõe gl_id (necessário para o main.cpp montar ImTextureID).
// [FIX] Inclui o header do OpenGL correto por plataforma.
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gp {

    using TextureId = std::uint32_t;

    struct TextureSet {
        TextureId base = 0;
        TextureId knob = 0;
        TextureId knobShadow = 0;
        TextureId led = 0;
        TextureId footswitch = 0;
        TextureId label = 0;
        TextureId overlay = 0;
    };

    class TextureCache {
    public:
        TextureCache() = default;
        ~TextureCache();

        TextureCache(const TextureCache&) = delete;
        TextureCache& operator=(const TextureCache&) = delete;

        TextureId load(const std::filesystem::path& path);
        void release(TextureId id);

        // [FIX] Info agora inclui gl_id (GLuint cru) para o ImGui montar ImTextureID.
        struct Info { int w = 0, h = 0; unsigned gl_id = 0; std::string path; };
        Info info(TextureId id) const;

    private:
        struct Entry { unsigned gl = 0; int w = 0, h = 0; int refs = 1; std::string path; };
        std::unordered_map<std::string, TextureId> byPath_;
        std::unordered_map<TextureId, Entry> byId_;
        TextureId next_ = 1;
    };

    TextureCache& globalTextureCache();

}  // namespace gp