#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

namespace Editor {
namespace Camera {

    // Install GLFW mouse/scroll/keyboard callbacks that drive the
    // editor camera: orbit (left drag), rotate (right drag),
    // zoom (scroll), WASD/QE pan, plus editor hotkeys.
    void InstallInputCallbacks(GLFWwindow* window);

    // World-space camera position: target + orbit offset + pan offset.
    glm::vec3 ComputeCameraPosition();

    // Apply this frame's input deltas (orbit / rotate / WASD pan).
    // Call once per frame AFTER the camera position was computed and
    // BEFORE the view matrix is built.
    void UpdateInput(GLFWwindow* window);

    // View matrix for the given camera position + target, including
    // the yaw/pitch rotation accumulated from right-drag.
    glm::mat4 ComputeViewMatrix(const glm::vec3& camPos, const glm::vec3& target);

}  // namespace Camera
}  // namespace Editor
