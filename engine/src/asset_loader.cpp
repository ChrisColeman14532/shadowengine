#include "core/asset_loader.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <algorithm>
#include <float.h>

namespace {

    // 8 floats per vertex: pos(3) + normal(3) + uv(2)
    static const int VERTEX_FLOAT_STRIDE = 8;

    void WriteVertex(float* out, const aiVector3D& pos, const aiVector3D& normal, const aiVector3D& uv) {
        out[0] = pos.x;
        out[1] = pos.y;
        out[2] = pos.z;
        out[3] = normal.x;
        out[4] = normal.y;
        out[5] = normal.z;
        out[6] = uv.x;
        out[7] = uv.y;
    }

    void ExtractMeshData(const aiMesh* aiMesh, std::vector<float>& vertices, std::vector<uint32_t>& indices) {
        size_t vertCount = aiMesh->mNumVertices;
        vertices.resize(vertCount * VERTEX_FLOAT_STRIDE);

        for (unsigned int i = 0; i < vertCount; ++i) {
            aiVector3D pos = aiMesh->mVertices[i];
            aiVector3D normal = aiMesh->HasNormals() ? aiMesh->mNormals[i] : aiVector3D(0, 1, 0);
            aiVector3D uv = aiMesh->mTextureCoords[0] ? aiMesh->mTextureCoords[0][i] : aiVector3D(0, 0, 0);
            WriteVertex(&vertices[i * VERTEX_FLOAT_STRIDE], pos, normal, uv);
        }

        for (unsigned int i = 0; i < aiMesh->mNumFaces; ++i) {
            const aiFace& face = aiMesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; ++j) {
                indices.push_back(static_cast<uint32_t>(face.mIndices[j]));
            }
        }
    }

    bool s_engineInited = false;
    std::vector<CoreEngine::FBXModel> s_allLoadedModels;

    void EnsureEngineInit() {
        if (!s_engineInited) {
            CoreEngine::Init();
            s_engineInited = true;
        }
    }

} // namespace

namespace AssetLoader {

    CoreEngine::FBXModel LoadFBX(const std::string& path) {
        CoreEngine::FBXModel model;
        model.filename = path;

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(
            path,
            aiProcess_Triangulate |
            aiProcess_GenNormals |
            aiProcess_JoinIdenticalVertices |
            aiProcess_GenUVCoords |
            aiProcess_FixInfacingNormals
        );

        if (!scene || !scene->mRootNode || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) {
            fprintf(stderr, "[AssetLoader] Failed to load FBX: %s\n", importer.GetErrorString());
            return model;
        }

        if (!scene->mMeshes || scene->mNumMeshes == 0) {
            fprintf(stderr, "[AssetLoader] FBX '%s' contains no meshes\n", path.c_str());
            return model;
        }

        printf("[AssetLoader] Loaded FBX '%s' with %u sub-meshes\n", path.c_str(), scene->mNumMeshes);

        for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
            const aiMesh* aiMesh = scene->mMeshes[i];

            std::vector<float> vertexData;
            std::vector<uint32_t> indices;
            ExtractMeshData(aiMesh, vertexData, indices);

            std::string name;
            if (aiMesh->mName.data != nullptr) {
                name = aiMesh->mName.data;
            } else {
                name = "mesh_" + std::to_string(i);
            }

            CoreEngine::RawMeshData rawMesh;
            rawMesh.name = name;
            rawMesh.vertices = std::move(vertexData);
            rawMesh.indices = std::move(indices);
            model.rawMeshes.push_back(std::move(rawMesh));
            model.meshes.push_back(CoreEngine::PrimitiveMesh{});
            model.meshes.back().name = name;
        }

        if (!model.rawMeshes.empty()) {
            model.success = true;
        }

        s_allLoadedModels.push_back(model);
        return model;
    }

    void DestroyFBX(CoreEngine::FBXModel& model) {
        if (!model.success) return;
        for (auto& mesh : model.meshes) {
            if (mesh.VAO) { glDeleteVertexArrays(1, &mesh.VAO); mesh.VAO = 0; }
            if (mesh.VBO) { glDeleteBuffers(1, &mesh.VBO); mesh.VBO = 0; }
            if (mesh.EBO) { glDeleteBuffers(1, &mesh.EBO); mesh.EBO = 0; }
            mesh.indexCount = 0;
        }
        model.success = false;
        model.meshes.clear();
    }

    void ClearAll() {
        for (auto& model : s_allLoadedModels) {
            DestroyFBX(model);
        }
        s_allLoadedModels.clear();
    }

    CoreEngine::PrimitiveMesh MergeFromModel(const CoreEngine::FBXModel& model) {
        CoreEngine::PrimitiveMesh merged;
        merged.name = "merged_fbx";

        size_t totalVerts = 0;
        size_t totalIndices = 0;
        for (const auto& raw : model.rawMeshes) {
            totalVerts += raw.vertices.size() / VERTEX_FLOAT_STRIDE;
            totalIndices += raw.indices.size();
        }

        if (totalVerts == 0 || totalIndices == 0) {
            return merged;
        }

        std::vector<float> allVertData;
        std::vector<uint32_t> allIndices;
        allVertData.reserve(totalVerts * VERTEX_FLOAT_STRIDE);
        allIndices.reserve(totalIndices);

        size_t vertOffset = 0;
        for (const auto& raw : model.rawMeshes) {
            allVertData.insert(allVertData.end(), raw.vertices.begin(), raw.vertices.end());
            for (const auto& idx : raw.indices) {
                allIndices.push_back(idx + static_cast<uint32_t>(vertOffset));
            }
            vertOffset += raw.vertices.size() / VERTEX_FLOAT_STRIDE;
        }

        merged.indexCount = static_cast<uint32_t>(allIndices.size());

        GLuint VAO, VBO, EBO;
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, allVertData.size() * sizeof(float), allVertData.data(), GL_STATIC_DRAW);

        // location 0: position (3 floats)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, VERTEX_FLOAT_STRIDE * sizeof(float), (void*)0);

        // location 1: normal (3 floats)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, VERTEX_FLOAT_STRIDE * sizeof(float), (void*)(3 * sizeof(float)));

        // location 2: uv (2 floats)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, VERTEX_FLOAT_STRIDE * sizeof(float), (void*)(6 * sizeof(float)));

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, allIndices.size() * sizeof(uint32_t), allIndices.data(), GL_STATIC_DRAW);

        glBindVertexArray(0);

        merged.VAO = VAO;
        merged.VBO = VBO;
        merged.EBO = EBO;

        return merged;
    }

    void ComputeModelAABB(const CoreEngine::FBXModel& model, glm::vec3& center, glm::vec3& extent) {
        glm::vec3 minVal(FLT_MAX, FLT_MAX, FLT_MAX);
        glm::vec3 maxVal(-FLT_MAX, -FLT_MAX, -FLT_MAX);

        for (const auto& raw : model.rawMeshes) {
            for (size_t i = 0; i < raw.vertices.size(); i += VERTEX_FLOAT_STRIDE) {
                glm::vec3 pos(raw.vertices[i], raw.vertices[i + 1], raw.vertices[i + 2]);
                for (int j = 0; j < 3; ++j) {
                    if (pos[j] < minVal[j]) minVal[j] = pos[j];
                    if (pos[j] > maxVal[j]) maxVal[j] = pos[j];
                }
            }
        }

        extent = (maxVal - minVal) * 0.5f;
        center = (maxVal + minVal) * 0.5f;
    }

} // namespace AssetLoader
