#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "core/types.h"

namespace CoreEngine {

    // ── Shadow Mapping ──────────────────────────────────────────────

    struct ShadowMap {
        GLuint fbo = 0;
        GLuint depthTexture = 0;
        GLuint depthRenderbuffer = 0;
        int width = 2048;
        int height = 2048;
        bool inited = false;
    };

    // Light direction in world space (normalized)
    void SetShadowLightDirection(Vector3 dir);
    Vector3 GetShadowLightDirection();
    glm::mat4 GetLightViewMatrix();
    glm::mat4 GetLightProjectionMatrix();
    glm::mat4 GetLightSpaceMatrix();

    // Shadow map initialization / rendering
    void InitShadowMap(int width = 2048, int height = 2048);
    void DrawShadowPass();          // Render scene to shadow map

    void CleanupShadowMap();        // Free shadow map FBO + texture

    // Get the shadow map for use in shaders
    GLuint GetShadowMapTexture();
    GLuint GetShadowMapFBO();
    int GetShadowMapWidth();
    int GetShadowMapHeight();

} // namespace CoreEngine
