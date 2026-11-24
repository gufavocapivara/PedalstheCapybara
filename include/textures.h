// textures.h - Carregamento e cache de texturas de pedais/amps Lua.
#pragma once
#include <cstdint>
#include <filesystem>
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

        // Carrega do disco (PNG/JPEG). Caminhos Unicode funcionam no Windows
        // porque o arquivo e lido com std::ifstream e decodificado em memoria.
        TextureId load(const std::filesystem::path& path);

        // Carrega bytes ja em memoria (imagens dentro de .pctp/.pp ou assets
        // do editor). `key` identifica a textura no cache.
        TextureId loadFromMemory(const std::string& key, const void* data, std::size_t size);

        void release(TextureId id);

        // Libera todas as texturas. Deve ser chamado antes de destruir o
        // contexto OpenGL (o destrutor estatico rodaria depois do GLFW).
        void clear();

        struct Info {
            int w = 0, h = 0;
            unsigned gl_id = 0;
            std::string path;
            bool valid() const { return gl_id != 0; }
        };
        Info info(TextureId id) const;

        std::size_t size() const { return byId_.size(); }

    private:
        TextureId upload(const std::string& key, const unsigned char* data, int w, int h);
        struct Entry {
            unsigned gl = 0;
            int w = 0, h = 0;
            int refs = 1;
            std::string path;
        };
        std::unordered_map<std::string, TextureId> byPath_;
        std::unordered_map<TextureId, Entry> byId_;
        TextureId next_ = 1;
        bool cleared_ = false;
    };

    TextureCache& globalTextureCache();

}  // namespace gp
