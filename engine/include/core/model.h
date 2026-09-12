#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "core/mesh.h"
#include "core/animation.h"

namespace CoreEngine {

    // Loaded FBX (from asset_loader.cpp)
    struct FBXModel {
        bool success = false;
        std::string filename;
        std::vector<PrimitiveMesh> meshes;
        std::vector<RawMeshData> rawMeshes;
        glm::vec3 materialColor = glm::vec3(0.7f);  // Diffuse color from FBX material
        // Embedded textures extracted from FBX (decoded pixel data)
        struct EmbeddedTexture {
            unsigned char* data = nullptr;
            int width = 0;
            int height = 0;
            int channels = 0;
        };
        std::vector<EmbeddedTexture> textures;
        // Indices into `textures` for the first material's texture slots (-1 = none)
        int diffuseTextureIndex = -1;
        int normalTextureIndex = -1;
        // Per-scene-material texture slot mapping + diffuse colors, so each
        // sub-mesh can be rendered with ITS OWN material's texture.
        struct MaterialTextureMap {
            int diffuseIndex = -1;  // index into textures (-1 = none)
            int normalIndex = -1;   // index into textures (-1 = none)
        };
        std::vector<MaterialTextureMap> materialTextures;  // one entry per scene material
        std::vector<glm::vec3> materialColors;             // diffuse color per scene material

        // Node hierarchy (rest pose) — includes assimp's decomposed wrapper
        // nodes so animation channels bind by name. Empty for models loaded
        // before the node walk was added.
        std::vector<AnimNode> nodes;
        // Animation clips found in the file (0 for animation-less models,
        // 0 for animation-only files — those use LoadFBXAnimation).
        std::vector<AnimationClip> animations;
    };

} // namespace CoreEngine
