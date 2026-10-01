#pragma once

// Built-in ECS systems.
//
// Currently just the transform hierarchy: every tick it recomputes the
// WorldTransform (local->world matrix) of every entity that has a
// Transform, by resolving each parent's world matrix first (DFS with a
// cycle guard — a parent cycle breaks the chain instead of recursing
// forever, and the result is stable from frame to frame).
//
// RegisterBuiltinSystems adds the systems in dependency order; call it
// once on a SystemManager before ticking.

#include <cstdint>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "core/ecs/world.h"
#include "core/ecs/system.h"
#include "core/ecs/components.h"

namespace CoreEngine { namespace Ecs {

    // Local TRS matrix for a Transform.
    // MUST stay in lock-step with Scene::ObjectLocalMatrix in
    // engine/src/scene.cpp: translate * rotX * rotY * rotZ * scale.
    inline glm::mat4 TransformLocalMatrix(const Transform& t) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f),
                                     glm::vec3(t.position.x, t.position.y, t.position.z));
        // Rotation values are RADIANS (same convention as
        // Scene::ObjectLocalMatrix) — do NOT wrap in glm::radians().
        m = m * glm::rotate(glm::mat4(1.0f), t.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        m = m * glm::rotate(glm::mat4(1.0f), t.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        m = m * glm::rotate(glm::mat4(1.0f), t.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
        m = m * glm::scale(glm::mat4(1.0f),
                           glm::vec3(t.scale.x, t.scale.y, t.scale.z));
        return m;
    }

    // ── Transform system ────────────────────────────────────────────
    // Computes WorldTransform.localToWorld for every Transform entity.
    // World matrix = parentWorld * local, root = local. A parent without
    // a Transform (or an unknown/invalid one) is treated as the world
    // root. Cycles are cut at the in-progress link.
    inline void TransformSystemTick(World& w, float dt) {
        (void)dt;

        auto& ts = w.Storage<Transform>();
        auto& ws = w.Storage<WorldTransform>();

        // Every Transform entity gets a WorldTransform slot.
        for (std::size_t i = 0; i < ts.Count(); ++i)
            ws.Emplace(ts.EntityAt(i));

        // DFS state: 0 = todo, 1 = in progress (on the recursion stack),
        // 2 = resolved this tick.
        std::vector<std::uint8_t> state(w.Capacity(), 0);

        // Returns true when e's WorldTransform is valid after the call.
        const auto resolve = [&](Entity e, auto&& self) -> bool {
            if (e >= state.size() || !w.IsAlive(e)) return false;
            if (!w.Has<Transform>(e)) return false;
            if (state[e] == 2) return true;
            if (state[e] == 1) return false;  // cycle: cut the chain
            state[e] = 1;

            const Transform& t = *w.Get<Transform>(e);
            glm::mat4 local = TransformLocalMatrix(t);

            bool parentOk = false;
            if (t.parent != INVALID_ENTITY &&
                w.IsAlive(t.parent) && w.Has<Transform>(t.parent)) {
                parentOk = self(t.parent, self);
            }

            WorldTransform& wt = ws.Emplace(e);
            wt.localToWorld = parentOk
                ? w.Get<WorldTransform>(t.parent)->localToWorld * local
                : local;

            state[e] = 2;
            return true;
        };

        for (Entity e : w.AliveEntities())
            if (w.Has<Transform>(e))
                resolve(e, resolve);
    }

    // Register the built-in systems in dependency order.
    // Idempotent per name (re-registering replaces order at the end).
    inline void RegisterBuiltinSystems(SystemManager& sys) {
        sys.Register("ecs.Transform", TransformSystemTick);
    }

} } // namespace CoreEngine::Ecs
