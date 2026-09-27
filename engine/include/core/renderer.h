#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <string>

#include "core/types.h"

namespace CoreEngine {

    static constexpr const char* ENGINE_NAME   = "ShadowEngine";
    static constexpr int         VERSION_MAJOR   = 0;
    static constexpr int         VERSION_MINOR   = 2;
    static constexpr int         VERSION_PATCH   = 3;  // v0.2.3

    // Lifecycle
    void Init();
    void Shutdown();
    std::string GetEngineName();
    void GetVersion(int& major, int& minor, int& patch);
    EngineInfo GetEngineInfo(int width, int height);

    // Renderer (existing)
    bool InitRenderer(const char* title, int width, int height);
    GLFWwindow* GetWindow();
    void RenderBegin();
    void RenderEnd();
    bool ShouldClose();

} // namespace CoreEngine
