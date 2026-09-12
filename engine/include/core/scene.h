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

        // Hierarchy: 0 = top level (child of the scene root).
        // The object's world transform is the product of its own local
        // TRS with every ancestor's local TRS, so transforming a parent
        // moves/rotates/scales all of its children with it.
        uint32_t parentId = 0;
        // True for transform-only nodes (no mesh) — e.g. the model root
        // created when an FBX is imported. All parts of that model are
        // its children.
        bool isGroup = false;
    };

    // World-space transform of a scene object: local TRS multiplied by
    // the local TRS of every ancestor (parent chain). Unknown or
    // dangling parent ids are treated as top level (identity). Safe to
    // call for ids that no longer exist (returns identity).
    glm::mat4 ComputeObjectWorldMatrix(uint32_t id);

    // Decompose an affine 4x4 into the engine's TRS convention — the
    // exact composition order ObjectLocalMatrix() and the render passes
    // use:
    //     M = translate(p) * rotX(rx) * rotY(ry) * rotZ(rz) * scale(s)
    // A single reflection is folded into a negative scale component.
    // `err` receives the largest RELATIVE element difference between M and
    // the reconstructed TRS (scale-independent: pure TRS reports ~0 at any
    // model scale, while shear shows up as O(1)). Returns false when M is
    // degenerate (zero scale).
    bool DecomposeTRS(const glm::mat4& m, Vector3& position, Vector3& rotation,
                      Vector3& scale, float& err);

    // Scene management
    std::vector<SceneObject>& GetSceneObjects();
    // Creates the object and returns its id. Do NOT hold a SceneObject&
    // across a later AddToScene/ClearScene/RemoveFromScene — vector
    // reallocation invalidates references. Re-fetch with GetSceneObject(id)
    // whenever you need the object again.
    uint32_t AddToScene(const std::string& name, MeshPtr mesh, Material mat);
    // Non-const access by id (nullptr when not found).
    SceneObject* GetSceneObject(uint32_t id);
    void ClearScene();
    // Removes the object AND all of its descendants (children, and their
    // children, ...), so deleting a model root deletes every part.
    void RemoveFromScene(uint32_t id);
    void SelectObject(uint32_t id);
    SceneObject* GetSelectedObject();
    uint32_t GetSelectedObjectId();
    uint32_t GetNextSceneObjectId();

} // namespace CoreEngine
