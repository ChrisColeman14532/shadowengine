#pragma once

#include <glm/glm.hpp>

namespace CoreEngine {

    // Solid ground plane on y=0 that fades into the sky's horizon color
    // with distance (exponential fog). Drawn before the grid so the grid
    // lines sit on top. The plane follows the camera (cell-snapped) so it
    // is effectively infinite, and its fog color matches the skybox so the
    // ground melts into the backdrop at the horizon with no seam.
    void DrawGroundPlane(const glm::mat4& view, const glm::mat4& projection);

    // 3D ground grid (Unity-style): minor line every `spacing` world
    // units, constant pixel line width, distance fade, and a
    // camera-following (effectively infinite) extent. spacing = 1.0 puts
    // one unit cube between lines. The lines dissolve into the same fog
    // color as the ground plane so they vanish at the horizon.
    void DrawGrid(float spacing, const glm::mat4& view, const glm::mat4& projection);

    // Bounding box wireframe for selected object
    void DrawSelectedObjectBounds(const glm::mat4& view, const glm::mat4& projection);

} // namespace CoreEngine
