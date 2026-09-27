#pragma once

namespace CoreEngine {

    // Skybox (procedural gradient with sun)
    void InitSkybox();
    void DrawSkybox(Vector3 cameraPosition, Vector3 cameraTarget, float aspect = 1280.0f / 720.0f);

} // namespace CoreEngine
