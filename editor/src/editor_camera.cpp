#include "editor_camera.h"
#include "editor_state.h"
#include "core/engine.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <string>

namespace Editor {
namespace Camera {

    // Orbit camera state
    static double g_prevMouseX = 0;
    static double g_prevMouseY = 0;
    static bool g_isOrbiting = false;
    static glm::vec3 g_cameraPanOffset = glm::vec3(0, 0, 0);

    // Camera rotation state (yaw + pitch) - controlled by right-click drag
    static float g_cameraYaw = 0.0f;
    static float g_cameraPitch = 0.0f;
    static bool g_isRotating = false;

    void InstallInputCallbacks(GLFWwindow* window) {
        glfwSetCharCallback(window, [](GLFWwindow* w, unsigned int codepoint) {
            ImGui_ImplGlfw_CharCallback(w, codepoint);
        });

        glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int scancode, int action, int mods) {
            // Feed keys to ImGui first
            ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);

            // Ctrl+C: copy console text to clipboard
            if (action == GLFW_PRESS && key == GLFW_KEY_C && (mods & GLFW_MOD_CONTROL)) {
                if (!g_consoleText.empty()) {
                    ImGui::SetClipboardText(g_consoleText.c_str());
                    ConsoleLog("Console copied to clipboard");
                }
            }

            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                if (!ImGui::GetIO().WantCaptureKeyboard) {
                    float speed = 0.05f;
                    auto& scene = CoreEngine::GetSceneObjects();
                    uint32_t selectedId = CoreEngine::GetSelectedObjectId();
                    for (auto& obj : scene) {
                        if (obj.id == selectedId && obj.name.find("cube") != std::string::npos) {
                            if (key == GLFW_KEY_A) obj.rotation.y += speed;
                            if (key == GLFW_KEY_D) obj.rotation.y -= speed;
                            if (key == GLFW_KEY_S) obj.rotation.x += speed;
                            if (key == GLFW_KEY_W) obj.rotation.x -= speed;
                        }
                    }
                }
                if (key == GLFW_KEY_L) {
                    g_triggerFileDialog = true;
                }
                if (key == GLFW_KEY_HOME) {
                    CoreEngine::ResetCamera();
                    ConsoleLog("Camera reset to default position");
                }
            }
        });

        glfwGetCursorPos(window, &g_prevMouseX, &g_prevMouseY);
        glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int btn, int action, int mods) {
            ImGui_ImplGlfw_MouseButtonCallback(w, btn, action, mods);
            if (action == GLFW_PRESS) {
                double mx, my;
                glfwGetCursorPos(w, &mx, &my);
                g_prevMouseX = mx;
                g_prevMouseY = my;
                if (btn == GLFW_MOUSE_BUTTON_LEFT) g_isOrbiting = true;
                if (btn == GLFW_MOUSE_BUTTON_RIGHT) g_isRotating = true;
            } else {
                if (btn == GLFW_MOUSE_BUTTON_LEFT) g_isOrbiting = false;
                if (btn == GLFW_MOUSE_BUTTON_RIGHT) g_isRotating = false;
            }
        });

        glfwSetScrollCallback(window, [](GLFWwindow* w, double dx, double dy) {
            ImGui_ImplGlfw_ScrollCallback(w, dx, dy);
            // Don't zoom camera if ImGui is using the scroll (e.g. console scroll)
            if (ImGui::GetIO().WantCaptureMouse) return;
            auto offset = CoreEngine::GetCameraOffset();
            float zoom = 1.0f + (float)dy * 0.05f;
            if (zoom < 0.1f) zoom = 0.1f;
            if (zoom > 50.0f) zoom = 50.0f;
            offset.x *= zoom;
            offset.y *= zoom;
            offset.z *= zoom;
            CoreEngine::SetCameraOffset(offset);
        });
    }

    glm::vec3 ComputeCameraPosition() {
        auto offset = CoreEngine::GetCameraOffset();
        auto target = CoreEngine::GetCameraTarget();
        return glm::vec3(
            target.x + (float)offset.x + g_cameraPanOffset.x,
            target.y + (float)offset.y + g_cameraPanOffset.y,
            target.z + (float)offset.z + g_cameraPanOffset.z);
    }

    void UpdateInput(GLFWwindow* window) {
        // Apply orbit rotation from mouse delta (skip when ImGui has mouse)
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        bool wantCaptureMouse = ImGui::GetIO().WantCaptureMouse;

        if (g_isOrbiting && !wantCaptureMouse) {
            float dx = (float)(mx - g_prevMouseX);
            float dy = (float)(my - g_prevMouseY);
            if (dx != 0.0f || dy != 0.0f) {
                auto offset = CoreEngine::GetCameraOffset();
                glm::vec3 off(offset.x, offset.y, offset.z);
                float dist = glm::length(off);
                if (dist < 0.001f) dist = 0.001f;

                float theta = atan2f(off.x, off.z);
                float phi = acosf(glm::clamp(off.y / dist, -1.0f, 1.0f));

                theta -= dx * 0.003f;
                phi -= dy * 0.003f;
                phi = glm::clamp(phi, 0.01f, glm::pi<float>() - 0.01f);

                glm::vec3 newOffset(dist * sinf(phi) * sinf(theta),
                                    dist * cosf(phi),
                                    dist * sinf(phi) * cosf(theta));
                CoreEngine::SetCameraOffset(newOffset);
            }
            g_prevMouseX = mx;
            g_prevMouseY = my;
        }

        // Apply camera rotation (right-click drag) - modifies view direction
        if (g_isRotating && !wantCaptureMouse) {
            float dx = (float)(mx - g_prevMouseX);
            float dy = (float)(my - g_prevMouseY);

            // Rotate yaw (horizontal) and pitch (vertical)
            g_cameraYaw -= dx * 0.005f;
            g_cameraPitch -= dy * 0.005f;
            g_cameraPitch = glm::clamp(g_cameraPitch, -glm::half_pi<float>() + 0.1f, glm::half_pi<float>() - 0.1f);

            g_prevMouseX = mx;
            g_prevMouseY = my;
        }

        // WASD camera movement (world-space, free look)
        // When WASD is pressed, move camera target along with camera so it never re-orients
        {
            bool moving = false;
            double keyX = 0.0, keyY = 0.0, keyZ = 0.0;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_A) == GLFW_REPEAT) {
                keyX = -1.0f; moving = true;
            }
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_D) == GLFW_REPEAT) {
                keyX = 1.0f; moving = true;
            }
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_W) == GLFW_REPEAT) {
                keyY = 1.0f; moving = true;
            }
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_S) == GLFW_REPEAT) {
                keyY = -1.0f; moving = true;
            }
            if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_Q) == GLFW_REPEAT) {
                keyZ = 1.0f; moving = true;
            }
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_E) == GLFW_REPEAT) {
                keyZ = -1.0f; moving = true;
            }

            if (moving) {
                float speed = 5.0f * 0.02f;
                g_cameraPanOffset.x += (float)keyX * speed;
                g_cameraPanOffset.y += (float)keyY * speed;
                g_cameraPanOffset.z += (float)keyZ * speed;

                // Move the target with the camera so the view doesn't reorient
                auto target = CoreEngine::GetCameraTarget();
                target.x += (float)keyX * speed;
                target.y += (float)keyY * speed;
                target.z += (float)keyZ * speed;
                CoreEngine::SetCameraTarget(target);
            }
        }
    }

    glm::mat4 ComputeViewMatrix(const glm::vec3& camPos, const glm::vec3& target) {
        // Compute rotation matrix from yaw/pitch
        glm::mat4 rotationMat = glm::rotate(glm::mat4(1.0f), g_cameraYaw, glm::vec3(0, 1, 0)) *
                                glm::rotate(glm::mat4(1.0f), g_cameraPitch, glm::vec3(1, 0, 0));

        // Calculate base direction from camera to target
        glm::vec3 baseDir = normalize(target - camPos);

        // Apply yaw/pitch rotation to direction
        glm::vec3 rotatedDir = glm::vec3(rotationMat * glm::vec4(baseDir, 0.0f));

        // Build view matrix: look at cameraPos + rotatedDir
        return glm::lookAt(camPos, camPos + rotatedDir, glm::vec3(0, 1, 0));
    }

}  // namespace Camera
}  // namespace Editor
