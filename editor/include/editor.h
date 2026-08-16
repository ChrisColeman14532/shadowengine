#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>

struct GLFWwindow;

namespace Editor {

    // Initialize window + engine, return window handle
    GLFWwindow* Init();
    void ShutDown(GLFWwindow* window);
    bool IsRunning(GLFWwindow* window);
    void RenderFrame(GLFWwindow* window);
    void PollEvents(GLFWwindow* window);
    void RenderImGui(GLFWwindow* window);

}  // namespace Editor