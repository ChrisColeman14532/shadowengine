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

glm::mat4 GetLightViewMatrix() {
    // Light looks down its direction
    auto dir = s_shadowLightDir;
    glm::vec3 lightPos(
        dir.x * SHADOW_PLANE_HALF + SHADOW_NEAR_PLANE,
        dir.y * SHADOW_PLANE_HALF + SHADOW_NEAR_PLANE,
        dir.z * SHADOW_PLANE_HALF + SHADOW_NEAR_PLANE
    );
    glm::vec3 target(0.0f, 0.0f, 0.0f);
    return glm::lookAt(lightPos, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 GetLightProjectionMatrix() {
    // Use an orthographic projection for directional light shadow map
    float half = SHADOW_PLANE_HALF;
    return glm::ortho(-half, half, -half, half, SHADOW_NEAR, SHADOW_FAR);
}

glm::mat4 GetLightSpaceMatrix() {
    return GetLightProjectionMatrix() * GetLightViewMatrix();
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

    // Render all scene objects (skip the camera visual — it must not
    // cast a shadow blob at the camera position)
    auto& scene = GetSceneObjects();
    for (auto& obj : scene) {
        if (obj.id == s_cameraObjectId) continue;
        auto& mesh = obj.mesh;
        if (!mesh || !mesh->VAO || mesh->indexCount == 0) continue;

        // Build model matrix
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(obj.position.x, obj.position.y, obj.position.z));
        model = glm::rotate(model, (float)obj.rotation.x, glm::vec3(1, 0, 0));
        model = glm::rotate(model, (float)obj.rotation.y, glm::vec3(0, 1, 0));
        model = glm::rotate(model, (float)obj.rotation.z, glm::vec3(0, 0, 1));
        model = glm::scale(model, glm::vec3(obj.scale.x, obj.scale.y, obj.scale.z));

        GLint modelLoc = glGetUniformLocation(s_shadowDepthProg, "uModel");
        if (modelLoc != -1) {
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        }

        glBindVertexArray(mesh->VAO);
        if (mesh->EBO) {
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->indexCount, GL_UNSIGNED_INT, 0);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)mesh->indexCount);
        }
        glBindVertexArray(0);
    }

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

} // namespace CoreEngine
