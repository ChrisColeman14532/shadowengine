// Scene management: scene objects, selection, and id allocation.

#include "core/engine.h"

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

#include "engine_internal.h"

namespace CoreEngine {

std::vector<SceneObject>& GetSceneObjects() { return s_sceneObjects; }

SceneObject& AddToScene(const std::string& name, MeshPtr mesh, Material mat) {
    SceneObject obj;
    obj.id = s_nextSceneObjectId++;
    obj.name = name;
    obj.mesh = std::move(mesh);
    obj.material = std::move(mat);
    s_sceneObjects.push_back(std::move(obj));
    return s_sceneObjects.back();
}

void ClearScene() {
    s_selectedObjectId = 0;
    s_cameraObjectId = 0;  // Reset so camera gets recreated on next CreateCameraObject()
    s_sceneObjects.clear();
}

void RemoveFromScene(uint32_t id) {
    for (auto it = s_sceneObjects.begin(); it != s_sceneObjects.end(); ++it) {
        if (it->id == id) {
            if (s_selectedObjectId == id) s_selectedObjectId = 0;
            s_sceneObjects.erase(it);
            return;
        }
    }
}

void SelectObject(uint32_t id) {
    s_selectedObjectId = id;
}

SceneObject* GetSelectedObject() {
    for (auto& obj : s_sceneObjects) {
        if (obj.id == s_selectedObjectId) {
            return &obj;
        }
    }
    return nullptr;
}

uint32_t GetSelectedObjectId() { return s_selectedObjectId; }

uint32_t GetNextSceneObjectId() { return s_nextSceneObjectId; }

} // namespace CoreEngine
