#pragma once

#include <GL/glew.h>
#include <memory>
#include <string>

namespace CoreEngine {

    // ── Textures ────────────────────────────────────────────────────
    // GPU textures are reference-counted: the GL texture is deleted when
    // the last TexturePtr referencing it is destroyed. Materials can
    // freely copy/share the same texture.

    struct Texture {
        GLuint id = 0;
        int width = 0;
        int height = 0;
        int channels = 0;
    };
    using TexturePtr = std::shared_ptr<Texture>;
    TexturePtr LoadTexture(const std::string& path);
    TexturePtr LoadTextureFromMemory(const unsigned char* data, int width, int height, int channels);
    void BindTexture(const TexturePtr& tex, GLuint unit);

} // namespace CoreEngine
