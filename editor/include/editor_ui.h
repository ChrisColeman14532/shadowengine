#pragma once

struct GLFWwindow;

namespace Editor {

    // Dear ImGui lifecycle (context + GLFW/OpenGL3 backends)
    void InitImGui(GLFWwindow* window);
    void ShutdownImGui();

    // Render the full UI overlay: menu bar, scene hierarchy, inspector,
    // console and shadow settings panels.
    void RenderImGui(GLFWwindow* window);

}  // namespace Editor
