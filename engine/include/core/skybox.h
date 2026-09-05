#pragma once

#include <glm/glm.hpp>

namespace CoreEngine {

    // Skybox (procedural gradient with sun)
    void InitSkybox();
    void DrawSkybox(glm::vec3 cameraPosition, float aspect = 1280.0f / 720.0f);

} // namespace CoreEngine
