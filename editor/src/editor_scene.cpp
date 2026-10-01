#include "editor_scene.h"
#include "editor_state.h"
#include "core/engine.h"
#include "core/scene_file.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace Editor {

    bool SaveSceneToFile(const std::string& path) {
        bool ok = CoreEngine::SaveSceneFile(path, g_smoothNormals);
        ConsoleLog(ok ? ("Scene saved: " + path)
                      : ("FAILED to save scene: " + path));
        return ok;
    }

    void SaveSceneFromFileDialog(GLFWwindow* window) {
        (void)window;
#if defined(_WIN32)
        char filePath[MAX_PATH] = {0};
        OPENFILENAME ofn = {0};
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = 0;
        ofn.lpstrFilter = "Scene Files (*.scene)\0*.scene\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_PATHMUSTEXIST;
        if (GetSaveFileName(&ofn)) {
            // The filter defaults to *.scene, but the user can type a name
            // without an extension — append it so the file is recognizable.
            std::string path(filePath);
            size_t dot = path.find_last_of('.');
            if (dot == std::string::npos)
                path += ".scene";
            SaveSceneToFile(path);
        }
#else
        printf("[Editor] Native scene save dialog not implemented on this platform\n");
#endif
    }

    void OpenSceneFromFileDialog(GLFWwindow* window) {
        (void)window;
#if defined(_WIN32)
        char filePath[MAX_PATH] = {0};
        OPENFILENAME ofn = {0};
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = 0;
        ofn.lpstrFilter = "Scene Files (*.scene)\0*.scene\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileName(&ofn)) {
            std::string path(filePath);
            if (CoreEngine::LoadSceneFile(path)) {
                ConsoleLog("Scene loaded: " + path);
            } else {
                ConsoleLog("FAILED to open scene: " + path);
            }
        }
#else
        printf("[Editor] Native scene open dialog not implemented on this platform\n");
#endif
    }

}  // namespace Editor
