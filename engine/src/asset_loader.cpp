#include "core/asset_loader.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <float.h>

// stb_image wrapper — declarations only (implementation is in stb_image_loader.cpp)
#include "stb_image_loader.h"

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

    // Recompute smooth normals from the triangulated geometry: each vertex
    // gets the normalized sum of the face normals of the triangles it belongs
    // to. Vertices duplicated at authored hard edges (same position, different
    // normals) stay separate, so hard edges are preserved while the rest of
    // the surface shades smoothly.
    void SmoothRawNormals(CoreEngine::RawMeshData& raw) {
        if (raw.vertices.empty() || raw.indices.size() < 3) return;

        // Zero the normal components
        for (size_t i = 0; i < raw.vertices.size(); i += VERTEX_FLOAT_STRIDE) {
            raw.vertices[i + 3] = 0.0f;
            raw.vertices[i + 4] = 0.0f;
            raw.vertices[i + 5] = 0.0f;
        }

        // Accumulate (area-weighted) face normals into each corner vertex
        for (size_t i = 0; i + 2 < raw.indices.size(); i += 3) {
            const int ia = raw.indices[i], ib = raw.indices[i + 1], ic = raw.indices[i + 2];
            const float* pa = &raw.vertices[ia * VERTEX_FLOAT_STRIDE];
            const float* pb = &raw.vertices[ib * VERTEX_FLOAT_STRIDE];
            const float* pc = &raw.vertices[ic * VERTEX_FLOAT_STRIDE];

            const float ux = pb[0] - pa[0], uy = pb[1] - pa[1], uz = pb[2] - pa[2];
            const float vx = pc[0] - pa[0], vy = pc[1] - pa[1], vz = pc[2] - pa[2];
            const float nx = uy * vz - uz * vy;
            const float ny = uz * vx - ux * vz;
            const float nz = ux * vy - uy * vx;

            for (int ia2 : {ia, ib, ic}) {
                float* n = &raw.vertices[ia2 * VERTEX_FLOAT_STRIDE + 3];
                n[0] += nx;
                n[1] += ny;
                n[2] += nz;
            }
        }

        // Normalize (fallback to +Y for degenerate vertices)
        for (size_t i = 3; i < raw.vertices.size(); i += VERTEX_FLOAT_STRIDE) {
            const float x = raw.vertices[i], y = raw.vertices[i + 1], z = raw.vertices[i + 2];
            const float len = std::sqrt(x * x + y * y + z * z);
            if (len > 1e-8f) {
                raw.vertices[i]     = x / len;
                raw.vertices[i + 1] = y / len;
                raw.vertices[i + 2] = z / len;
            } else {
                raw.vertices[i] = 0.0f;
                raw.vertices[i + 1] = 1.0f;
                raw.vertices[i + 2] = 0.0f;
            }
        }
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

    // Find the embedded texture index referenced by a material slot, or -1.
    // Embedded textures are referenced by paths like "*0", "*1", ... where the
    // number is the zero-based index into aiScene::mTextures.
    int FindEmbeddedTextureIndex(const aiMaterial* mat, aiTextureType type, unsigned int texCount) {
        unsigned int count = mat->GetTextureCount(type);
        for (unsigned int i = 0; i < count; ++i) {
            aiString path;
            if (mat->GetTexture(type, i, &path) != AI_SUCCESS) continue;
            const char* c = path.C_Str();
            if (c && c[0] == '*') {
                int idx = atoi(c + 1);
                if (idx >= 0 && idx < static_cast<int>(texCount)) return idx;
            }
        }
        return -1;
    }

    bool s_engineInited = false;
    std::vector<CoreEngine::FBXModel> s_allLoadedModels;

    void EnsureEngineInit() {
        if (!s_engineInited) {
            CoreEngine::Init();
            s_engineInited = true;
        }
    }

    // Track embedded textures across all loaded models for cleanup
    static std::vector<CoreEngine::FBXModel::EmbeddedTexture*> s_allEmbeddedTextures;

    void CleanupEmbeddedTextures() {
        for (auto* etexPtr : s_allEmbeddedTextures) {
            stbi_image_free(etexPtr->data);
        }
        s_allEmbeddedTextures.clear();
    }

} // namespace

namespace AssetLoader {

    CoreEngine::FBXModel LoadFBX(const std::string& path, bool smoothNormals) {
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

            // Debug: per-sub-mesh stats — catches degenerate/empty geometry
            // (a sub-mesh with 0 indices is silently skipped by the renderer).
            {
                size_t vcount = vertexData.size() / VERTEX_FLOAT_STRIDE;
                if (vcount > 0) {
                    float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
                    float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;
                    for (size_t vi = 0; vi < vcount; ++vi) {
                        const float* p = &vertexData[vi * VERTEX_FLOAT_STRIDE];
                        minX = fminf(minX, p[0]); maxX = fmaxf(maxX, p[0]);
                        minY = fminf(minY, p[1]); maxY = fmaxf(maxY, p[1]);
                        minZ = fminf(minZ, p[2]); maxZ = fmaxf(maxZ, p[2]);
                    }
                    printf("[AssetLoader]   %s: %u verts, %u indices | bounds (%.3f,%.3f,%.3f)..(%.3f,%.3f,%.3f)\n",
                           name.c_str(), (unsigned)vcount, (unsigned)indices.size(),
                           minX, minY, minZ, maxX, maxY, maxZ);
                    if (indices.empty()) {
                        printf("[AssetLoader]   WARNING: %s has NO indices — it will NOT render!\n", name.c_str());
                    }
                } else {
                    printf("[AssetLoader]   WARNING: %s has NO vertices!\n", name.c_str());
                }
            }

            CoreEngine::RawMeshData rawMesh;
            rawMesh.name = name;
            rawMesh.materialIndex = static_cast<int>(aiMesh->mMaterialIndex);
            rawMesh.vertices = std::move(vertexData);
            rawMesh.indices = std::move(indices);
            if (smoothNormals) {
                SmoothRawNormals(rawMesh);
                printf("[AssetLoader]   %s: recomputed smooth normals (%u verts)\n",
                       name.c_str(), static_cast<unsigned int>(rawMesh.vertices.size() / VERTEX_FLOAT_STRIDE));
            }
            model.rawMeshes.push_back(std::move(rawMesh));
            model.meshes.push_back(CoreEngine::PrimitiveMesh{});
            model.meshes.back().name = name;
        }

        if (!model.rawMeshes.empty()) {
            model.success = true;
        }

        // Extract material diffuse color from the first material
        if (scene->mNumMaterials > 0 && scene->mMaterials) {
            aiMaterial* firstMat = scene->mMaterials[0];
            aiColor3D diffColor(0, 0, 0);
            if (firstMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffColor) == AI_SUCCESS) {
                model.materialColor = glm::vec3(diffColor.r, diffColor.g, diffColor.b);
                printf("[AssetLoader] Extracted material color: (%.2f, %.2f, %.2f)\n",
                       model.materialColor.r, model.materialColor.g, model.materialColor.b);
            }
        }

        // Debug: dump scene info
        printf("[AssetLoader] Scene has %u meshes, %u textures, %u materials\n",
               scene->mNumMeshes, scene->mNumTextures, scene->mNumMaterials);
        if (scene->mMaterials) {
            for (unsigned int m = 0; m < scene->mNumMaterials; ++m) {
                aiMaterial* mat = scene->mMaterials[m];
                aiString matName;
                mat->Get(AI_MATKEY_NAME, matName);
                printf("[AssetLoader]   Material %u: '%s'\n", m, matName.C_Str());

                // Get diffuse color directly (not texture)
                aiColor3D diffColor(0, 0, 0);
                if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffColor) == AI_SUCCESS) {
                    printf("[AssetLoader]     Diffuse color: (%.2f, %.2f, %.2f)\n", diffColor.r, diffColor.g, diffColor.b);
                }

                // Get specular color
                aiColor3D specColor(0, 0, 0);
                if (mat->Get(AI_MATKEY_COLOR_SPECULAR, specColor) == AI_SUCCESS) {
                    printf("[AssetLoader]     Specular color: (%.2f, %.2f, %.2f)\n", specColor.r, specColor.g, specColor.b);
                }

                // Check for any texture slots at all
                unsigned int texCount = mat->GetTextureCount(aiTextureType_DIFFUSE);
                printf("[AssetLoader]     Diffuse texture slots: %u\n", texCount);
                texCount = mat->GetTextureCount(aiTextureType_BASE_COLOR);
                printf("[AssetLoader]     BaseColor texture slots: %u\n", texCount);

                // Dump all texture paths for ALL texture types
                for (aiTextureType type = aiTextureType_DIFFUSE; type <= aiTextureType_HEIGHT; type = (aiTextureType)(type + 1)) {
                    unsigned int count = mat->GetTextureCount(type);
                    if (count > 0) {
                        aiString path;
                        if (mat->GetTexture(type, 0, &path) == AI_SUCCESS) {
                            printf("[AssetLoader]     Texture type %d path: '%s'\n", type, path.C_Str());
                        }
                    }
                }

            }
        }

        // Map EACH material's texture slots to embedded texture indices and
        // record its diffuse color. (Must run before we rely on model.textures;
        // uses the aiScene directly.) Multi-material models need per-material
        // mappings so each sub-mesh samples its own texture.
        if (scene->mNumMaterials > 0 && scene->mMaterials) {
            for (unsigned int m = 0; m < scene->mNumMaterials; ++m) {
                const aiMaterial* mat = scene->mMaterials[m];

                CoreEngine::FBXModel::MaterialTextureMap map;
                map.diffuseIndex = FindEmbeddedTextureIndex(mat, aiTextureType_DIFFUSE, scene->mNumTextures);
                if (map.diffuseIndex < 0) {
                    map.diffuseIndex = FindEmbeddedTextureIndex(mat, aiTextureType_BASE_COLOR, scene->mNumTextures);
                }
                map.normalIndex = FindEmbeddedTextureIndex(mat, aiTextureType_NORMALS, scene->mNumTextures);
                model.materialTextures.push_back(map);

                aiColor3D diffColor(1, 1, 1);
                if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffColor) == AI_SUCCESS) {
                    model.materialColors.emplace_back(diffColor.r, diffColor.g, diffColor.b);
                } else {
                    model.materialColors.emplace_back(1.0f, 1.0f, 1.0f);
                }
                printf("[AssetLoader]   Material %u: diffuseTex=%d normalTex=%d\n",
                       m, map.diffuseIndex, map.normalIndex);
            }

            // Keep top-level fields in sync with material 0 (legacy behavior)
            model.diffuseTextureIndex = model.materialTextures[0].diffuseIndex;
            model.normalTextureIndex = model.materialTextures[0].normalIndex;
        }

        // Extract embedded textures from FBX
        if (scene->mNumTextures > 0 && scene->mTextures) {
            printf("[AssetLoader] Found %u embedded texture(s) in FBX\n", scene->mNumTextures);

            for (unsigned int i = 0; i < scene->mNumTextures; ++i) {
                const aiTexture* aiTex = scene->mTextures[i];

                // NOTE: a placeholder is pushed for failed/empty textures too, so
                // model.textures indices stay aligned with scene->mTextures indices
                // (material slot references point at those indices).
                CoreEngine::FBXModel::EmbeddedTexture etex = {};

                if (!aiTex->pcData) {
                    printf("[AssetLoader]   Texture %u: no embedded data\n", i);
                    model.textures.push_back(etex);
                    continue;
                }

                if (aiTex->mHeight > 0) {
                    // Uncompressed: pcData points to raw pixel data
                    // mWidth = width, mHeight = height
                    // Channel order is given by achFormatHint, e.g. "rgba8888"
                    // or "argb8888" (first 4 chars = channel order).
                    size_t pixelCount = (size_t)aiTex->mWidth * aiTex->mHeight;
                    size_t byteCount = pixelCount * 4;  // 4 bytes per pixel

                    // Allocate output buffer (RGBA for OpenGL) with malloc so
                    // stbi_image_free (used in cleanup, which calls free) matches.
                    etex.data = static_cast<unsigned char*>(malloc(byteCount));
                    if (!etex.data) {
                        printf("[AssetLoader]   Texture %u: out of memory\n", i);
                        model.textures.push_back(etex);
                        continue;
                    }
                    // pcData is aiTextureElement* for uncompressed, cast to raw bytes
                    const unsigned char* src = reinterpret_cast<const unsigned char*>(aiTex->pcData);

                    const std::string hint(aiTex->achFormatHint);
                    const bool isArgb = (hint.compare(0, 4, "argb") == 0);
                    if (isArgb) {
                        // Convert ARGB -> RGBA
                        for (size_t j = 0; j < pixelCount; ++j) {
                            const unsigned char* srcPixel = &src[j * 4];
                            unsigned char* dst = &etex.data[j * 4];
                            dst[0] = srcPixel[1];  // R
                            dst[1] = srcPixel[2];  // G
                            dst[2] = srcPixel[3];  // B
                            dst[3] = srcPixel[0];  // A
                        }
                    } else if (hint.compare(0, 4, "rgba") == 0) {
                        // Already RGBA — copy as-is
                        memcpy(etex.data, src, byteCount);
                    } else {
                        // Unknown channel order — copy as-is (best effort)
                        printf("[AssetLoader]   Texture %u: unknown format hint '%s', assuming RGBA\n",
                               i, aiTex->achFormatHint);
                        memcpy(etex.data, src, byteCount);
                    }
                    etex.width = aiTex->mWidth;
                    etex.height = aiTex->mHeight;
                    etex.channels = 4;
                    printf("[AssetLoader]   Texture %u: uncompressed %dx%d (%s)\n",
                           i, aiTex->mWidth, aiTex->mHeight, aiTex->achFormatHint);

                } else {
                    // Compressed: pcData points to compressed bytes (JPEG, PNG, etc.)
                    // mWidth = byte count, achFormatHint = file extension
                    size_t compLen = (size_t)aiTex->mWidth;
                    std::vector<unsigned char> compData(compLen);
                    memcpy(compData.data(), aiTex->pcData, compLen);

                    // Try to decode with stb_image.
                    // stb returns images with top-left origin; OpenGL expects
                    // bottom-left origin (matching CoreEngine::LoadTexture), so
                    // flip vertically during load.
                    stbi_set_flip_vertically_on_load(true);
                    int w = 0, h = 0, comp = 0;
                    unsigned char* decoded = stbi_load_from_memory(
                        compData.data(), (int)compLen,
                        &w, &h, &comp, 4
                    );
                    stbi_set_flip_vertically_on_load(false);  // restore default

                    if (!decoded) {
                        printf("[AssetLoader]   Texture %u: compressed decode failed (%s)\n",
                               i, stbi_failure_reason() ? stbi_failure_reason() : "unknown");
                        model.textures.push_back(etex);  // placeholder, keeps indices aligned
                        continue;
                    }

                    etex.data = decoded;
                    etex.width = w;
                    etex.height = h;
                    etex.channels = 4;
                    printf("[AssetLoader]   Texture %u: compressed %dx%d (%s)\n",
                           i, w, h, aiTex->achFormatHint);
                }

                model.textures.push_back(etex);
                if (etex.data) {
                    s_allEmbeddedTextures.push_back(&model.textures.back());
                }
            }
        } else {
            printf("[AssetLoader] WARNING: No embedded textures found (mNumTextures=%u)\n", scene->mNumTextures);
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
        CleanupEmbeddedTextures();
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

        // Store the model's AABB (mesh-local space) so the editor can draw a
        // correctly sized/positioned selection bounding box.
        glm::vec3 aabbCenter(0.0f, 0.0f, 0.0f);
        glm::vec3 aabbExtent(0.0f, 0.0f, 0.0f);
        ComputeModelAABB(model, aabbCenter, aabbExtent);
        merged.center = CoreEngine::Vector3(aabbCenter);
        merged.halfExtent = CoreEngine::Vector3(aabbExtent);

        return merged;
    }

    // Build a GPU mesh from a SINGLE sub-mesh (instead of merging the whole
    // model). Used for multi-material models where each sub-mesh is rendered
    // with its own material/texture.
    CoreEngine::PrimitiveMesh MergeSubMesh(const CoreEngine::FBXModel& model, size_t submeshIndex) {
        CoreEngine::PrimitiveMesh merged;
        if (submeshIndex >= model.rawMeshes.size()) return merged;
        const CoreEngine::RawMeshData& raw = model.rawMeshes[submeshIndex];
        if (raw.vertices.empty() || raw.indices.empty()) return merged;

        merged.name = raw.name;
        merged.indexCount = static_cast<uint32_t>(raw.indices.size());

        // Store this sub-mesh's AABB (mesh-local space) so the editor can draw
        // a correctly sized/positioned selection bounding box.
        glm::vec3 minVal(FLT_MAX, FLT_MAX, FLT_MAX);
        glm::vec3 maxVal(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (size_t i = 0; i < raw.vertices.size(); i += VERTEX_FLOAT_STRIDE) {
            glm::vec3 pos(raw.vertices[i], raw.vertices[i + 1], raw.vertices[i + 2]);
            for (int j = 0; j < 3; ++j) {
                if (pos[j] < minVal[j]) minVal[j] = pos[j];
                if (pos[j] > maxVal[j]) maxVal[j] = pos[j];
            }
        }
        merged.center = CoreEngine::Vector3((maxVal + minVal) * 0.5f);
        merged.halfExtent = CoreEngine::Vector3((maxVal - minVal) * 0.5f);

        GLuint VAO, VBO, EBO;
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, raw.vertices.size() * sizeof(float), raw.vertices.data(), GL_STATIC_DRAW);

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
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, raw.indices.size() * sizeof(uint32_t), raw.indices.data(), GL_STATIC_DRAW);

        glBindVertexArray(0);

        merged.VAO = VAO;
        merged.VBO = VBO;
        merged.EBO = EBO;

        return merged;
    }

} // namespace AssetLoader
