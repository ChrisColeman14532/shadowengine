// Shadow mapping: directional-light shadow map (FBO + depth texture),
// light-space matrices, and the depth pre-pass.

#include "core/engine.h"

#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine_internal.h"

namespace CoreEngine {

void SetShadowLightDirection(Vector3 dir) {
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len > 0.001f) {
        s_shadowLightDir = {dir.x / len, dir.y / len, dir.z / len};
    }
}

Vector3 GetShadowLightDirection() {
    return s_shadowLightDir;
}

static float ComputeFrustumHalfExtent() {
    // Compute the world-space AABB of all scene objects projected onto
    // the light's perpendicular plane (X/Z), then return half the larger
    // span.  Uses ComputeObjectWorldMatrix so children of moved groups
    // are correctly included.
    float minWorldX = 1e9f, maxWorldX = -1e9f;
    float minWorldZ = 1e9f, maxWorldZ = -1e9f;
    bool hasAny = false;

    // ECS query: every drawable object (MeshRenderer) with its computed
    // world matrix (WorldTransform). Groups have no MeshRenderer, so the
    // legacy isGroup skip is implicit.
    GetEcsWorld().ForEach<Ecs::WorldTransform, Ecs::MeshRenderer>(
        [&](Ecs::Entity e, Ecs::WorldTransform& wt, Ecs::MeshRenderer& mr) {
            if (e == s_cameraObjectId || !mr.mesh) return;
            hasAny = true;

            const glm::mat4& world = wt.localToWorld;
            // World-space scale (matrix column lengths) so children of
            // scaled/rotated groups contribute their REAL extent.
            const float hwX = glm::length(glm::vec3(world[0])) * mr.mesh->halfExtent.x;
            const float hwZ = glm::length(glm::vec3(world[2])) * mr.mesh->halfExtent.z;

            // Use world-space center so children of moved groups track correctly.
            const float cx = world[3].x;
            const float cz = world[3].z;

            if (cx - hwX < minWorldX) minWorldX = cx - hwX;
            if (cx + hwX > maxWorldX) maxWorldX = cx + hwX;
            if (cz - hwZ < minWorldZ) minWorldZ = cz - hwZ;
            if (cz + hwZ > maxWorldZ) maxWorldZ = cz + hwZ;
        });

    if (!hasAny) return 15.0f;

    float spanX = maxWorldX - minWorldX;
    float spanZ = maxWorldZ - minWorldZ;
    return fmaxf(spanX, spanZ) * 0.5f;
}

static glm::vec3 ComputeFrustumCenter();  // defined below

static glm::vec3 ShadowLightDirWorld() {
    return glm::normalize(glm::vec3((float)s_shadowLightDir.x,
                                     (float)s_shadowLightDir.y,
                                     (float)s_shadowLightDir.z));
}

// How far the scene extends along the light direction relative to the
// scene-center plane: `toward` = toward the light, `away` = away from it.
// Used to size the light's distance and the ortho depth window so the WHOLE
// scene is inside the shadow frustum (no clipping) and the near/far range
// stays as tight as possible (best depth precision).
static void ComputeShadowDepthSpan(float& toward, float& away) {
    toward = 0.0f;
    away = 0.0f;

    glm::vec3 dir = ShadowLightDirWorld();
    glm::vec3 center = ComputeFrustumCenter();
    float centerProj = glm::dot(center, dir);

    float minProj = 1e30f, maxProj = -1e30f;
    bool hasAny = false;
    GetEcsWorld().ForEach<Ecs::WorldTransform, Ecs::MeshRenderer>(
        [&](Ecs::Entity e, Ecs::WorldTransform& wt, Ecs::MeshRenderer& mr) {
            if (e == s_cameraObjectId || !mr.mesh) return;
            hasAny = true;

            // World matrix (hierarchy included) so children of moved/scaled
            // groups contribute their REAL world extent.
            const glm::mat4& world = wt.localToWorld;
            glm::vec3 c = world[3].xyz;
            // GLM 1.0+ swizzle proxies can't be passed to template functions
            // (glm::length etc. deduce the vec type), so materialize first.
            glm::vec3 col0 = world[0].xyz;
            glm::vec3 col1 = world[1].xyz;
            glm::vec3 col2 = world[2].xyz;
            glm::vec3 ws(glm::length(col0), glm::length(col1), glm::length(col2));

            float p = glm::dot(c, dir);
            float r = ws.x * mr.mesh->halfExtent.x * std::fabs(dir.x)
                    + ws.y * mr.mesh->halfExtent.y * std::fabs(dir.y)
                    + ws.z * mr.mesh->halfExtent.z * std::fabs(dir.z);
            minProj = fminf(minProj, p - r);
            maxProj = fmaxf(maxProj, p + r);
        });
    if (!hasAny) return;

    // dir points from the scene toward the light, so larger projection =
    // closer to the light.
    toward = fmaxf(0.0f, maxProj - centerProj);
    away   = fmaxf(0.0f, centerProj - minProj);
}

// Depth window of the shadow frustum, sized to the scene's extent along the
// light direction. The light sits `max(toward, away) + 1` from the center,
// so the scene spans [1, 1 + 2*maxSpan] in light-space depth and the window
// [0.5, 2*maxSpan + 2] always covers it.
static glm::vec2 ShadowDepthWindow() {
    float toward, away;
    ComputeShadowDepthSpan(toward, away);
    float maxSpan = fmaxf(toward, away);
    float nearP = 0.5f;
    float farP = fmaxf(2.0f * maxSpan + 2.0f, nearP + 1.0f);
    return glm::vec2(nearP, farP);
}

static glm::vec3 ComputeFrustumCenter() {
    // Compute the world-space AABB center of all scene objects (including
    // their extents).  Uses X/Z for the light's perpendicular plane and
    // the average Y for proper depth culling.
    float minX = 1e9f, maxX = -1e9f;
    float minY = 1e9f, maxY = -1e9f;
    float minZ = 1e9f, maxZ = -1e9f;
    bool hasAny = false;

    GetEcsWorld().ForEach<Ecs::WorldTransform, Ecs::MeshRenderer>(
        [&](Ecs::Entity e, Ecs::WorldTransform& wt, Ecs::MeshRenderer& mr) {
            if (e == s_cameraObjectId || !mr.mesh) return;
            hasAny = true;

            const glm::mat4& world = wt.localToWorld;
            // World-space scale (column lengths) — correct under scaled
            // parent groups.
            const float hwX = glm::length(glm::vec3(world[0])) * mr.mesh->halfExtent.x;
            const float hwY = glm::length(glm::vec3(world[1])) * mr.mesh->halfExtent.y;
            const float hwZ = glm::length(glm::vec3(world[2])) * mr.mesh->halfExtent.z;

            const float wX = world[3].x;
            const float wY = world[3].y;
            const float wZ = world[3].z;

            if (wX - hwX < minX) minX = wX - hwX;
            if (wX + hwX > maxX) maxX = wX + hwX;
            if (wY - hwY < minY) minY = wY - hwY;
            if (wY + hwY > maxY) maxY = wY + hwY;
            if (wZ - hwZ < minZ) minZ = wZ - hwZ;
            if (wZ + hwZ > maxZ) maxZ = wZ + hwZ;
        });

    if (!hasAny) return glm::vec3(0.0f);

    return glm::vec3(
        (minX + maxX) * 0.5f,
        (minY + maxY) * 0.5f,
        (minZ + maxZ) * 0.5f
    );
}

// ── Light-space matrices (dynamic frustum) ─────────────────────────
// The shadow frustum tightly encloses all scene objects so large models
// (e.g. Mixamo characters that span 0‑180 units) are still shadowed.

glm::mat4 GetLightViewMatrix() {
    glm::vec3 center = ComputeFrustumCenter();
    glm::vec3 dir = ShadowLightDirWorld();
    // Position the light far enough along its direction that the whole
    // scene (its full extent along the light direction) stays inside the
    // depth window — previously a fixed 200 units, which clipped large
    // scenes out of the shadow map.
    float toward, away;
    ComputeShadowDepthSpan(toward, away);
    float lightDist = fmaxf(toward, away) + 1.0f;
    glm::vec3 lightPos = center + dir * lightDist;
    // lookAt degenerates if the light direction is (near-)parallel to up
    glm::vec3 up = std::fabs(dir.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f)
                                            : glm::vec3(0.0f, 1.0f, 0.0f);
    // Camera looks from light toward scene center.
    return glm::lookAt(lightPos, center, up);
}

glm::mat4 GetLightProjectionMatrix() {
    float half = ComputeFrustumHalfExtent();
    glm::vec2 win = ShadowDepthWindow();
    return glm::ortho(-half, half, -half, half, win.x, win.y);
}

glm::mat4 GetLightProjectionMatrixAspect(float aspect) {
    float half = ComputeFrustumHalfExtent();
    glm::vec2 win = ShadowDepthWindow();
    return glm::ortho(-half * aspect, half * aspect, -half, half, win.x, win.y);
}

glm::mat4 GetLightSpaceMatrix() {
    // Match the SHADOW MAP's aspect (it renders into a square 2048x2048
    // target), NOT the screen aspect — stretching by the screen aspect made
    // X texels wider than Y texels in world space (stretched shadows +
    // asymmetric self-shadowing artifacts on flat planes).
    float aspect = (s_shadowHeight > 0)
        ? (float)s_shadowWidth / (float)s_shadowHeight : 1.0f;
    return GetLightProjectionMatrixAspect(aspect) * GetLightViewMatrix();
}

ShadowFrameParams GetShadowFrameParams() {
    ShadowFrameParams p;
    p.lightSpace = GetLightSpaceMatrix();

    // Light view basis (columns of the view matrix).
    glm::mat4 view = GetLightViewMatrix();
    p.lightRight = glm::normalize(glm::vec3(view[0].x, view[0].y, view[0].z));
    p.lightUp    = glm::normalize(glm::vec3(view[1].x, view[1].y, view[1].z));

    // World-space size of one shadow texel along the light view's X and Y.
    float half = ComputeFrustumHalfExtent();
    float aspect = (s_shadowHeight > 0)
        ? (float)s_shadowWidth / (float)s_shadowHeight : 1.0f;
    p.texelWorld.x = (s_shadowWidth  > 0) ? (2.0f * half * aspect) / (float)s_shadowWidth : 0.0f;
    p.texelWorld.y = (s_shadowHeight > 0) ? (2.0f * half) / (float)s_shadowHeight : 0.0f;

    glm::vec2 win = ShadowDepthWindow();
    p.near = win.x;
    p.far  = win.y;
    return p;
}

void InitShadowMap(int width, int height) {
    if (s_shadowInited) {
        // Recreate if dimensions changed
        if (s_shadowWidth != width || s_shadowHeight != height) {
            CleanupShadowMap();
        } else {
            return;
        }
    }

    s_shadowWidth = width;
    s_shadowHeight = height;

    // Create depth texture
    glGenTextures(1, &s_shadowDepthTex);
    glBindTexture(GL_TEXTURE_2D, s_shadowDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    
    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create FBO with depth attachment
    glGenFramebuffers(1, &s_shadowFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s_shadowDepthTex, 0);
    
    // Use GL_NONE - we only need depth, no color attachments
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[ShadowMap] FBO incomplete!\n");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Compile shadow depth shader
    s_shadowDepthProg = LoadShaderProgram("shadow_depth.vert", "shadow_depth.frag");

    s_shadowInited = true;
    printf("[ShadowMap] Initialized %dx%d shadow map\n", width, height);
}

void DrawShadowPass() {
    if (!s_shadowInited) return;

    // Save the caller's viewport so we don't leak the shadow-map
    // viewport into the main pass.
    GLint savedViewport[4];
    glGetIntegerv(GL_VIEWPORT, savedViewport);

    // Bind shadow FBO for depth rendering
    glBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
    glViewport(0, 0, s_shadowWidth, s_shadowHeight);
    glClear(GL_DEPTH_BUFFER_BIT);
    
    // Use shadow shader
    glUseProgram(s_shadowDepthProg);
    
    // Compute light space matrix
    glm::mat4 lightSpaceMat = GetLightSpaceMatrix();
    GLint lsmLoc = glGetUniformLocation(s_shadowDepthProg, "uLightSpaceMatrix");
    if (lsmLoc != -1) {
        glUniformMatrix4fv(lsmLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMat));
    }

    // Render every ECS drawable (skip the camera visual — it must not
    // cast a shadow blob at the camera position). World matrices come
    // from the ECS transform system (hierarchy included), so shadows
    // track the animated pose AND any user transform of the model root.
    static glm::mat4 bonePalette[MAX_SKIN_BONES];
    GetEcsWorld().ForEach<Ecs::WorldTransform, Ecs::MeshRenderer>(
        [&](Ecs::Entity e, Ecs::WorldTransform& wt, Ecs::MeshRenderer& mr) {
            if (e == s_cameraObjectId) return;
            auto& mesh = mr.mesh;
            if (!mesh || !mesh->VAO || mesh->indexCount == 0) return;

            const glm::mat4& model = wt.localToWorld;

            GLint modelLoc = glGetUniformLocation(s_shadowDepthProg, "uModel");
            if (modelLoc != -1) {
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
            }

            // Skinning: same bone palette as the main pass so shadows track
            // the animated pose (entity value == scene object id).
            int nBones = Animator::Get().GetBonePalette(e, bonePalette);
            GLint skinCountLoc = glGetUniformLocation(s_shadowDepthProg, "uSkinCount");
            if (skinCountLoc != -1) glUniform1i(skinCountLoc, nBones);
            if (nBones > 0) {
                GLint bonesLoc = glGetUniformLocation(s_shadowDepthProg, "uBoneMatrices");
                if (bonesLoc != -1) glUniformMatrix4fv(bonesLoc, nBones, GL_FALSE, glm::value_ptr(bonePalette[0]));
            }

            glBindVertexArray(mesh->VAO);
            if (mesh->EBO) {
                glDrawElements(GL_TRIANGLES, (GLsizei)mesh->indexCount, GL_UNSIGNED_INT, 0);
            } else {
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)mesh->indexCount);
            }
            glBindVertexArray(0);
        });

    // Restore default framebuffer and the caller's viewport
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
}

void CleanupShadowMap() {
    if (s_shadowDepthTex) {
        glDeleteTextures(1, &s_shadowDepthTex);
        s_shadowDepthTex = 0;
    }
    if (s_shadowDepthRB) {
        glDeleteRenderbuffers(1, &s_shadowDepthRB);
        s_shadowDepthRB = 0;
    }
    if (s_shadowFBO) {
        glDeleteFramebuffers(1, &s_shadowFBO);
        s_shadowFBO = 0;
    }
    if (s_shadowDepthProg) {
        glDeleteProgram(s_shadowDepthProg);
        s_shadowDepthProg = 0;
    }
    s_shadowInited = false;
}

GLuint GetShadowMapTexture() {
    return s_shadowDepthTex;
}

GLuint GetShadowMapFBO() {
    return s_shadowFBO;
}

int GetShadowMapWidth() {
    return s_shadowWidth;
}

int GetShadowMapHeight() {
    return s_shadowHeight;
}

float GetShadowNear() {
    // Dynamic — must match the frustum the shadow pass actually renders
    // with (see GetLightProjectionMatrix).
    return ShadowDepthWindow().x;
}

float GetShadowFar() {
    return ShadowDepthWindow().y;
}

} // namespace CoreEngine
