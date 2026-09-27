#pragma once

// ECS systems: named, ordered update functions over the World.
//
// A system is any callable `void(World&, float dt)`. The SystemManager
// runs them in registration order every Tick — register dependencies
// before their dependents (the built-in transform system is registered
// first by RegisterBuiltinSystems).

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/ecs/world.h"

namespace CoreEngine { namespace Ecs {

    class World;

    using SystemFn = std::function<void(World&, float dt)>;

    class SystemManager {
    public:
        // Register (or re-register at the END) a named system.
        void Register(const char* name, SystemFn fn) {
            systems_.emplace_back(name, std::move(fn));
        }

        // Remove the first system with this name (if any).
        void Unregister(const char* name) {
            for (std::size_t i = 0; i < systems_.size(); ++i) {
                if (systems_[i].first == name) {
                    systems_.erase(systems_.begin() + static_cast<std::ptrdiff_t>(i));
                    return;
                }
            }
        }

        bool Has(const char* name) const {
            for (auto& s : systems_)
                if (s.first == name) return true;
            return false;
        }

        const char* NameAt(std::size_t i) const {
            return i < systems_.size() ? systems_[i].first.c_str() : nullptr;
        }

        std::size_t Count() const { return systems_.size(); }

        // Run all systems in registration order.
        void Tick(World& world, float dt) {
            for (auto& s : systems_) s.second(world, dt);
        }

    private:
        std::vector<std::pair<std::string, SystemFn>> systems_;
    };

} } // namespace CoreEngine::Ecs
