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

    // aiMatrix4x4 (members a1..d4: row letter = a/b/c/d, digit = column) →
    // glm::mat4 (column-major storage: out[col][row])
    glm::mat4 ToGlmMat4(const aiMatrix4x4& m) {
        glm::mat4 out;
        out[0][0] = m.a1; out[0][1] = m.b1; out[0][2] = m.c1; out[0][3] = m.d1;
        out[1][0] = m.a2; out[1][1] = m.b2; out[1][2] = m.c2; out[1][3] = m.d2;
        out[2][0] = m.a3; out[2][1] = m.b3; out[2][2] = m.c3; out[2][3] = m.d3;
        out[3][0] = m.a4; out[3][1] = m.b4; out[3][2] = m.c4; out[3][3] = m.d4;
        return out;
    }

    // Recursively add nodes to the flat tree (pre-order: parents always get
    // lower indices than their children, so world transforms can be computed
    // in a single forward pass).
    int AddNodeToTree(const aiNode* n, int parent, std::vector<CoreEngine::AnimNode>& out) {
        int idx = (int)out.size();
        CoreEngine::AnimNode an;
        an.name = n->mName.C_Str();
        an.parentIndex = parent;
        an.local = ToGlmMat4(n->mTransformation);
        out.push_back(std::move(an));
        for (unsigned i = 0; i < n->mNumMeshes; ++i)
            out[idx].meshIndices.push_back((int)n->mMeshes[i]);
        for (unsigned i = 0; i < n->mNumChildren; ++i) {
            int child = AddNodeToTree(n->mChildren[i], idx, out);
            out[idx].childIndices.push_back(child);
        }
        return idx;
    }

    void ComputeNodeWorlds(std::vector<CoreEngine::AnimNode>& nodes) {
        for (size_t i = 0; i < nodes.size(); ++i) {
            const glm::mat4 parentWorld = (nodes[i].parentIndex >= 0)
                ? nodes[nodes[i].parentIndex].world : glm::mat4(1.0f);
            nodes[i].world = parentWorld * nodes[i].local;
        }
    }

    // Convert all aiAnimation clips into AnimationClips (key times in seconds).
    // Channels keep assimp's decomposed wrapper node names
    // (e.g. "mixamorig:Hips_$AssimpFbx$_Rotation") so they match the node
    // tree built from a character FBX parsed the same way.
    void ExtractAnimations(const aiScene* scene, std::vector<CoreEngine::AnimationClip>& out) {
        for (unsigned a = 0; a < scene->mNumAnimations; ++a) {
            const aiAnimation* an = scene->mAnimations[a];
            CoreEngine::AnimationClip clip;
            clip.name = (an->mName.length > 0) ? an->mName.C_Str()
                                               : ("animation_" + std::to_string(a));
            double tps = (an->mTicksPerSecond > 0.0) ? an->mTicksPerSecond : 30.0;
            clip.fps = (float)tps;
            clip.duration = (an->mDuration > 0.0) ? (float)(an->mDuration / tps) : 0.0f;

            for (unsigned c = 0; c < an->mNumChannels; ++c) {
                const aiNodeAnim* ch = an->mChannels[c];
                CoreEngine::NodeAnimTrack track;
                track.nodeName = ch->mNodeName.C_Str();

                for (unsigned k = 0; k < ch->mNumPositionKeys; ++k) {
                    const aiVectorKey& key = ch->mPositionKeys[k];
                    track.position.push_back({(float)(key.mTime / tps),
                        glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z)});
                }
                for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
                    const aiQuatKey& key = ch->mRotationKeys[k];
                    // aiQuaternion is (x,y,z,w); glm::quat is (w,x,y,z)
                    track.rotation.push_back({(float)(key.mTime / tps),
                        glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z)});
                }
                for (unsigned k = 0; k < ch->mNumScalingKeys; ++k) {
                    const aiVectorKey& key = ch->mScalingKeys[k];
                    track.scale.push_back({(float)(key.mTime / tps),
                        glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z)});
                }

                if (track.position.empty() && track.rotation.empty() && track.scale.empty())
                    continue;
                clip.tracks.push_back(std::move(track));
            }

            // mDuration can be -1 on some exports; derive it from the keys.
            if (clip.duration <= 0.0f) {
                float maxT = 0.0f;
                for (const auto& tr : clip.tracks) {
                    if (!tr.position.empty()) maxT = fmaxf(maxT, tr.position.back().time);
                    if (!tr.rotation.empty()) maxT = fmaxf(maxT, tr.rotation.back().time);
                    if (!tr.scale.empty()) maxT = fmaxf(maxT, tr.scale.back().time);
                }
                clip.duration = maxT;
            }
            out.push_back(std::move(clip));
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
            if (scene->mNumAnimations > 0) {
                fprintf(stderr, "[AssetLoader] FBX '%s' contains %u animation(s) but no meshes — "
                        "use File → Load Animation instead\n", path.c_str(), scene->mNumAnimations);
            } else {
                fprintf(stderr, "[AssetLoader] FBX '%s' contains no meshes\n", path.c_str());
            }
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

        // ── Node hierarchy (rest pose) + embedded animations ─────────
        if (scene->mRootNode) {
            model.nodes.reserve(256);
            AddNodeToTree(scene->mRootNode, -1, model.nodes);
            ComputeNodeWorlds(model.nodes);
            // Attach each mesh to the node that references it
            for (size_t ni = 0; ni < model.nodes.size(); ++ni) {
                for (int mi : model.nodes[ni].meshIndices) {
                    if (mi >= 0 && mi < (int)model.rawMeshes.size()) {
                        model.rawMeshes[mi].nodeIndex = (int)ni;
                        model.rawMeshes[mi].nodeName = model.nodes[ni].name;
                    }
                }
            }

            // ── Skinning extraction (needs node worlds) ──────────────
            // Per-vertex bone indices/weights + per-bone inverse-bind
            // matrices (formula N, see mesh.h). Meshes with more than
            // MAX_SKIN_BONES influences render static (rest pose).
            for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
                const aiMesh* am = scene->mMeshes[i];
                if (am->mNumBones == 0) continue;
                CoreEngine::RawMeshData& raw = model.rawMeshes[i];
                if (raw.nodeIndex < 0 || raw.nodeIndex >= (int)model.nodes.size()) {
                    printf("[AssetLoader]   WARNING: skinned mesh '%s' not attached to a node — rendered static\n", raw.name.c_str());
                    continue;
                }
                if (am->mNumBones > CoreEngine::MAX_SKIN_BONES) {
                    printf("[AssetLoader]   WARNING: mesh '%s' has %u bones (max %d) — rendered static\n",
                           raw.name.c_str(), am->mNumBones, CoreEngine::MAX_SKIN_BONES);
                    continue;
                }

                const glm::mat4 Wmesh = model.nodes[raw.nodeIndex].world;
                const glm::mat4 WmeshInv = glm::inverse(Wmesh);
                raw.meshWorldRest = Wmesh;
                raw.skins.assign(am->mNumVertices, CoreEngine::VertexSkin());

                // Per-vertex (bone, weight) accumulation, then sort/normalize.
                std::vector<std::vector<std::pair<unsigned int, float>>> perVert(am->mNumVertices);
                for (unsigned b = 0; b < am->mNumBones; ++b) {
                    const aiBone* bone = am->mBones[b];
                    CoreEngine::MeshBone mb;
                    mb.name = bone->mName.C_Str();
                    mb.modelNode = -1;
                    for (size_t ni = 0; ni < model.nodes.size(); ++ni) {
                        if (model.nodes[ni].name == mb.name) { mb.modelNode = (int)ni; break; }
                    }
                    if (mb.modelNode < 0) {
                        printf("[AssetLoader]   WARNING: bone '%s' of mesh '%s' not found in node tree — bone held at identity\n",
                               mb.name.c_str(), raw.name.c_str());
                    } else {
                        mb.restWorld = model.nodes[mb.modelNode].world;
                        mb.IB = glm::inverse(WmeshInv * mb.restWorld);
                    }
                    raw.meshBones.push_back(std::move(mb));

                    for (unsigned w = 0; w < bone->mNumWeights; ++w) {
                        const aiVertexWeight& vw = bone->mWeights[w];
                        if (vw.mVertexId < am->mNumVertices)
                            perVert[vw.mVertexId].push_back({b, (float)vw.mWeight});
                    }
                }

                for (size_t v = 0; v < am->mNumVertices; ++v) {
                    auto& list = perVert[v];
                    std::sort(list.begin(), list.end(),
                              [](const auto& a, const auto& b) { return a.second > b.second; });
                    size_t n = std::min<size_t>(list.size(), 4);
                    float kept = 0.0f;
                    for (size_t k = 0; k < n; ++k) kept += list[k].second;
                    if (kept > 1e-8f) {
                        auto& sk = raw.skins[v];
                        for (size_t k = 0; k < n; ++k) {
                            sk.boneIndices[k] = (uint8_t)list[k].first;
                            sk.weights[k] = list[k].second / kept;
                        }
                    }  // else: keep default (full weight on bone 0)
                }
                printf("[AssetLoader]   %s: skinned (%u bones, %u vertices)\n",
                       raw.name.c_str(), am->mNumBones, (unsigned)am->mNumVertices);
            }
        }
        ExtractAnimations(scene, model.animations);
        for (const auto& c : model.animations)
            printf("[AssetLoader] Animation clip '%s': %.3fs, %zu node tracks\n",
                   c.name.c_str(), c.duration, c.tracks.size());

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

    // Load an animation file (e.g. a mixamo download: skeleton + keyframes,
    // no meshes). Returns the file's OWN rest node tree plus its clips —
    // the Animator evaluates clips on this tree, where every channel binds
    // by exact name.
    CoreEngine::AnimationFile LoadFBXAnimation(const std::string& path) {
        CoreEngine::AnimationFile file;
        file.filename = path;

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenUVCoords);
        if (!scene) {
            fprintf(stderr, "[AssetLoader] Failed to load animation FBX '%s': %s\n",
                    path.c_str(), importer.GetErrorString());
            return file;
        }
        if (scene->mNumAnimations == 0) {
            fprintf(stderr, "[AssetLoader] FBX '%s' contains no animations\n", path.c_str());
            return file;
        }

        // Rest node tree, parsed the SAME way as model files (same flags,
        // same wrapper decomposition) so channel names match node names.
        if (scene->mRootNode) {
            file.nodes.reserve(256);
            AddNodeToTree(scene->mRootNode, -1, file.nodes);
            ComputeNodeWorlds(file.nodes);
        }
        ExtractAnimations(scene, file.clips);
        file.success = !file.clips.empty();
        for (const auto& c : file.clips)
            printf("[AssetLoader] Animation clip '%s': %.3fs, %zu node tracks\n",
                   c.name.c_str(), c.duration, c.tracks.size());
        return file;
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

        // The merged layout must be uniform across sub-meshes: if ANY of
        // them is skinned, all vertices carry the 16-float skinned layout
        // (non-skinned parts get zero weights — uSkinCount decides usage).
        bool anySkinned = false;
        for (const auto& raw : model.rawMeshes) anySkinned |= raw.isSkinned();

        std::vector<float> allVertData;
        std::vector<uint32_t> allIndices;
        const int strideFloats = anySkinned ? 16 : VERTEX_FLOAT_STRIDE;
        allVertData.reserve(totalVerts * strideFloats);
        allIndices.reserve(totalIndices);

        size_t vertOffset = 0;
        for (const auto& raw : model.rawMeshes) {
            const size_t vertCount = raw.vertices.size() / VERTEX_FLOAT_STRIDE;
            const bool skinned = anySkinned && raw.isSkinned();
            for (size_t v = 0; v < vertCount; ++v) {
                const float* base = &raw.vertices[v * VERTEX_FLOAT_STRIDE];
                allVertData.insert(allVertData.end(), base, base + VERTEX_FLOAT_STRIDE);
                if (anySkinned) {
                    if (skinned) {
                        const CoreEngine::VertexSkin& sk = raw.skins[v];
                        allVertData.push_back((float)sk.boneIndices[0]);
                        allVertData.push_back((float)sk.boneIndices[1]);
                        allVertData.push_back((float)sk.boneIndices[2]);
                        allVertData.push_back((float)sk.boneIndices[3]);
                        for (int k = 0; k < 4; ++k) allVertData.push_back(sk.weights[k]);
                    } else {
                        // Static part inside a merged skinned model: bind
                        // fully to bone 0 of its own (empty) palette — the
                        // shader never reads it (uSkinCount is per-object).
                        allVertData.insert(allVertData.end(), {0, 0, 0, 0, 1, 0, 0, 0});
                    }
                }
            }
            for (const auto& idx : raw.indices) {
                allIndices.push_back(idx + static_cast<uint32_t>(vertOffset));
            }
            vertOffset += vertCount;
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
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, strideFloats * sizeof(float), (void*)0);

        // location 1: normal (3 floats)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, strideFloats * sizeof(float), (void*)(3 * sizeof(float)));

        // location 2: uv (2 floats)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, strideFloats * sizeof(float), (void*)(6 * sizeof(float)));

        if (anySkinned) {
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, strideFloats * sizeof(float), (void*)(8 * sizeof(float)));
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, strideFloats * sizeof(float), (void*)(12 * sizeof(float)));
        }

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

    // ── GPU vertex layout ─────────────────────────────────────────────
    // Base (all meshes): 8 floats  pos(3) + normal(3) + uv(2)
    // Skinned:         16 floats  + boneIndices(4) + boneWeights(4)
    // Attribute locations: 0=pos 1=normal 2=uv 3=boneIndices 4=boneWeights
    // (3 and 4 are only enabled for skinned meshes; the shaders read them
    // only when uSkinCount > 0, so non-skinned VAOs are unaffected).
    namespace {
        struct GpuVertexData {
            std::vector<float> floats;   // interleaved vertex data
            int strideFloats = 8;
            bool skinned = false;
        };
        // Build the interleaved GPU vertex buffer for one raw mesh.
        GpuVertexData BuildGpuVertexData(const CoreEngine::RawMeshData& raw) {
            GpuVertexData out;
            const size_t vertCount = raw.vertices.size() / VERTEX_FLOAT_STRIDE;
            out.skinned = raw.isSkinned();
            out.strideFloats = out.skinned ? 16 : VERTEX_FLOAT_STRIDE;
            out.floats.reserve(vertCount * out.strideFloats);
            for (size_t v = 0; v < vertCount; ++v) {
                const float* base = &raw.vertices[v * VERTEX_FLOAT_STRIDE];
                out.floats.insert(out.floats.end(), base, base + VERTEX_FLOAT_STRIDE);
                if (out.skinned) {
                    const CoreEngine::VertexSkin& sk = raw.skins[v];
                    out.floats.push_back((float)sk.boneIndices[0]);
                    out.floats.push_back((float)sk.boneIndices[1]);
                    out.floats.push_back((float)sk.boneIndices[2]);
                    out.floats.push_back((float)sk.boneIndices[3]);
                    for (int k = 0; k < 4; ++k) out.floats.push_back(sk.weights[k]);
                }
            }
            return out;
        }
        // Bind the standard attribute layout against `vbo` (currently bound
        // to the array buffer of the VAO being built).
        void BindStandardAttributes(const GpuVertexData& vd) {
            const GLsizei stride = (GLsizei)(vd.strideFloats * sizeof(float));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)(0 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
            if (vd.skinned) {
                glEnableVertexAttribArray(3);
                glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
                glEnableVertexAttribArray(4);
                glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(12 * sizeof(float)));
            }
        }
    } // namespace

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

        GpuVertexData vd = BuildGpuVertexData(raw);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vd.floats.size() * sizeof(float), vd.floats.data(), GL_STATIC_DRAW);
        BindStandardAttributes(vd);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, raw.indices.size() * sizeof(uint32_t), raw.indices.data(), GL_STATIC_DRAW);

        glBindVertexArray(0);

        merged.VAO = VAO;
        merged.VBO = VBO;
        merged.EBO = EBO;

        return merged;
    }

} // namespace AssetLoader
