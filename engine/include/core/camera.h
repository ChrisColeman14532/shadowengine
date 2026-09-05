#pragma once

#include <glm/glm.hpp>

#include "core/types.h"

namespace CoreEngine {

    // Camera (orbit-style)
    void SetCameraPosition(Vector3 pos);
    void SetCameraTarget(Vector3 target);
    void ResetCamera(); // Reset to initial default camera state
    void SetCameraDirection(Vector3 dir);
    Vector3 GetCameraPosition();
    Vector3 GetCameraTarget();
    Vector3 GetCameraDirection();
    Vector3 GetCameraOffset();
    void SetCameraOffset(Vector3 offset);
    glm::mat4 GetProjectionMatrix(float fov, float aspect);
    glm::mat4 GetProjectionMatrix(float fov, float aspect, float nearPlane, float farPlane);

    // ── Camera (as a scene object) ──────────────────────────────────
    uint32_t GetCameraObjectId();
    void SetCameraId(uint32_t id);
    bool IsCameraObjectId(uint32_t id);
    void CreateCameraObject();          // create camera visual in scene
    void SyncSceneToCameraObject();     // editor: pull object pos → orbit camera

} // namespace CoreEngine
