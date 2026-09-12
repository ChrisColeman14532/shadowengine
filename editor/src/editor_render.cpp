#include "editor_render.h"
#include "editor_state.h"
#include "editor_camera.h"
#include "core/engine.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <string>
#include <vector>

namespace Editor {

    void RenderScene3D(GLFWwindow* window) {
        // Get window size for viewport
        int windowW = 1280, windowH = 720;
        glfwGetFramebufferSize(window, &windowW, &windowH);

        const float menuBarH = 28.0f;
        const float consoleH = 150.0f;
        const float leftPanelW = 280.0f;
        const float rightPanelW = 320.0f;

        // Only reserve console space when it's visible
        int panelBottomH = g_showStatusBar ? (int)consoleH : 0;

        int vpX = (int)leftPanelW;
        int vpY = (int)menuBarH;
        int vpW = windowW - (int)leftPanelW - (int)rightPanelW;
        int vpH = windowH - (int)menuBarH - panelBottomH;
        if (vpW < 1) vpW = windowW;
        if (vpH < 1) vpH = windowH;
        // Prevent zero/NaN aspect ratio
        float aspect = 1.333f; // 16:10 default
        if (vpW > 0 && vpH > 0) {
            aspect = (float)vpW / (float)vpH;
            if (aspect <= 0.0f) aspect = 1.333f;
        }

        CoreEngine::RenderBegin();

        // Set viewport for 3D rendering (center area only)
        glViewport(vpX, vpY, vpW, vpH);

        // Compute actual camera position (original position + orbit + pan).
        // The target is read BEFORE input updates so the view matrix below
        // uses the same values as the skybox (input takes effect next frame).
        glm::vec3 camPos = Camera::ComputeCameraPosition();
        auto cameraTarget = CoreEngine::GetCameraTarget();

        // Draw skybox first (background) using the ACTUAL camera position
        CoreEngine::DrawSkybox(camPos, aspect);

        auto& sceneObjs = CoreEngine::GetSceneObjects();

        // Apply this frame's orbit / rotate / WASD input
        Camera::UpdateInput(window);

        // Camera view matrix with rotation
        glm::vec3 camTarget(cameraTarget.x, cameraTarget.y, cameraTarget.z);
        glm::mat4 view = Camera::ComputeViewMatrix(camPos, camTarget);

        // Get projection matrix using actual viewport dimensions.
        // Extend the far plane so large models (e.g. cm-scale characters that
        // are hundreds of units tall) aren't clipped by the default 100-unit
        // far plane: far = furthest scene-object bound, with sane bounds.
        float farPlane = 100.0f;
        for (const auto& o : CoreEngine::GetSceneObjects()) {
            if (o.id == CoreEngine::GetCameraObjectId() || !o.mesh) continue;
            glm::vec3 objPos(o.position.x, o.position.y, o.position.z);
            const float d = glm::distance(camPos, objPos);
            // Bounding-sphere radius: scaled half-extents + local center offset
            const float radius = glm::length(glm::vec3(o.scale.x * o.mesh->halfExtent.x,
                                                       o.scale.y * o.mesh->halfExtent.y,
                                                       o.scale.z * o.mesh->halfExtent.z))
                               + glm::length(glm::vec3(o.scale.x * o.mesh->center.x,
                                                      o.scale.y * o.mesh->center.y,
                                                      o.scale.z * o.mesh->center.z));
            farPlane = fmaxf(farPlane, d + radius + 10.0f);
        }
        farPlane = fminf(farPlane, 100000.0f);  // cap so near/far ratio stays sane
        glm::mat4 projection = CoreEngine::GetProjectionMatrix(60.0f, aspect, 0.1f, farPlane);

        // ── Shadow Pass ──────────────────────────────────────────────
        if (g_showShadows) {
            CoreEngine::DrawShadowPass();
        }

        // ── Main Pass: Render with shadow mapping ──────────────────────
        GLuint prog = CoreEngine::GetShaderProgram();
        glUseProgram(prog);
        GLint viewLoc = glGetUniformLocation(prog, "uView");
        GLint projLoc = glGetUniformLocation(prog, "uProjection");
        if (viewLoc != -1) CoreEngine::SetUniformMat4(prog, "uView", view);
        if (projLoc != -1) CoreEngine::SetUniformMat4(prog, "uProjection", projection);

        // World-space camera position for per-pixel view direction (specular)
        GLint camPosLoc = glGetUniformLocation(prog, "uCameraPos");
        if (camPosLoc != -1) glUniform3f(camPosLoc, camPos.x, camPos.y, camPos.z);

        // Set shadow mapping uniforms (only when shadows are enabled)
        if (g_showShadows) {
            GLuint shadowTex = CoreEngine::GetShadowMapTexture();
            GLint shadowMapLoc = glGetUniformLocation(prog, "uShadowMap");
            GLint hasShadowLoc = glGetUniformLocation(prog, "uHasShadowMap");
            if (hasShadowLoc != -1) glUniform1i(hasShadowLoc, 1);
            if (shadowMapLoc != -1) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, shadowTex);
                glUniform1i(shadowMapLoc, 0);
            }

            // Light space matrix
            glm::mat4 lightSpaceMat = CoreEngine::GetLightSpaceMatrix();
            GLint lsmLoc = glGetUniformLocation(prog, "uLightSpaceMatrix");
            if (lsmLoc != -1) {
                glUniformMatrix4fv(lsmLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMat));
            }

            // Light direction
            auto lightDir = CoreEngine::GetShadowLightDirection();
            GLint ldLoc = glGetUniformLocation(prog, "uLightDirection");
            if (ldLoc != -1) {
                glUniform3f(ldLoc, lightDir.x, lightDir.y, lightDir.z);
            }

            // Shadow near/far
            GLint snLoc = glGetUniformLocation(prog, "uShadowNear");
            GLint sfLoc = glGetUniformLocation(prog, "uShadowFar");
            if (snLoc != -1) glUniform1f(snLoc, 0.5f);
            if (sfLoc != -1) glUniform1f(sfLoc, 50.0f);

            // Shadow map resolution (for PCF texel size)
            GLint shadowSizeLoc = glGetUniformLocation(prog, "uShadowMapSize");
            if (shadowSizeLoc != -1) {
                int size = CoreEngine::GetShadowMapWidth();
                glUniform1f(shadowSizeLoc, (float)size);
            }
        } else {
            // Shadows disabled — tell shader to skip shadow calc
            GLint hasShadowLoc = glGetUniformLocation(prog, "uHasShadowMap");
            if (hasShadowLoc != -1) glUniform1i(hasShadowLoc, 0);
        }

        // Draw grid on the ground FIRST (before scene objects)
        CoreEngine::DrawGrid(40, 1.0f, 20.0f, view, projection);

        for (auto& obj : sceneObjs) {
            // Skip camera object — it's placed at the camera position,
            // so rendering it would put the camera inside the cube.
            if (obj.id == CoreEngine::GetCameraObjectId()) continue;
            if (obj.isGroup) continue;  // Transform-only node (model root)
            auto& mesh = obj.mesh;
            if (!mesh || !mesh->VAO || mesh->indexCount == 0) {
                // One-time diagnostic: object exists in the scene but has no
                // renderable geometry (this is why it's invisible).
                static std::vector<uint32_t> warnedEmptyIds;
                bool alreadyWarned = false;
                for (uint32_t w : warnedEmptyIds) if (w == obj.id) { alreadyWarned = true; break; }
                if (!alreadyWarned) {
                    warnedEmptyIds.push_back(obj.id);
                    ConsoleLog(std::string("WARNING: '") + obj.name + "' has empty mesh " +
                        "(VAO=" + std::to_string(mesh ? mesh->VAO : 0) +
                        ", indices=" + std::to_string(mesh ? mesh->indexCount : 0) +
                        ") — not rendered");
                }
                continue;
            }

            glUseProgram(prog);

            // World matrix = parent chain * local TRS. FBX parts are
            // children of the model root node, so transforming the root
            // moves/rotates/scales every part with it.
            glm::mat4 model = CoreEngine::ComputeObjectWorldMatrix(obj.id);

            CoreEngine::SetUniformMat4(prog, "uModel", model);
            CoreEngine::SetUniformVec3(prog, "uBaseColor", obj.material.baseColor);

            // Skinning: upload this object's bone palette (J matrices from
            // the Animator). uSkinCount = 0 leaves static meshes untouched.
            static glm::mat4 bonePalette[CoreEngine::MAX_SKIN_BONES];
            int nBones = CoreEngine::Animator::Get().GetBonePalette(obj.id, bonePalette);
            GLint skinCountLoc = glGetUniformLocation(prog, "uSkinCount");
            if (skinCountLoc != -1) glUniform1i(skinCountLoc, nBones);
            if (nBones > 0) {
                GLint bonesLoc = glGetUniformLocation(prog, "uBoneMatrices");
                if (bonesLoc != -1) glUniformMatrix4fv(bonesLoc, nBones, GL_FALSE, glm::value_ptr(bonePalette[0]));
            }

            // Pass material properties
            GLint metallicLoc = glGetUniformLocation(prog, "uMetallic");
            if (metallicLoc != -1) glUniform1f(metallicLoc, obj.material.metallic);
            GLint roughnessLoc = glGetUniformLocation(prog, "uRoughness");
            if (roughnessLoc != -1) glUniform1f(roughnessLoc, obj.material.roughness);
            GLint aoLoc = glGetUniformLocation(prog, "uAO");
            if (aoLoc != -1) glUniform1f(aoLoc, obj.material.ao);
            GLint emissiveLoc = glGetUniformLocation(prog, "uEmissiveColor");
            if (emissiveLoc != -1) CoreEngine::SetUniformVec3(prog, "uEmissiveColor", obj.material.emissiveColor);

            // Textures
            GLint hasDiffuseLoc = glGetUniformLocation(prog, "uHasDiffuse");
            GLint hasNormalMapLoc = glGetUniformLocation(prog, "uHasNormalMap");
            GLint diffuseLoc = glGetUniformLocation(prog, "uDiffuseTex");
            GLint normalMapLoc = glGetUniformLocation(prog, "uNormalTex");

            if (obj.material.diffuseTexture) {
                if (hasDiffuseLoc != -1) glUniform1i(hasDiffuseLoc, 1);
                if (diffuseLoc != -1) {
                    glActiveTexture(GL_TEXTURE1);
                    CoreEngine::BindTexture(obj.material.diffuseTexture, 1);
                    glUniform1i(diffuseLoc, 1);
                }
            } else {
                if (hasDiffuseLoc != -1) glUniform1i(hasDiffuseLoc, 0);
            }

            // Normal map texture (separate from diffuse)
            if (obj.material.normalTexture) {
                if (hasNormalMapLoc != -1) glUniform1i(hasNormalMapLoc, 1);
                if (normalMapLoc != -1) {
                    glActiveTexture(GL_TEXTURE2);
                    CoreEngine::BindTexture(obj.material.normalTexture, 2);
                    glUniform1i(normalMapLoc, 2);
                }
            } else {
                if (hasNormalMapLoc != -1) glUniform1i(hasNormalMapLoc, 0);
            }

            glBindVertexArray(mesh->VAO);
            if (mesh->EBO) {
                glDrawElements(GL_TRIANGLES, (GLsizei)mesh->indexCount, GL_UNSIGNED_INT, 0);
            } else {
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)mesh->indexCount);
            }
            glBindVertexArray(0);
        }

        // Draw selected object bounds wireframe
        CoreEngine::DrawSelectedObjectBounds(view, projection);

        // Reset viewport for full-window ImGui rendering
        glViewport(0, 0, windowW, windowH);
    }

}  // namespace Editor
