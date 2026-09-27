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
    glm::mat4 GetLightProjectionMatrixAspect(float aspect);
    glm::mat4 GetLightSpaceMatrix();

    // Per-frame shadow frame: everything the main pass needs for shadow
    // comparison and scale-aware shadow biasing. The shadow frustum is sized
    // to the scene each frame (dynamic near/far along the light direction,
    // aspect matching the square shadow map), so these values must come
    // from this function — the old fixed SHADOW_NEAR/SHADOW_FAR constants
    // no longer describe the frustum.
    struct ShadowFrameParams {
        glm::mat4 lightSpace;      // world -> shadow map (proj * view)
        glm::vec3 lightRight;      // world-space right of the light view
        glm::vec3 lightUp;         // world-space up of the light view
        glm::vec2 texelWorld;     // world units per shadow texel (light X, Y)
        float near = 1.0f;        // shadow frustum depth window (world units)
        float far  = 100.0f;
    };
    ShadowFrameParams GetShadowFrameParams();

    // Shadow map initialization / rendering
    void InitShadowMap(int width = 2048, int height = 2048);
    void DrawShadowPass();          // Render scene to shadow map

    void CleanupShadowMap();        // Free shadow map FBO + texture

    // Get the shadow map for use in shaders
    GLuint GetShadowMapTexture();
    GLuint GetShadowMapFBO();
    int GetShadowMapWidth();
    int GetShadowMapHeight();

    // Shadow frustum parameters (used by main shader for shadow comparison)
    float GetShadowNear();
    float GetShadowFar();

} // namespace CoreEngine
