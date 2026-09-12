#include "editor_asset.h"
#include "editor_state.h"
#include "core/engine.h"
#include "core/asset_loader.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace Editor {

    void LoadFBXAtPath(const std::string& path) {
        AssetLoader::ClearAll();
        // NOTE: imports are ADDITIVE — existing scene objects (and their
        // animation registrations, if any) are left untouched.

        CoreEngine::FBXModel model = AssetLoader::LoadFBX(path, g_smoothNormals);
        if (!model.success) {
            printf("[Editor] Failed to load FBX '%s'\n", path.c_str());
            return;
        }

        auto* win = CoreEngine::GetWindow();
        int w = 1280, h = 720;
        if (win) glfwGetFramebufferSize(win, &w, &h);
        if (w < 1) w = 1280;
        if (h < 1) h = 720;

        // NOTE: we deliberately do NOT clear the scene here. Importing a model
        // ADDS its parts to the scene; existing objects (planes, cubes, models
        // imported earlier) are left untouched. AssetLoader::ClearAll() above
        // only resets the FBX asset cache, not the scene.

        // Base PBR settings shared by all parts of the model
        auto baseMat = CoreEngine::CreateDefaultMaterial();
        baseMat.roughness = 0.8f;
        baseMat.metallic = 0.2f;
        baseMat.useMaterial = true;

        printf("[Editor] Model has %zu sub-mesh(es), %zu texture(s)\n",
               model.rawMeshes.size(), model.textures.size());

        // One scene object per sub-mesh, each using ITS OWN material's texture
        // mapping (set by LoadFBX). Merging everything into a single mesh +
        // single texture makes sub-meshes with different UV layouts (e.g. a
        // head with its own texture) sample the wrong map.
        const bool hasMapping = !model.materialTextures.empty();

        // ── Model root (parent) node ───────────────────────────────────────
        // Everything the FBX contains becomes a CHILD of this transform
        // group, so moving/rotating/scaling the root in the Inspector moves
        // the whole model (each part keeps its own local transform).
        std::string baseName = path;
        {
            size_t slash = baseName.find_last_of("/\\");
            if (slash != std::string::npos) baseName = baseName.substr(slash + 1);
            size_t dot = baseName.find_last_of('.');
            if (dot != std::string::npos) baseName = baseName.substr(0, dot);
        }
        if (baseName.empty() || baseName == ".") baseName = "model";

        // The file's root node may carry the exported model's overall
        // transform. Parts hold their node transforms RELATIVE to it, so
        // the root object itself starts at the identity.
        glm::mat4 fileRoot = glm::mat4(1.0f);
        if (!model.nodes.empty()) fileRoot = model.nodes[0].world;
        glm::mat4 fileRootInv = glm::inverse(fileRoot);

        g_lastModelObjectIds.clear();
        // NOTE: AddToScene returns an id on purpose — a SceneObject& would
        // dangle after the first part below reallocates the scene vector.
        uint32_t modelRootId = CoreEngine::AddToScene(baseName, nullptr, baseMat);
        if (CoreEngine::SceneObject* rootObj = CoreEngine::GetSceneObject(modelRootId))
            rootObj->isGroup = true;
        g_lastModelObjectIds.push_back(modelRootId);

        // Camera framing happens in FILE space: each part now carries its
        // node transform, so frame using every part's local AABB transformed
        // by its local matrix (the old raw-vertex AABB is mesh-local and
        // ignores the node hierarchy).
        glm::vec3 fileMin(FLT_MAX, FLT_MAX, FLT_MAX);
        glm::vec3 fileMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        bool haveAnyGeo = false;

        for (size_t i = 0; i < model.rawMeshes.size(); ++i) {
            const auto& raw = model.rawMeshes[i];

            int matIdx = raw.materialIndex;
            if (hasMapping && (matIdx < 0 || matIdx >= (int)model.materialTextures.size()))
                matIdx = 0;
            if (!hasMapping) matIdx = 0;

            CoreEngine::Material partMat = baseMat;
            partMat.name = raw.name + "_material";
            if (matIdx >= 0 && matIdx < (int)model.materialColors.size()) {
                partMat.baseColor = model.materialColors[matIdx];
            } else {
                partMat.baseColor = model.materialColor;
            }

            // Pick this material's embedded textures (fall back to texture 0
            // when the FBX has no material-to-texture mapping).
            int diffuseIdx = -1, normalIdx = -1;
            if (hasMapping) {
                diffuseIdx = model.materialTextures[matIdx].diffuseIndex;
                normalIdx  = model.materialTextures[matIdx].normalIndex;
            } else if (!model.textures.empty()) {
                diffuseIdx = 0;
            }

            if (diffuseIdx >= 0 && diffuseIdx < (int)model.textures.size()) {
                const auto& tex = model.textures[diffuseIdx];
                if (tex.data) {
                    partMat.diffuseTexture = CoreEngine::LoadTextureFromMemory(
                        tex.data, tex.width, tex.height, tex.channels);
                    printf("[Editor]   %s (mat %d): diffuse texture %d %dx%d (gl id %u)\n",
                           raw.name.c_str(), matIdx, diffuseIdx, tex.width, tex.height,
                           partMat.diffuseTexture ? partMat.diffuseTexture->id : 0u);
                } else {
                    printf("[Editor]   %s (mat %d): diffuse texture %d has no data!\n",
                           raw.name.c_str(), matIdx, diffuseIdx);
                }
            }
            if (normalIdx >= 0 && normalIdx < (int)model.textures.size()) {
                const auto& tex = model.textures[normalIdx];
                if (tex.data) {
                    partMat.normalTexture = CoreEngine::LoadTextureFromMemory(
                        tex.data, tex.width, tex.height, tex.channels);
                    printf("[Editor]   %s (mat %d): normal texture %d %dx%d\n",
                           raw.name.c_str(), matIdx, normalIdx, tex.width, tex.height);
                }
            }

            auto partMesh = AssetLoader::MergeSubMesh(model, i);
            uint32_t addedId = CoreEngine::AddToScene(
                raw.name, CoreEngine::CreateMesh(std::move(partMesh)), partMat);
            CoreEngine::SceneObject* added = CoreEngine::GetSceneObject(addedId);
            g_lastModelObjectIds.push_back(addedId);

            // Local transform = this part's FBX node world, relative to
            // the file's root node, decomposed into the engine's TRS.
            // (Before hierarchy support, node transforms were silently
            // dropped and every part rendered at the origin.)
            glm::mat4 partLocal = glm::mat4(1.0f);
            if (raw.nodeIndex >= 0 && (size_t)raw.nodeIndex < model.nodes.size()) {
                partLocal = model.nodes[raw.nodeIndex].world * fileRootInv;
                CoreEngine::Vector3 p, r, s;
                float err = 0.0f;
                if (CoreEngine::DecomposeTRS(partLocal, p, r, s, err)) {
                    added->position = p;
                    added->rotation = r;
                    added->scale    = s;
                    if (err > 1e-3f) {
                        ConsoleLog(("WARNING: " + raw.name +
                            " node transform has shear; TRS approximation err=") +
                            std::to_string(err));
                    }
                } else {
                    ConsoleLog("WARNING: " + raw.name +
                        " has a degenerate node transform; left at origin");
                }
            }
            added->parentId = modelRootId;

            // File-space AABB contribution (local AABB × local matrix)
            {
                size_t vstride = raw.isSkinned() ? 16 : 8;  // floats per vertex
                if (raw.vertices.size() >= vstride) {
                    glm::vec3 lmin(FLT_MAX), lmax(-FLT_MAX);
                    for (size_t v = 0; v + vstride <= raw.vertices.size(); v += vstride) {
                        glm::vec3 vp(raw.vertices[v], raw.vertices[v + 1], raw.vertices[v + 2]);
                        lmin.x = fminf(lmin.x, vp.x); lmin.y = fminf(lmin.y, vp.y); lmin.z = fminf(lmin.z, vp.z);
                        lmax.x = fmaxf(lmax.x, vp.x); lmax.y = fmaxf(lmax.y, vp.y); lmax.z = fmaxf(lmax.z, vp.z);
                    }
                    for (int ci = 0; ci < 8; ++ci) {
                        glm::vec3 corner(
                            (ci & 1) ? lmax.x : lmin.x,
                            (ci & 2) ? lmax.y : lmin.y,
                            (ci & 4) ? lmax.z : lmin.z);
                        glm::vec3 tc = glm::vec3(partLocal * glm::vec4(corner, 1.0f));
                        fileMin.x = fminf(fileMin.x, tc.x); fileMin.y = fminf(fileMin.y, tc.y); fileMin.z = fminf(fileMin.z, tc.z);
                        fileMax.x = fmaxf(fileMax.x, tc.x); fileMax.y = fmaxf(fileMax.y, tc.y); fileMax.z = fmaxf(fileMax.z, tc.z);
                    }
                    haveAnyGeo = true;
                }
            }

            // Skinned sub-mesh: hand its bind data to the Animator so a
            // later-loaded animation can drive this object.
            if (raw.isSkinned()) {
                CoreEngine::MeshSkinBinding bind;
                bind.meshName = raw.name;
                bind.Wmesh = raw.meshWorldRest;
                for (const auto& mb : raw.meshBones) {
                    bind.boneNames.push_back(mb.name);
                    bind.IB.push_back(mb.IB);
                    bind.boneRestWorld.push_back(mb.restWorld);
                }
                CoreEngine::Animator::Get().RegisterObject(addedId, bind);
            }
        }

        // Visible confirmation in the Console panel (also proves the running
        // binary contains the additive-import behavior: existing objects are
        // NOT removed).
        {
            ConsoleLog("Imported " + baseName + ": added 1 model root + " +
                       std::to_string(model.rawMeshes.size()) +
                       " part(s), scene now has " +
                       std::to_string(CoreEngine::GetSceneObjects().size()) + " object(s)");
        }

        // Frame the camera on the model's FILE-space bounding box
        glm::vec3 modelCenter = haveAnyGeo ? (fileMin + fileMax) * 0.5f : glm::vec3(0.0f);
        glm::vec3 modelExtent = haveAnyGeo ? (fileMax - fileMin) * 0.5f : glm::vec3(1.0f);
        for (int i = 0; i < 3; ++i) {
            if (modelExtent[i] < 0.001f) modelExtent[i] = 1.0f;
        }
        float maxDim = fmaxf(fmaxf(modelExtent.x, modelExtent.y), modelExtent.z);
        float dist = maxDim * 4.0f;
        if (dist < 5.0f) dist = 5.0f;
        glm::vec3 camPos(modelCenter.x, modelCenter.y + dist * 0.3f, modelCenter.z - dist);
        // Aim at the model's center — models may be large and/or offset from
        // the world origin, so the default (0,0,0) target can leave them
        // off-center in the view.
        CoreEngine::SetCameraTarget({modelCenter.x, modelCenter.y, modelCenter.z});
        CoreEngine::SetCameraPosition({camPos.x, camPos.y, camPos.z});

        // Keep the "Camera" scene object in sync with the orbit camera's new
        // position, otherwise the inspector would show a stale position and
        // editing the camera object would snap the view back.
        uint32_t camId = CoreEngine::GetCameraObjectId();
        if (camId != 0) {
            for (auto& obj : CoreEngine::GetSceneObjects()) {
                if (obj.id == camId) {
                    obj.position = CoreEngine::GetCameraPosition();
                    break;
                }
            }
        }
    }

        void ReloadLastFBX() {
        // Remove the parts from the previous import, then re-import so new
        // settings (e.g. smooth normals) apply in place without duplicating
        // the model in the scene.
        for (uint32_t id : g_lastModelObjectIds) {
            CoreEngine::RemoveFromScene(id);
            CoreEngine::Animator::Get().UnregisterObject(id);
        }
        g_lastModelObjectIds.clear();
        if (!g_lastLoadedFBX.empty()) {
            LoadFBXAtPath(g_lastLoadedFBX);
        }
    }

    void LoadFBXFromFileDialog(GLFWwindow* window) {
#if defined(_WIN32)
        char filePath[MAX_PATH] = {0};
        OPENFILENAME ofn = {0};
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = 0;
        ofn.lpstrFilter = "FBX Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileName(&ofn)) {
            g_lastLoadedFBX = filePath;
            LoadFBXAtPath(filePath);
        }
#else
        printf("[Editor] Press F1 to select an FBX file (native file dialog not implemented on this platform)\n");
#endif
    }

    void LoadAnimationAtPath(const std::string& path) {
        CoreEngine::AnimationFile file = AssetLoader::LoadFBXAnimation(path);
        if (!file.success) {
            ConsoleLog("Failed to load animation: " + path);
            return;
        }

        auto& anim = CoreEngine::Animator::Get();
        if (anim.Bind(file)) {
            const auto* clip = anim.ActiveClip();
            ConsoleLog("Animation bound: '" + clip->name + "' (" +
                       std::to_string((int)(clip->duration * 1000.0f)) + " ms, " +
                       std::to_string(anim.GetSkinObjectCount()) + " skinned object(s))");
            g_showAnimationPanel = true;
        } else {
            ConsoleLog("No skinned model in the scene — import a rigged FBX first (File → Load FBX)");
        }
    }

    void LoadAnimationFromFileDialog(GLFWwindow* window) {
        (void)window;
#if defined(_WIN32)
        char filePath[MAX_PATH] = {0};
        OPENFILENAME ofn = {0};
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = 0;
        ofn.lpstrFilter = "FBX Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileName(&ofn)) {
            LoadAnimationAtPath(filePath);
        }
#else
        printf("[Editor] Native animation file dialog not implemented on this platform\n");
#endif
    }

}  // namespace Editor
