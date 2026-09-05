// Textures: reference-counted GPU textures, file/memory loading
// (via stb_image), and binding helpers.

#include "core/engine.h"

#include <cstdio>

#include "stb_image_loader.h"
#include "engine_internal.h"

namespace {
    // Shared ownership: the GL texture is deleted when the last
    // TexturePtr referencing it is destroyed (main thread, context current).
    CoreEngine::TexturePtr MakeTexture() {
        return CoreEngine::TexturePtr(new CoreEngine::Texture(), [](CoreEngine::Texture* t) {
            if (t->id) glDeleteTextures(1, &t->id);
            delete t;
        });
    }

    // Upload raw pixel data to the GPU. Returns nullptr on failure.
    CoreEngine::TexturePtr UploadTexture(const unsigned char* data, int width, int height, int channels) {
        CoreEngine::TexturePtr tex = MakeTexture();
        tex->width = width;
        tex->height = height;
        tex->channels = channels;

        glGenTextures(1, &tex->id);
        if (tex->id == 0) return nullptr;
        glBindTexture(GL_TEXTURE_2D, tex->id);

        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Determine internal format and source format to match channel count
        GLenum internalFormat = GL_RGBA;
        GLenum format = GL_RGBA;
        if (channels == 3) { internalFormat = GL_RGB; format = GL_RGB; }
        else if (channels == 1) { internalFormat = GL_R; format = GL_RED; }

        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    }
}

namespace CoreEngine {

TexturePtr LoadTexture(const std::string& path) {
    // stb_image loads with origin at top-left for most formats.
    // OpenGL expects bottom-left origin, so flip during load.
    // The flip flag is global stb state, so restore the default afterwards.
    stbi_set_flip_vertically_on_load(true);
    int w = 0, h = 0, c = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 4);
    stbi_set_flip_vertically_on_load(false);
    if (!data) {
        fprintf(stderr, "[Texture] Failed to load: %s (stb error: %s)\n",
            path.c_str(), stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        return nullptr;
    }
    TexturePtr tex = UploadTexture(data, w, h, 4);
    stbi_image_free(data);
    return tex;
}

TexturePtr LoadTextureFromMemory(const unsigned char* data, int width, int height, int channels) {
    return UploadTexture(data, width, height, channels);
}

void BindTexture(const TexturePtr& tex, GLuint unit) {
    if (!tex || tex->id == 0) return;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex->id);
}

} // namespace CoreEngine
