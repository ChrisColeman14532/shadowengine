#pragma once

struct GLFWwindow;

namespace Editor {

    // Render the 3D scene into the center viewport: skybox, grid,
    // shadow pass, scene objects and selection bounds. Restores the
    // full-window viewport on exit so the ImGui overlay can render.
    // dt (seconds) drives frame-rate-independent camera input.
    void RenderScene3D(GLFWwindow* window, float dt);

    // Pick a scene object under the given window-space mouse position
    // (the same coordinates GLFW reports). Selects the nearest hit, or
    // deselects when the click lands in the 3D viewport but hits nothing.
    // Safe to call before a frame has been rendered (no-op in that case).
    void TryPickAtMouse(GLFWwindow* window, float mouseX, float mouseY);

}  // namespace Editor
