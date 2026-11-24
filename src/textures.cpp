// textures.cpp - Implementacao do cache de texturas (OpenGL).
//
// [FIX] Le os arquivos em memoria: caminhos Unicode no Windows funcionam.
// [FIX] Suporta texturas dentro de .pctp/.pp sem extrair para o disco.
// [FIX] clear() e chamado antes do contexto OpenGL ser destruido.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include "textures.h"

#include <fstream>
#include <vector>

// ---- OpenGL: glad (opcional) ou fallback ----
#if defined(GP_USE_GLAD)
#  include <glad/glad.h>
#else
#  if defined(_WIN32)
#    include <windows.h>
#    include <GL/gl.h>
#  elif defined(__APPLE__)
#    include <OpenGL/gl3.h>
#  else
#    include <GL/gl.h>
#  endif
#  define GLFW_INCLUDE_NONE
#  include <GLFW/glfw3.h>
#endif

#ifndef GL_CLAMP_TO_EDGE
#  define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace gp {

    static TextureCache g_cache;
    TextureCache& globalTextureCache() { return g_cache; }

    TextureCache::~TextureCache() {
        clear();
    }

    TextureId TextureCache::upload(const std::string& key, const unsigned char* data, int w, int h) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        if (!tex) return 0;
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        const TextureId id = next_++;
        byPath_[key] = id;
        byId_[id] = Entry{ (unsigned)tex, w, h, 1, key };
        cleared_ = false;
        return id;
    }

    TextureId TextureCache::loadFromMemory(const std::string& rawKey, const void* data, std::size_t size) {
        if (!data || size == 0) return 0;
        std::string key = rawKey;
        if (key.empty()) key = "memory:" + std::to_string(next_);

        auto it = byPath_.find(key);
        if (it != byPath_.end()) {
            byId_[it->second].refs++;
            return it->second;
        }

        int w = 0, h = 0, channels = 0;
        unsigned char* pixels = stbi_load_from_memory(
            static_cast<const unsigned char*>(data), (int)size, &w, &h, &channels, 4);
        if (!pixels || w <= 0 || h <= 0) {
            if (pixels) stbi_image_free(pixels);
            return 0;
        }
        const TextureId id = upload(key, pixels, w, h);
        stbi_image_free(pixels);
        return id;
    }

    TextureId TextureCache::load(const std::filesystem::path& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) return 0;

        std::string key = std::filesystem::absolute(path, ec).string();
        if (ec) key = path.string();
        auto it = byPath_.find(key);
        if (it != byPath_.end()) {
            byId_[it->second].refs++;
            return it->second;
        }

        std::error_code sizeEc;
        const auto size = std::filesystem::file_size(path, sizeEc);
        if (sizeEc || size == 0 || size > 64ull * 1024ull * 1024ull) return 0;

        std::ifstream in(path, std::ios::binary);
        if (!in) return 0;
        std::vector<unsigned char> bytes((size_t)size);
        in.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)size);
        if (!in) return 0;
        return loadFromMemory(key, bytes.data(), bytes.size());
    }

    void TextureCache::release(TextureId id) {
        auto it = byId_.find(id);
        if (it == byId_.end()) return;
        if (--it->second.refs <= 0) {
            if (it->second.gl) glDeleteTextures(1, &it->second.gl);
            byPath_.erase(it->second.path);
            byId_.erase(it);
        }
    }

    void TextureCache::clear() {
        for (auto& [id, e] : byId_)
            if (e.gl) glDeleteTextures(1, &e.gl);
        byId_.clear();
        byPath_.clear();
        next_ = 1;
        cleared_ = true;
    }

    TextureCache::Info TextureCache::info(TextureId id) const {
        auto it = byId_.find(id);
        if (it == byId_.end()) return {};
        return { it->second.w, it->second.h, it->second.gl, it->second.path };
    }

}  // namespace gp
