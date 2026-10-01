#include "editor.h"
#include "core/engine.h"
#include "editor_state.h"
#include "editor_camera.h"
#include "editor_ui.h"
#include "editor_render.h"
#include "editor_asset.h"
#include "editor_assetbrowser.h"
#include "editor_scene.h"
#include "core/scene_file.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <string>

namespace {
    // Base window title (engine name + version); RenderFrame prefixes the
    // current scene file name to it.
    std::string g_baseTitle;
}

namespace Editor {

    GLFWwindow* Init() {
        CoreEngine::Init();

        std::string name = CoreEngine::GetEngineName();
        int major, minor, patch;
        CoreEngine::GetVersion(major, minor, patch);
        std::cout << "[Editor] Starting " << name << " v" << major << "." << minor << "." << patch << std::endl;

        // Title bar shows the engine version, e.g. "ShadowEngine v0.2.3".
        // (RenderFrame prefixes the scene file name once one is open.)
        g_baseTitle = name + " v" + std::to_string(major) + "." +
                      std::to_string(minor) + "." + std::to_string(patch);
        int width = 1280;
        int height = 720;

        if (!CoreEngine::InitRenderer(g_baseTitle.c_str(), width, height)) {
            return nullptr;
        }

        std::cout << "[Editor] OpenGL window created (" << width << "x" << height << ")" << std::endl;
        ConsoleLog("OpenGL window created (" + std::to_string(width) + "x" + std::to_string(height) + ")");

        CoreEngine::EngineInfo info = CoreEngine::GetEngineInfo(width, height);
        std::cout << "[Editor] Engine communication OK - \"" << info.name
                  << "\" v" << info.majorVersion << "." << info.minorVersion << "." << info.patchVersion << std::endl;

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

        // Files dropped from Windows Explorer (or any OS source) go to the
        // Asset Browser: they are copied into assets/ and listed there.
        glfwSetDropCallback(CoreEngine::GetWindow(),
            [](GLFWwindow* window, int count, const char* paths[]) {
                (void)window;
                AssetBrowser::QueueExternalFiles(count, paths);
            });

        return CoreEngine::GetWindow();
    }

    void ShutDown(GLFWwindow* window) {
        CoreEngine::ClearScene();
        ShutdownImGui();
        CoreEngine::Shutdown();
    }

    bool IsRunning(GLFWwindow* window) {
        return !CoreEngine::ShouldClose();
    }

    void RenderFrame(GLFWwindow* window) {
        // Import files queued by the GLFW drop callback (Explorer drags)
        AssetBrowser::PumpExternalDrops();

        if (g_triggerFileDialog) {
            g_triggerFileDialog = false;
            LoadFBXFromFileDialog(window);
        }
        if (g_triggerAnimFileDialog) {
            g_triggerAnimFileDialog = false;
            LoadAnimationFromFileDialog(window);
        }
        if (g_triggerOpenSceneFile) {
            g_triggerOpenSceneFile = false;
            OpenSceneFromFileDialog(window);
        }
        if (g_triggerSaveSceneAsFile) {
            g_triggerSaveSceneAsFile = false;
            SaveSceneFromFileDialog(window);
        }
        if (g_triggerSaveSceneFile) {
            g_triggerSaveSceneFile = false;
            // Ctrl+S: save to the current path, or Save-As when none yet.
            const std::string& path = CoreEngine::GetSceneFilePath();
            if (path.empty())
                SaveSceneFromFileDialog(window);
            else
                SaveSceneToFile(path);
        }

        // Keep the title bar in sync with the open scene file.
        {
            // "" (no scene yet) matches the title set in Init().
            static std::string s_lastScenePath;
            const std::string& scenePath = CoreEngine::GetSceneFilePath();
            if (scenePath != s_lastScenePath) {
                s_lastScenePath = scenePath;
                std::string fileName = scenePath;
                size_t slash = fileName.find_last_of("/\\");
                if (slash != std::string::npos) fileName = fileName.substr(slash + 1);
                glfwSetWindowTitle(window, (fileName.empty()
                    ? g_baseTitle
                    : fileName + " - " + g_baseTitle).c_str());
            }
        }

        // Advance animation playback (and refresh bone palettes) before
        // rendering. dt is clamped so a backgrounded window doesn't skip
        // ahead of the clip on return.
        static float s_lastFrameTime = -1.0f;
        float now = (float)glfwGetTime();
        float dt = (s_lastFrameTime < 0.0f) ? 0.0f : (now - s_lastFrameTime);
        s_lastFrameTime = now;
        if (dt > 0.1f) dt = 0.1f;
        CoreEngine::Animator::Get().Tick(dt);

        RenderScene3D(window, dt);
        RenderImGui(window);
        CoreEngine::RenderEnd();
    }

    void PollEvents(GLFWwindow* window) {
        glfwPollEvents();
    }

}  // namespace Editor
