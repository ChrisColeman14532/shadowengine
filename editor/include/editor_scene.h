#pragma once

#include <string>

struct GLFWwindow;

namespace Editor {

    // Save the live scene to `path` (.scene JSON, via CoreEngine).
    // Logs the result to the editor console. Returns true on success.
    bool SaveSceneToFile(const std::string& path);

    // Open the native Save-As dialog (Win32) and save the scene to the
    // chosen path.
    void SaveSceneFromFileDialog(GLFWwindow* window);

    // Open the native Open dialog (Win32) and replace the live scene with
    // the chosen .scene file (FBX assets are re-imported).
    void OpenSceneFromFileDialog(GLFWwindow* window);

}  // namespace Editor
