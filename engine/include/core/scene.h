#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/mesh.h"
#include "core/material.h"

namespace CoreEngine {

    // Scene object — placed by editor (mesh + transform + material)
    struct SceneObject {
        uint32_t id = 0;
        std::string name;
        MeshPtr mesh;
        Vector3 position  = {0, 0, 0};
        Vector3 rotation  = {0, 0, 0};   // Euler radians
        Vector3 scale     = {1, 1, 1};
        Material material;                // PBR material
    };

    // Scene management
    std::vector<SceneObject>& GetSceneObjects();
    SceneObject& AddToScene(const std::string& name, MeshPtr mesh, Material mat);
    void ClearScene();
    void RemoveFromScene(uint32_t id);
    void SelectObject(uint32_t id);
    SceneObject* GetSelectedObject();
    uint32_t GetSelectedObjectId();
    uint32_t GetNextSceneObjectId();

} // namespace CoreEngine
