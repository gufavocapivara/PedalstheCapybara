// textures.cpp - Implementação do cache de texturas (OpenGL).
//
// [FIX] Usa glad consistentemente (GP_USE_GLAD). Inclui o header ANTES do GLFW
//       para evitar conflito, e não inclui <GL/gl.h> quando glad está ativo.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include "textures.h"

// ---- OpenGL: glad (recomendado) ou fallback ----
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

namespace gp {

    static TextureCache g_cache;
    TextureCache& globalTextureCache() { return g_cache; }

    TextureCache::~TextureCache() {
        for (auto& [id, e] : byId_)
            if (e.gl) glDeleteTextures(1, &e.gl);
    }

    TextureId TextureCache::load(const std::filesystem::path& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) return 0;

        std::string key = std::filesystem::absolute(path, ec).string();
        if (ec) return 0;

        auto it = byPath_.find(key);
        if (it != byPath_.end()) {
            byId_[it->second].refs++;
            return it->second;
        }

        int w = 0, h = 0, ch = 0;
        unsigned char* data = stbi_load(key.c_str(), &w, &h, &ch, 4);
        if (!data) return 0;

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(data);

        TextureId id = next_++;
        byPath_[key] = id;
        byId_[id] = Entry{ (unsigned)tex, w, h, 1, key };
        return id;
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

    TextureCache::Info TextureCache::info(TextureId id) const {
        auto it = byId_.find(id);
        if (it == byId_.end()) return {};
        return { it->second.w, it->second.h, it->second.gl, it->second.path };
    }

}  // namespace gp