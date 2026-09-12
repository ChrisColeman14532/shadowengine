#pragma once

#include <GL/glew.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/types.h"

namespace CoreEngine {

    // Max bones per skinned mesh (also the size of the uBoneMatrices[]
    // uniform array in the skinning shaders). Mixamo rigs use ~22-65.
    static constexpr int MAX_SKIN_BONES = 64;

    // Per-vertex skinning influence: up to 4 bones. Bone indices are indices
    // into the owning mesh's bone list (RawMeshData::meshBones). Weights are
    // normalized to sum to 1. Stored separately from the base vertex data;
    // the mesh builder interleaves it as attributes 3 (indices) and 4
    // (weights) when the mesh is skinned.
    struct VertexSkin {
        uint8_t boneIndices[4] = {0, 0, 0, 0};
        float weights[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    };

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

    // One bone affecting a mesh, with its bind-time data.
    //
    // Inverse-bind matrix (IB) uses formula N (validated in
    // tools/test_skin_eval.cpp): IB = BrMesh^-1 where
    // BrMesh = WmeshInv * Wm(bone_rest). Anchoring skinning to the
    // MODEL file's exported rest pose keeps skinned vertices at their
    // authored positions when the animation's first frame differs from
    // the model's rest pose.
    struct MeshBone {
        std::string name;                     // node name in the model tree
        int modelNode = -1;                   // index into FBXModel::nodes (-1 = not found)
        glm::mat4 IB = glm::mat4(1.0f);       // inverse bind = (WmeshInv * Wm(bone_rest))^-1
        glm::mat4 restWorld = glm::mat4(1.0f); // Wm(bone_rest): bone rest world, model space
    };

    // Raw mesh data (used by FBX loader for merging)
    struct RawMeshData {
        std::string name;
        int materialIndex = 0;  // index into FBX material list (aiMesh::mMaterialIndex)
        std::vector<float> vertices;
        std::vector<uint32_t> indices;
        // Node this mesh hangs off in the source hierarchy (-1 = unknown).
        // Index into FBXModel::nodes; nodeName kept for logging/matching.
        int nodeIndex = -1;
        std::string nodeName;

        // ── Skinning (populated by the loader for rigged sub-meshes) ──
        std::vector<VertexSkin> skins;        // one per vertex; empty = not skinned
        std::vector<MeshBone> meshBones;      // aiMesh bone order; vertex indices refer to this
        glm::mat4 meshWorldRest = glm::mat4(1.0f);  // Wmesh: mesh node's rest world (model space)
        bool isSkinned() const { return !skins.empty(); }
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
