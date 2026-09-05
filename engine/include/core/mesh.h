#pragma once

#include <GL/glew.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/types.h"

namespace CoreEngine {

    // Vertex layout: position(3) + normal(3) + uv(2) = 8 floats
    struct VertexPositionNormalUV {
        float pos[3];
        float normal[3];
        float uv[2];
        static int stride() { return sizeof(VertexPositionNormalUV); }
    };

    // Mesh data (produced by asset loader or primitives)
    struct PrimitiveMesh {
        std::string name;
        GLuint VAO = 0;
        GLuint VBO = 0;
        GLuint EBO = 0;
        uint32_t indexCount = 0;
        Vector3 halfExtent = {1.0f, 1.0f, 1.0f};  // AABB half-extents for bounding wireframe
        Vector3 center = {0.0f, 0.0f, 0.0f};      // AABB center in mesh-local space
    };

    // Raw mesh data (used by FBX loader for merging)
    struct RawMeshData {
        std::string name;
        int materialIndex = 0;  // index into FBX material list (aiMesh::mMaterialIndex)
        std::vector<float> vertices;
        std::vector<uint32_t> indices;
    };

    // Shared GPU mesh handle. When the last reference is dropped, the
    // VAO/VBO/EBO are deleted. Scene objects and primitive templates
    // can safely share the same GPU mesh.
    using MeshPtr = std::shared_ptr<PrimitiveMesh>;
    MeshPtr CreateMesh(PrimitiveMesh mesh);

    // Mesh primitives (cube, plane — useful in editor/for testing)
    void InitPrimitiveMeshes();
    PrimitiveMesh CreateBox(const Vector3& size);
    PrimitiveMesh CreatePlane(float width=1.0f, float height=1.0f);
    void DestroyMesh(PrimitiveMesh& mesh);

    // Internal helpers (used by editor)
    MeshPtr GetPrimitiveMesh(const char* name);

} // namespace CoreEngine
