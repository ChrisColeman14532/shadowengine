#pragma once

// Internal shared state for the engine implementation.
//
// NOT part of the public API — include only from engine/src/*.cpp.
//
// The state lives in C++17 `inline` variables so that every translation
// unit in the engine library shares exactly one instance of each. This
// replaces the file-local `static` state that engine.cpp used when the
// whole implementation fit in a single file.

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cstdint>
#include <vector>

#include "core/mesh.h"
#include "core/scene.h"

namespace CoreEngine {

    // ── Window / renderer ──────────────────────────────────────────
    inline GLFWwindow* s_window     = nullptr;
    inline int         s_width      = 0;
    inline int         s_height     = 0;
    inline bool        s_engineInited = false;

    // ── Default material shader ────────────────────────────────────
    inline GLuint      s_shaderProg = 0;
    inline GLuint      s_vao        = 0;
    inline GLuint      s_vbo        = 0;

    // ── Orbit camera ───────────────────────────────────────────────
    inline Vector3 s_cameraPos    = {15, 12, 25};
    inline Vector3 s_cameraTarget = {0, 0, 0};
    inline Vector3 s_cameraOffset = {0, -1.5f, -5};

    // ── Scene ──────────────────────────────────────────────────────
    inline std::vector<SceneObject> s_sceneObjects;
    inline std::vector<MeshPtr>     s_primitiveMeshes;
    inline bool        s_primitivesBuilt   = false;
    inline uint32_t    s_nextSceneObjectId = 1;
    inline uint32_t    s_selectedObjectId  = 0;

    // ── Camera as a scene object ───────────────────────────────────
    inline uint32_t s_cameraObjectId = 0;

    // ── Skybox ─────────────────────────────────────────────────────
    inline GLuint s_skyboxVBO  = 0;
    inline GLuint s_skyboxVAO  = 0;
    inline GLuint s_skyboxProg = 0;
    inline bool   s_skyboxInited = false;

    // ── Shadow mapping ─────────────────────────────────────────────
    inline GLuint  s_shadowFBO       = 0;
    inline GLuint  s_shadowDepthTex  = 0;
    inline GLuint  s_shadowDepthRB   = 0;
    inline GLuint  s_shadowDepthProg = 0;
    inline bool    s_shadowInited    = false;
    inline int     s_shadowWidth     = 2048;
    inline int     s_shadowHeight    = 2048;
    inline Vector3 s_shadowLightDir  = {0.5f, 1.0f, 0.3f};  // Sun-like direction

    inline constexpr float SHADOW_NEAR         = 0.5f;
    inline constexpr float SHADOW_FAR          = 50.0f;
    inline constexpr float SHADOW_PLANE_HALF   = 15.0f;  // Half extent of shadow frustum
    inline constexpr float SHADOW_NEAR_PLANE   = 5.0f;   // Distance from light to near plane (so camera is behind light)

    // ── Cross-file helpers (internal) ──────────────────────────────
    // shader.cpp
    GLuint LoadShaderProgram(const char* vsFile, const char* fsFile);
    void   compileDefaultShader();
    // primitives.cpp
    void   buildPrimitiveVAOs();

} // namespace CoreEngine
