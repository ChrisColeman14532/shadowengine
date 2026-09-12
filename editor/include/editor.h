#pragma once

// ShadowEngine editor — public API.
//
// Module breakdown:
//   editor_state.h   shared editor state (console log, panel flags, ...)
//   editor_camera.h  orbit camera input + view matrix computation
//   editor_ui.h      Dear ImGui overlay (menu bar + panels)
//   editor_render.h  3D viewport rendering (skybox, shadows, objects)
//   editor_asset.h   FBX loading

struct GLFWwindow;

namespace Editor {

    // Initialize window + engine, return window handle
    GLFWwindow* Init();
    void ShutDown(GLFWwindow* window);
    bool IsRunning(GLFWwindow* window);
    void RenderFrame(GLFWwindow* window);
    void PollEvents(GLFWwindow* window);

}  // namespace Editor
