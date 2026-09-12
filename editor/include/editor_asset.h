#pragma once

#include <string>

struct GLFWwindow;

namespace Editor {

    // Load the FBX at `path` into the scene: ADDS one object per sub-mesh
    // (existing objects are left untouched) and frames the camera on the
    // model's bounding box.
    void LoadFBXAtPath(const std::string& path);

    // Remove the scene objects created by the previous LoadFBXAtPath() call
    // and re-import the last loaded FBX (applies new import settings, e.g.
    // smooth normals, in place).
    void ReloadLastFBX();

    // Open the native file dialog (Win32) and load the chosen FBX.
    void LoadFBXFromFileDialog(GLFWwindow* window);

    // Load an animation file (e.g. a mixamo animation FBX) and bind it to
    // the skinned model objects currently in the scene (Animator).
    void LoadAnimationAtPath(const std::string& path);

    // Open the native file dialog and load the chosen animation FBX.
    void LoadAnimationFromFileDialog(GLFWwindow* window);

}  // namespace Editor
