// Materials: PBR material defaults.

#include "core/engine.h"

namespace CoreEngine {

Material CreateDefaultMaterial() {
    return Material{
        "default",
        glm::vec3(0.7f, 0.7f, 0.7f),  // base color - mid gray
        glm::vec3(0.0f, 0.0f, 0.0f),  // emissive
        0.0f,             // metallic
        1.0f,             // roughness
        1.0f,             // AO
        {}, {},           // diffuseTexture, normalTexture (nullptr = no texture)
        false
    };
}

} // namespace CoreEngine
