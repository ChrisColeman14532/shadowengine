#pragma once

// ECS World: entity lifecycle + sparse-set component storage + queries.
//
// ── Design ─────────────────────────────────────────────────────────
//
// Storage: classic sparse sets. Each component type owns
//     sparse_[entity]  -> dense index  (or -1)
//     dense_[i]        -> component value
//     entities_[i]     -> entity
// Give/take/get are O(1). Removal swaps the LAST dense element into the
// hole, so iteration order is NOT stable — systems must not assume any
// order (the transform system resolves hierarchy order explicitly).
//
// Lifecycle: a World keeps one ComponentStorage per component type (in a
// type-erased list). Destroy(e) calls OnEntityDestroyed on every storage
// first, so a storage can never hold a dead entity.
//
// Ids: CreateEntity() allocates from a free list / incrementing counter
// (fresh ECS use). CreateEntity(id) reserves a specific id — the
// scene integration uses this so entity value == scene object id.
//
// Header-only; no GL dependency (component types may carry mesh refs,
// the World itself doesn't care).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "core/ecs/entity.h"

namespace CoreEngine { namespace Ecs {

    // ── Type-erased storage base ────────────────────────────────────
    struct StorageBase {
        virtual ~StorageBase() = default;
        // Called by World::Destroy before the entity is marked dead.
        virtual void OnEntityDestroyed(Entity e) = 0;
    };

    // ── Sparse-set component storage ────────────────────────────────
    template <typename T>
    class ComponentStorage final : public StorageBase {
    public:
        void OnEntityDestroyed(Entity e) override { Remove(e); }

        // Add `value` to entity e, or return the existing component. O(1).
        T& Emplace(Entity e, T value = T()) {
            if (e >= sparse_.size()) sparse_.resize(e + 1, -1);
            const std::int32_t slot = sparse_[e];
            if (slot >= 0) return dense_[slot];
            sparse_[e] = static_cast<std::int32_t>(dense_.size());
            dense_.push_back(std::move(value));
            entities_.push_back(e);
            return dense_.back();
        }

        T* Get(Entity e) {
            if (e >= sparse_.size()) return nullptr;
            const std::int32_t slot = sparse_[e];
            return slot >= 0 ? &dense_[slot] : nullptr;
        }
        const T* Get(Entity e) const {
            if (e >= sparse_.size()) return nullptr;
            const std::int32_t slot = sparse_[e];
            return slot >= 0 ? &dense_[slot] : nullptr;
        }

        bool Has(Entity e) const { return Get(e) != nullptr; }

        // Swap-remove: O(1), changes iteration order.
        void Remove(Entity e) {
            if (e >= sparse_.size()) return;
            const std::int32_t slot = sparse_[e];
            if (slot < 0) return;
            const std::int32_t last = static_cast<std::int32_t>(dense_.size()) - 1;
            if (slot != last) {
                std::swap(dense_[slot], dense_[last]);
                entities_[slot] = entities_[last];
                sparse_[entities_[slot]] = slot;
            }
            dense_.pop_back();
            entities_.pop_back();
            sparse_[e] = -1;
        }

        std::size_t Count() const { return dense_.size(); }
        const T& At(std::size_t i) const { return dense_[i]; }
        T& At(std::size_t i) { return dense_[i]; }
        Entity EntityAt(std::size_t i) const { return entities_[i]; }

    private:
        std::vector<std::int32_t> sparse_;   // entity -> dense index, -1 = absent
        std::vector<T>           dense_;     // packed values (iteration order)
        std::vector<Entity>      entities_;  // dense index -> entity
    };

    // ── World ───────────────────────────────────────────────────────
    class World {
    public:
        // ── Entity lifecycle ────────────────────────────────────────
        // Allocate a new id (free list first, then incrementing).
        Entity CreateEntity() {
            Entity e;
            if (!freeList_.empty()) {
                e = freeList_.back();
                freeList_.pop_back();
            } else {
                e = nextId_++;
            }
            if (e == INVALID_ENTITY) return INVALID_ENTITY;  // ids start at 1
            Reserve(e);
            return e;
        }

        // Reserve a specific id. Returns INVALID_ENTITY if the id is
        // already alive (or invalid). With fixed ids (scene sync) an id
        // is either alive (same entity — reuse) or free, so a failure
        // means a genuine conflict.
        Entity CreateEntity(Entity id) {
            if (id == INVALID_ENTITY || IsAlive(id)) return INVALID_ENTITY;
            // Keep the free list consistent: an explicitly reserved id
            // must not also be handed out by a later no-arg CreateEntity()
            // (Destroy() pushes ids onto freeList_, so an id can be both
            // free-listed and re-reserved explicitly).
            freeList_.erase(std::remove(freeList_.begin(), freeList_.end(), id),
                             freeList_.end());
            Reserve(id);
            return id;
        }

        // Create if missing; returns the (existing or new) id.
        Entity EnsureEntity(Entity id) {
            if (id == INVALID_ENTITY) return INVALID_ENTITY;
            if (IsAlive(id)) return id;
            return CreateEntity(id);
        }

        void Destroy(Entity e) {
            if (!IsAlive(e)) return;
            // Drop every component FIRST so no storage outlives its entity.
            for (auto& s : storages_) s->OnEntityDestroyed(e);
            alive_[e] = 0;
            for (std::size_t i = 0; i < aliveList_.size(); ++i) {
                if (aliveList_[i] == e) {
                    aliveList_[i] = aliveList_.back();  // swap-pop
                    aliveList_.pop_back();
                    break;
                }
            }
            freeList_.push_back(e);
        }

        bool IsAlive(Entity e) const {
            return e < alive_.size() && alive_[e] != 0;
        }

        // ── Components ──────────────────────────────────────────────
        // Get-or-create (default or moved value).
        template <typename T>
        T& AddOrGet(Entity e, T value = T()) {
            return Storage<T>().Emplace(e, std::move(value));
        }

        template <typename T>
        T* Get(Entity e) {
            auto* s = FindStorage<T>();
            return s ? s->Get(e) : nullptr;
        }
        template <typename T>
        const T* Get(Entity e) const {
            auto* s = FindStorage<T>();
            return s ? s->Get(e) : nullptr;
        }

        // Get-or-create the component storage for T. Systems use this to
        // iterate a component directly or to EnsureComponent().
        template <typename T>
        ComponentStorage<T>& Storage() {
            if (auto* p = FindStorage<T>()) return *p;
            auto created = std::make_unique<ComponentStorage<T>>();
            auto& ref = *created;
            storages_.push_back(std::move(created));
            return ref;
        }

        // Ensure entity e has component T (default-constructed if absent).
        template <typename T>
        T& EnsureComponent(Entity e, T value = T()) {
            return Storage<T>().Emplace(e, std::move(value));
        }

        template <typename T>
        bool Has(Entity e) const {
            const auto* s = FindStorage<T>();
            return s && s->Has(e);
        }

        template <typename T>
        void Remove(Entity e) {
            auto* s = FindStorage<T>();
            if (s) s->Remove(e);
        }

        // ── Queries ─────────────────────────────────────────────────
        // Iterate every alive entity that has ALL listed components.
        // Callback: fn(Entity, First&, Rest&...). Order is unstable.
        // Example:
        //     world.ForEach<Ecs::WorldTransform, Ecs::MeshRenderer>(
        //         [](Ecs::Entity e, auto& wt, auto& mr) { ... });
        template <typename First, typename... Rest, typename Fn>
        void ForEach(Fn&& fn) {
            auto& st = Storage<First>();
            for (std::size_t i = 0; i < st.Count(); ++i) {
                const Entity e = st.EntityAt(i);
                if (!IsAlive(e)) continue;
                First& f = st.At(i);
                // Collect the remaining component pointers; call only if
                // every one is present.
                auto call = [this, e, &fn, &f](auto*... rest) {
                    if (((rest != nullptr) && ...))
                        fn(e, f, *rest...);
                };
                call(Get<Rest>(e)...);
            }
        }

        // ── Introspection ───────────────────────────────────────────
        std::size_t EntityCount() const { return aliveList_.size(); }
        // Copy of the live id list (safe to destroy while iterating).
        std::vector<Entity> AliveEntities() const { return aliveList_; }
        // Highest reserved entity value + 1 (size of index arrays).
        std::size_t Capacity() const { return alive_.size(); }

        // Drop every entity and every component storage (full reset).
        void Clear() {
            alive_.clear();
            aliveList_.clear();
            freeList_.clear();
            nextId_ = 1;
            storages_.clear();
        }

    private:
        void Reserve(Entity e) {
            if (e + 1 >= alive_.size()) alive_.resize(e + 1, 0);
            alive_[e] = 1;
            aliveList_.push_back(e);
        }

        template <typename T>
        ComponentStorage<T>* FindStorage() {
            for (auto& s : storages_)
                if (auto* p = dynamic_cast<ComponentStorage<T>*>(s.get())) return p;
            return nullptr;
        }
        template <typename T>
        const ComponentStorage<T>* FindStorage() const {
            for (auto& s : storages_)
                if (auto* p = dynamic_cast<const ComponentStorage<T>*>(s.get())) return p;
            return nullptr;
        }

        std::vector<std::uint8_t> alive_;    // entity -> alive bit
        std::vector<Entity>       aliveList_; // live ids (unordered)
        std::vector<Entity>       freeList_;  // recyclable ids
        Entity                   nextId_ = 1;
        std::vector<std::unique_ptr<StorageBase>> storages_;
    };

} } // namespace CoreEngine::Ecs
