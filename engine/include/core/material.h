#pragma once

#include <string>
#include <glm/glm.hpp>

#include "core/texture.h"

namespace CoreEngine {

    // ── Materials ───────────────────────────────────────────────────

    struct Material {
        std::string name = "default";
        glm::vec3 baseColor = glm::vec3(0.5f);
        glm::vec3 emissiveColor = glm::vec3(0.0f);
        float metallic = 0.0f;    // 0 = non-metal, 1 = metal
        float roughness = 1.0f;   // 0 = polished, 1 = rough
        float ao = 1.0f;          // ambient occlusion multiplier
        TexturePtr diffuseTexture;   // nullptr means no texture
        TexturePtr normalTexture;    // nullptr means no texture
        bool useMaterial = false;
    };
    Material CreateDefaultMaterial();

} // namespace CoreEngine
