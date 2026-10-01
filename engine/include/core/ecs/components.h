#pragma once

// ECS components used by the scene-integrated world.
//
// Components are plain data — no behavior. A component's presence on an
// entity is meaningful: e.g. only entities with MeshRenderer are drawn,
// so groups (no mesh) drop out of render queries automatically.
//
// In the scene world, entity value == scene object id (see core/ecs.h),
// so a Transform's `parent` is directly the parent's entity id.

#include <string>

#include <glm/glm.hpp>

#include "core/types.h"
#include "core/mesh.h"
#include "core/material.h"
#include "core/ecs/entity.h"

namespace CoreEngine { namespace Ecs {

    // Local transform + parent link. Rotation is XYZ Euler in RADIANS
    // (copied from SceneObject::rotation), applied X then Y then Z —
    // matching Scene::ObjectLocalMatrix.
    struct Transform {
        Vector3 position{0.0f, 0.0f, 0.0f};
        Vector3 rotation{0.0f, 0.0f, 0.0f};
        Vector3 scale{1.0f, 1.0f, 1.0f};
        Entity  parent = INVALID_ENTITY;
    };

    // Local->world matrix, computed by the transform system each tick.
    // This is what render passes bind as uModel.
    struct WorldTransform {
        glm::mat4 localToWorld = glm::mat4(1.0f);
    };

    // Human-readable label (the scene object's name).
    struct Name {
        std::string value;
    };

    // "Draw this mesh here." `mesh` is a shared ref (see mesh.h);
    // `material` mirrors SceneObject::material.
    struct MeshRenderer {
        MeshPtr  mesh;
        Material material;
    };

} } // namespace CoreEngine::Ecs
