#pragma once

#include <glm/glm.hpp>

namespace CoreEngine {

    // 3D grid rendering
    void DrawGrid(int divisions, float unit, float halfExtent, const glm::mat4& view, const glm::mat4& projection);

    // Bounding box wireframe for selected object
    void DrawSelectedObjectBounds(const glm::mat4& view, const glm::mat4& projection);

} // namespace CoreEngine
