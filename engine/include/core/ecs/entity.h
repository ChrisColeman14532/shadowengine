#pragma once

// ECS entity handle.
//
// An Entity is just a packed integer — the index into the World's
// per-component sparse sets and the liveness bitset. Entities are cheap
// to pass around; ALL access goes through the World (never cache a
// pointer to a component across World mutations — sparse-set removal
// swaps dense slots).
//
// In the scene-integrated world the entity value equals the scene
// object id (see core/ecs.h), so existing code paths (Animator bone
// palettes, picking, selection) keep working with the same numbers.

#include <cstdint>

namespace CoreEngine { namespace Ecs {

    using Entity = std::uint32_t;

    // 0 is reserved as "no entity". Scene object ids also start at 1.
    inline constexpr Entity INVALID_ENTITY = 0;

} } // namespace CoreEngine::Ecs
