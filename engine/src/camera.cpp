// Camera: orbit-style camera state, projection helpers, and the
// camera's representation as a scene object (editor integration).

#include "core/engine.h"

#include <glm/gtc/matrix_transform.hpp>

#include "engine_internal.h"

namespace CoreEngine {

// ── Orbit camera ───────────────────────────────────────────────────

void SetCameraPosition(Vector3 pos) {
    s_cameraPos = pos;
    s_cameraOffset = s_cameraPos - s_cameraTarget;
}
void SetCameraTarget(Vector3 target) {
    s_cameraTarget = target;
    s_cameraOffset = s_cameraPos - s_cameraTarget;
}
void SetCameraDirection(Vector3 dir) { s_cameraOffset = dir; }
Vector3 GetCameraPosition() { return s_cameraPos; }
Vector3 GetCameraTarget() { return s_cameraTarget; }
Vector3 GetCameraDirection() { return s_cameraOffset; }
Vector3 GetCameraOffset() { return s_cameraOffset; }
void SetCameraOffset(Vector3 offset) {
    s_cameraOffset = offset;
    s_cameraPos = s_cameraTarget + offset;
}
// Default framing: a 3/4 view from above the origin, looking down at the
// ground grid (Unity-style). The camera sits above the y=0 ground plane so
// the grid is seen from above, receding to the horizon.
// Keep in sync with the s_cameraPos/s_cameraOffset initializers in
// engine_internal.h.
void ResetCamera() {
    s_cameraPos = {60, 45, 90};
    s_cameraTarget = {0, 0, 0};
    s_cameraOffset = s_cameraPos - s_cameraTarget;
}

glm::mat4 GetProjectionMatrix(float fov, float aspect) {
    return GetProjectionMatrix(fov, aspect, 0.1f, 100.0f);
}

glm::mat4 GetProjectionMatrix(float fov, float aspect, float nearPlane, float farPlane) {
    if (farPlane <= nearPlane) farPlane = nearPlane + 1.0f;
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}

// ── Camera as a scene object ────────────────────────────────────────

static SceneObject* GetCameraObject() {
    for (auto& obj : s_sceneObjects) {
        if (obj.id == s_cameraObjectId) return &obj;
    }
    return nullptr;
}

void SetCameraId(uint32_t id) { s_cameraObjectId = id; }
uint32_t GetCameraObjectId() { return s_cameraObjectId; }
bool IsCameraObjectId(uint32_t id) { return id == s_cameraObjectId; }

// Called from editor to create the camera object
void CreateCameraObject() {
    if (s_cameraObjectId != 0) return; // already created

    auto cubeMesh = CreateBox({1, 1, 1});
    cubeMesh.name = "cube";
    s_cameraObjectId = s_nextSceneObjectId++;

    SceneObject cam;
    cam.id = s_cameraObjectId;
    cam.name = "Camera";
    cam.mesh = CreateMesh(std::move(cubeMesh));
    cam.position = s_cameraPos;
    cam.scale = {0.3f, 0.3f, 0.3f};
    cam.material.name = "camera_material";
    cam.material.baseColor = glm::vec3(0.2f, 0.6f, 1.0f); // blue
    cam.material.useMaterial = true;
    s_sceneObjects.push_back(std::move(cam));
}

// Called when camera object is moved via inspector
void SyncSceneToCameraObject() {
    auto* cam = GetCameraObject();
    if (!cam) return;
    s_cameraPos = cam->position;
    s_cameraOffset = s_cameraPos - s_cameraTarget;
}

} // namespace CoreEngine
