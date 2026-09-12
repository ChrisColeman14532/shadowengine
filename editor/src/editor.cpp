#include "editor.h"
#include "core/engine.h"
#include "editor_state.h"
#include "editor_camera.h"
#include "editor_ui.h"
#include "editor_render.h"
#include "editor_asset.h"

#include <GLFW/glfw3.h>
#include <iostream>

namespace Editor {

    GLFWwindow* Init() {
        CoreEngine::Init();

        std::string name = CoreEngine::GetEngineName();
        int major, minor;
        CoreEngine::GetVersion(major, minor);
        std::cout << "[Editor] Starting " << name << " v" << major << "." << minor << std::endl;

        const char* title = name.c_str();
        int width = 1280;
        int height = 720;

        if (!CoreEngine::InitRenderer(title, width, height)) {
            return nullptr;
        }

        std::cout << "[Editor] OpenGL window created (" << width << "x" << height << ")" << std::endl;
        ConsoleLog("OpenGL window created (" + std::to_string(width) + "x" + std::to_string(height) + ")");

        CoreEngine::EngineInfo info = CoreEngine::GetEngineInfo(width, height);
        std::cout << "[Editor] Engine communication OK - \"" << info.name
                  << "\" v" << info.majorVersion << "." << info.minorVersion << std::endl;

        InitImGui(CoreEngine::GetWindow());

        // Initialize skybox
        CoreEngine::InitSkybox();

        // Initialize shadow mapping
        CoreEngine::InitShadowMap(2048, 2048);

        // Create camera as a scene object
        CoreEngine::CreateCameraObject();

        glfwSetInputMode(CoreEngine::GetWindow(), GLFW_REPEAT, GLFW_TRUE);
        glfwFocusWindow(CoreEngine::GetWindow());

        // Orbit / rotate / zoom / WASD input + editor hotkeys
        Camera::InstallInputCallbacks(CoreEngine::GetWindow());

        return CoreEngine::GetWindow();
    }

    void ShutDown(GLFWwindow* window) {
        CoreEngine::CleanupShadowMap();
        CoreEngine::ClearScene();
        ShutdownImGui();
    }

    bool IsRunning(GLFWwindow* window) {
        return !CoreEngine::ShouldClose();
    }

    void RenderFrame(GLFWwindow* window) {
        if (g_triggerFileDialog) {
            g_triggerFileDialog = false;
            LoadFBXFromFileDialog(window);
        }
        if (g_triggerAnimFileDialog) {
            g_triggerAnimFileDialog = false;
            LoadAnimationFromFileDialog(window);
        }

        // Advance animation playback (and refresh bone palettes) before
        // rendering. dt is clamped so a backgrounded window doesn't skip
        // ahead of the clip on return.
        {
            static float s_lastFrameTime = -1.0f;
            float now = (float)glfwGetTime();
            float dt = (s_lastFrameTime < 0.0f) ? 0.0f : (now - s_lastFrameTime);
            s_lastFrameTime = now;
            if (dt > 0.1f) dt = 0.1f;
            CoreEngine::Animator::Get().Tick(dt);
        }

        RenderScene3D(window);
        RenderImGui(window);
        CoreEngine::RenderEnd();
    }

    void PollEvents(GLFWwindow* window) {
        glfwPollEvents();
    }

}  // namespace Editor
