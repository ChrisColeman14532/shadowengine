#include "editor_render.h"
#include "editor_state.h"
#include "editor_camera.h"
#include "core/engine.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Editor {

    // Last rendered camera state, stored so TryPickAtMouse() can unproject
    // clicks. Picking happens on mouse-release (outside the render loop),
    // so the frame's matrices have to be remembered for it.
    static glm::mat4 g_lastView = glm::mat4(1.0f);
    static glm::mat4 g_lastProjection = glm::mat4(1.0f);
    static float g_lastViewport[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // x, y, w, h (fb pixels)
    static bool g_lastViewValid = false;

    void RenderScene3D(GLFWwindow* window, float dt) {
        // ECS: sync scene -> world and run the systems (transform
        // hierarchy) ONCE per frame, BEFORE any pass reads world
        // matrices (far plane, shadow pass, main pass, picking).
        CoreEngine::TickEcs(dt);

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
        CoreEngine::DrawSkybox(camPos, cameraTarget, aspect);

        // Apply this frame's orbit / rotate / WASD input (delta-time scaled)
        Camera::UpdateInput(window, dt);

        // Camera view matrix with rotation
        glm::vec3 camTarget(cameraTarget.x, cameraTarget.y, cameraTarget.z);
        glm::mat4 view = Camera::ComputeViewMatrix(camPos, camTarget);

        // Get projection matrix using actual viewport dimensions.
        // Extend the far plane so large models (e.g. cm-scale characters that
        // are hundreds of units tall) aren't clipped by the default 100-unit
        // far plane: far = furthest scene-object bound, with sane bounds.
        // Positions/bounds come from the FULL world matrix (parent chain
        // included): FBX parts are children of the model root, so their local
        // o.position alone misses the parent transform and could under- or
        // over-estimate where the model really is.
        // Start the far plane far enough to always include the ground grid
        // (±512 cells around the origin). The camera can be hundreds of units
        // from the origin, so the grid's far edge can be well beyond 100;
        // a too-small far plane would clip the entire grid.
        float farPlane = 1500.0f;
        float minDistToSurface = 1.0e30f;
        // ECS query: same math as the legacy per-object loop, but the
        // world matrices come from the transform system (TickEcs ran at
        // the top of this frame) instead of re-walking the parent chain
        // per object. Groups have no MeshRenderer, so they drop out of
        // the query (the legacy !o.mesh skip was the same effect).
        CoreEngine::GetEcsWorld().ForEach<CoreEngine::Ecs::WorldTransform, CoreEngine::Ecs::MeshRenderer>(
            [&](CoreEngine::Ecs::Entity e, CoreEngine::Ecs::WorldTransform& wt, CoreEngine::Ecs::MeshRenderer& mr) {
                if (e == CoreEngine::GetCameraObjectId() || !mr.mesh) return;
                const glm::mat4 wm = wt.localToWorld;
                const glm::vec3 worldPos = wm[3].xyz;
                // GLM 1.0+ swizzle proxies can't be passed to template functions,
                // so materialize the columns before calling glm::length.
                const glm::vec3 w0 = wm[0].xyz;
                const glm::vec3 w1 = wm[1].xyz;
                const glm::vec3 w2 = wm[2].xyz;
                const glm::vec3 worldScale(glm::length(w0),
                                           glm::length(w1),
                                           glm::length(w2));
                const float d = glm::distance(camPos, worldPos);
                // Bounding-sphere radius: world-scaled half-extents + world-scaled center offset
                const float radius = glm::length(glm::vec3(mr.mesh->halfExtent.x * worldScale.x,
                                                           mr.mesh->halfExtent.y * worldScale.y,
                                                           mr.mesh->halfExtent.z * worldScale.z))
                                   + glm::length(glm::vec3(mr.mesh->center.x * worldScale.x,
                                                          mr.mesh->center.y * worldScale.y,
                                                          mr.mesh->center.z * worldScale.z));
                farPlane = fmaxf(farPlane, d + radius + 10.0f);
                minDistToSurface = fminf(minDistToSurface, d - radius);
            });
        // The ground grid (y = 0) is infinite and follows the camera, so
        // when the camera is above it the nearest grid point is directly
        // below: distance = camPos.y. The adaptive near plane below must
        // not clip it.
        if (camPos.y > 0.0f)
            minDistToSurface = fminf(minDistToSurface, camPos.y);
        farPlane = fminf(farPlane, 100000.0f);  // cap so near/far ratio stays sane

        // Depth precision: a 24-bit depth buffer can't resolve a 0.1 vs 100000
        // near/far ratio, so push the near plane out to 1% of the distance to
        // the nearest visible surface (0.1 floor). Nothing visible can be
        // closer than that, so this clips nothing and keeps the ratio sane.
        float nearPlane = fmaxf(0.1f, fmaxf(0.0f, minDistToSurface) * 0.01f);
        glm::mat4 projection = CoreEngine::GetProjectionMatrix(60.0f, aspect, nearPlane, farPlane);

        // Remember the exact matrices + viewport used for this frame so
        // click picking can unproject against what was actually drawn.
        // (Must come after projection is computed.)
        g_lastView = view;
        g_lastProjection = projection;
        g_lastViewport[0] = (float)vpX;
        g_lastViewport[1] = (float)vpY;
        g_lastViewport[2] = (float)vpW;
        g_lastViewport[3] = (float)vpH;
        g_lastViewValid = true;

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

            // Shadow frame — light-space matrix plus the parameters the
            // shader needs for scale-aware shadow biasing (light view basis
            // and world size of one shadow texel). Computed from the same
            // scene data the shadow pass renders with.
            auto shadowFrame = CoreEngine::GetShadowFrameParams();
            GLint lsmLoc = glGetUniformLocation(prog, "uLightSpaceMatrix");
            if (lsmLoc != -1) {
                glUniformMatrix4fv(lsmLoc, 1, GL_FALSE, glm::value_ptr(shadowFrame.lightSpace));
            }

            // Light direction
            auto lightDir = CoreEngine::GetShadowLightDirection();
            GLint ldLoc = glGetUniformLocation(prog, "uLightDirection");
            if (ldLoc != -1) {
                glUniform3f(ldLoc, lightDir.x, lightDir.y, lightDir.z);
            }

            // Shadow near/far — must match the shadow map's projection frustum
            GLint snLoc = glGetUniformLocation(prog, "uShadowNear");
            GLint sfLoc = glGetUniformLocation(prog, "uShadowFar");
            if (snLoc != -1) glUniform1f(snLoc, shadowFrame.near);
            if (sfLoc != -1) glUniform1f(sfLoc, shadowFrame.far);

            // Scale-aware bias inputs (see the shadow-bias block in material.frag)
            GLint lrLoc = glGetUniformLocation(prog, "uLightRight");
            if (lrLoc != -1)
                glUniform3f(lrLoc, shadowFrame.lightRight.x, shadowFrame.lightRight.y, shadowFrame.lightRight.z);
            GLint luLoc = glGetUniformLocation(prog, "uLightUp");
            if (luLoc != -1)
                glUniform3f(luLoc, shadowFrame.lightUp.x, shadowFrame.lightUp.y, shadowFrame.lightUp.z);
            GLint twLoc = glGetUniformLocation(prog, "uShadowTexelWorld");
            if (twLoc != -1)
                glUniform2f(twLoc, shadowFrame.texelWorld.x, shadowFrame.texelWorld.y);

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

        // Draw the ground plane + grid FIRST (before scene objects).
        // The solid plane fades into the sky's horizon color; the grid
        // lines sit on top and dissolve into the same fog color.
        // spacing = 1.0: one unit cube between grid lines (Unity-style).
        CoreEngine::DrawGroundPlane(view, projection);
        CoreEngine::DrawGrid(1.0f, view, projection);

        // Main pass via ECS: draws every entity with
        // (WorldTransform, MeshRenderer) — same uniforms, texture binds
        // and bone palettes as the legacy per-object loop, but the
        // object list and world matrices come from ECS queries (world
        // synced at the top of this frame). Groups have no MeshRenderer,
        // so they drop out of the query; the camera object is skipped
        // inside.
        CoreEngine::RenderSceneMeshesEcs(prog);

        // Draw selected object bounds wireframe
        CoreEngine::DrawSelectedObjectBounds(view, projection);

        // Reset viewport for full-window ImGui rendering
        glViewport(0, 0, windowW, windowH);
    }

    void TryPickAtMouse(GLFWwindow* window, float mouseX, float mouseY) {
        if (!g_lastViewValid) return;  // no frame rendered yet

        // Map the window-space mouse position into framebuffer pixels
        // (DPI scaling can make window and framebuffer sizes differ).
        int winW = 0, winH = 0, fbW = 0, fbH = 0;
        glfwGetWindowSize(window, &winW, &winH);
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (winW <= 0 || winH <= 0 || fbW <= 0 || fbH <= 0) return;

        const float vpX = g_lastViewport[0], vpY = g_lastViewport[1];
        const float vpW = g_lastViewport[2], vpH = g_lastViewport[3];
        if (vpW <= 0.0f || vpH <= 0.0f) return;

        float px = mouseX * (float)fbW / (float)winW;
        float py = mouseY * (float)fbH / (float)winH;
        // Click outside the 3D viewport (on a UI panel) — ignore
        if (px < vpX || px >= vpX + vpW || py < vpY || py >= vpY + vpH) return;

        // Framebuffer coords have a bottom-left origin; NDC is [-1, 1]
        float yFromBottom = (float)fbH - py;
        float ndcX = 2.0f * (px - vpX) / vpW - 1.0f;
        float ndcY = 2.0f * (yFromBottom - vpY) / vpH - 1.0f;

        // Unproject the click into a world-space ray
        const glm::mat4 invVP = glm::inverse(g_lastProjection * g_lastView);
        // Perspective divide needs the w component, so stay in vec4 first.
        const glm::vec4 o4 = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        const glm::vec4 f4 = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        const glm::vec3 origin(o4.x / o4.w, o4.y / o4.w, o4.z / o4.w);
        const glm::vec3 farPoint(f4.x / f4.w, f4.y / f4.w, f4.z / f4.w);
        const glm::vec3 dir = glm::normalize(farPoint - origin);

        // Ray vs each object's world-space bounds: transform the ray into
        // the object's local space and slab-test it against the mesh AABB
        // (center ± halfExtent). Rank hits by world distance.
        uint32_t bestId = 0;
        float bestDist = 0.0f;
        // ECS query: world matrices from the transform system (synced
        // this frame by TickEcs). Entity value == scene object id, so
        // the selected id stays the same number the rest of the engine
        // uses.
        CoreEngine::GetEcsWorld().ForEach<CoreEngine::Ecs::WorldTransform, CoreEngine::Ecs::MeshRenderer>(
            [&](CoreEngine::Ecs::Entity e, CoreEngine::Ecs::WorldTransform& wt, CoreEngine::Ecs::MeshRenderer& mr) {
                if (e == CoreEngine::GetCameraObjectId()) return;
                auto& mesh = mr.mesh;
                if (!mesh || mesh->indexCount == 0) return;

                const glm::mat4 wm = wt.localToWorld;
                const glm::mat4 invW = glm::inverse(wm);
                const glm::vec3 lo = (invW * glm::vec4(origin, 1.0f)).xyz;
                const glm::vec3 rayDir = (invW * glm::vec4(dir, 0.0f)).xyz;
                const glm::vec3 ld = glm::normalize(rayDir);

                const glm::vec3 mn(mesh->center.x - mesh->halfExtent.x,
                                  mesh->center.y - mesh->halfExtent.y,
                                  mesh->center.z - mesh->halfExtent.z);
                const glm::vec3 mx(mesh->center.x + mesh->halfExtent.x,
                                  mesh->center.y + mesh->halfExtent.y,
                                  mesh->center.z + mesh->halfExtent.z);

                float tmin = 0.0f, tmax = 1.0e30f;
                bool hit = true;
                for (int i = 0; i < 3; ++i) {
                    if (std::fabs(ld[i]) < 1.0e-8f) {
                        if (lo[i] < mn[i] || lo[i] > mx[i]) { hit = false; break; }
                    } else {
                        float t1 = (mn[i] - lo[i]) / ld[i];
                        float t2 = (mx[i] - lo[i]) / ld[i];
                        if (t1 > t2) std::swap(t1, t2);
                        tmin = fmaxf(tmin, t1);
                        tmax = fminf(tmax, t2);
                        if (tmin > tmax) { hit = false; break; }
                    }
                }
                if (!hit) return;

                // tmin <= 0 means the origin is inside the box — count the hit
                // at t = 0 so the camera can "see through" a box it's in.
                const float t = tmin > 0.0f ? tmin : 0.0f;
                const glm::vec3 worldHit = (wm * glm::vec4(lo + ld * t, 1.0f)).xyz;
                const float dist = glm::distance(origin, worldHit);
                if (bestId == 0 || dist < bestDist) {
                    bestId = e;
                    bestDist = dist;
                }
            });

        // Select the nearest hit, or deselect on an empty viewport click
        CoreEngine::SelectObject(bestId);
    }

}  // namespace Editor
