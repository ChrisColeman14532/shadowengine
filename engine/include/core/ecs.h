#pragma once

// ECS (Entity-Component-System) for the ShadowEngine.
//
// Two layers:
//   1. Core (namespace CoreEngine::Ecs, header-only):
//        entity.h    Entity handle
//        world.h     World + sparse-set component storage + queries
//        system.h    SystemManager (ordered named systems)
//        components.h  Transform / WorldTransform / Name / MeshRenderer
//        systems.h   built-in transform-hierarchy system
//
//   2. Scene integration (namespace CoreEngine, engine/src/ecs_scene.cpp):
//      the SceneObject vector remains the editor-facing scene
//      representation. Once per frame, TickEcs() projects the scene
//      into the ECS world (entity value == scene object id) and runs
//      the systems. Render paths (main pass, shadow pass, far plane,
//      picking) then read ECS state instead of re-walking the parent
//      chain per object.
//
// Frame order (editor render loop):
//     CoreEngine::TickEcs(dt);   // sync scene -> ECS, compute world matrices
//     ... shadow pass, far plane, main pass (all ECS queries) ...

#include <GL/glew.h>

#include "core/ecs/entity.h"
#include "core/ecs/world.h"
#include "core/ecs/system.h"
#include "core/ecs/components.h"
#include "core/ecs/systems.h"

namespace CoreEngine {

    // ── Scene <-> ECS integration ───────────────────────────────────

    // The engine-wide world (also usable standalone by systems/tests).
    Ecs::World& GetEcsWorld();

    // The engine-wide system manager.
    Ecs::SystemManager& GetEcsSystems();

    // Register the built-in systems (transform hierarchy). Idempotent.
    void InitEcs();

    // Project the current scene objects into the ECS world:
    // upsert entities (Transform/Name, MeshRenderer when the object has
    // a mesh), prune entities whose scene object is gone. Cheap enough
    // to call any time; TickEcs() does it every frame.
    void SyncSceneToEcs();

    // Sync scene -> ECS, then run all systems.
    // Call ONCE PER FRAME, BEFORE any render pass that needs world
    // matrices (shadow pass, far plane, main pass).
    void TickEcs(float dt);

    // Draw every entity with (WorldTransform, MeshRenderer) — the
    // ECS main pass. Skips the camera object (its cube is drawn by the
    // editor viewport code). Binds per-object model/material/textures
    // and bone palettes exactly like the legacy per-object loop did.
    void RenderSceneMeshesEcs(GLuint program);

} // namespace CoreEngine
