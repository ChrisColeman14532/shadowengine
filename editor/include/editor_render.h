#pragma once

struct GLFWwindow;

namespace Editor {

    // Render the 3D scene into the center viewport: skybox, grid,
    // shadow pass, scene objects and selection bounds. Restores the
    // full-window viewport on exit so the ImGui overlay can render.
    void RenderScene3D(GLFWwindow* window);

}  // namespace Editor
