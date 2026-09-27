// ECS scene integration: project the SceneObject scene into the ECS
// world, tick the systems, and draw the scene through ECS queries.
//
// The SceneObject vector (scene.cpp) remains the editor-facing scene
// representation — the ECS is a per-frame projection of it plus the
// systems that compute derived state (world matrices). Entity value ==
// scene object id, so Animator bone palettes, picking and selection
// keep using the same numbers.

#include "core/engine.h"

#include <algorithm>
#include <cstdio>
#include <unordered_set>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "engine_internal.h"

namespace CoreEngine {

    Ecs::World& GetEcsWorld() { return s_ecsWorld; }
    Ecs::SystemManager& GetEcsSystems() { return s_ecsSystems; }

    void InitEcs() {
        if (s_ecsSystemsRegistered) return;
        Ecs::RegisterBuiltinSystems(s_ecsSystems);
        s_ecsSystemsRegistered = true;
    }

    // ── Scene -> ECS sync ───────────────────────────────────────────
    //
    // Upsert: every scene object becomes (or updates) an entity with
    // its own id, carrying Transform (local TRS + parent link), Name
    // and — when the object has a mesh — MeshRenderer. Groups have no
    // mesh, so they get no MeshRenderer and drop out of render queries
    // automatically. Stale entities (scene objects removed) are
    // destroyed.
    void SyncSceneToEcs() {
        Ecs::World& w = GetEcsWorld();

        std::vector<Ecs::Entity> kept;
        kept.reserve(s_sceneObjects.size());

        for (const auto& o : s_sceneObjects) {
            const Ecs::Entity e = w.EnsureEntity(o.id);
            if (e == Ecs::INVALID_ENTITY) continue;  // id conflict (should not happen)
            kept.push_back(e);

            Ecs::Transform& t = w.AddOrGet<Ecs::Transform>(e);
            t.position = o.position;
            t.rotation = o.rotation;
            t.scale = o.scale;
            t.parent = (o.parentId == 0) ? Ecs::INVALID_ENTITY : static_cast<Ecs::Entity>(o.parentId);

            w.AddOrGet<Ecs::Name>(e).value = o.name;

            if (o.mesh) {
                Ecs::MeshRenderer& r = w.AddOrGet<Ecs::MeshRenderer>(e);
                r.mesh = o.mesh;
                r.material = o.material;
            } else {
                w.Remove<Ecs::MeshRenderer>(e);
            }
        }

        // Prune entities whose scene object is gone.
        for (Ecs::Entity e : w.AliveEntities()) {
            if (std::find(kept.begin(), kept.end(), e) == kept.end())
                w.Destroy(e);
        }
    }

    void TickEcs(float dt) {
        InitEcs();
        SyncSceneToEcs();
        s_ecsSystems.Tick(s_ecsWorld, dt);
    }

    // ── ECS main pass ───────────────────────────────────────────────
    //
    // Draws every entity with (WorldTransform, MeshRenderer). This is
    // the ECS replacement for the editor's per-SceneObject draw loop:
    // same uniforms, same texture binds, same bone palettes — but the
    // object list and world matrices come from ECS queries instead of
    // a parent-chain walk. Groups have no MeshRenderer, so they drop
    // out of the query; the camera object is skipped explicitly.
    void RenderSceneMeshesEcs(GLuint program) {
        Ecs::World& w = GetEcsWorld();

        static glm::mat4 bonePalette[MAX_SKIN_BONES];
        static std::unordered_set<Ecs::Entity> warnedEmptyIds;

        w.ForEach<Ecs::WorldTransform, Ecs::MeshRenderer>(
            [&](Ecs::Entity e, Ecs::WorldTransform& wt, Ecs::MeshRenderer& mr) {
                // Skip camera object — it's placed at the camera position,
                // so rendering it would put the camera inside the cube.
                if (e == s_cameraObjectId) return;

                auto& mesh = mr.mesh;
                if (!mesh || !mesh->VAO || mesh->indexCount == 0) {
                    // One-time diagnostic: object exists in the scene but
                    // has no renderable geometry (this is why it's
                    // invisible).
                    if (warnedEmptyIds.insert(e).second) {
                        const char* name = "?";
                        if (const auto* nm = w.Get<Ecs::Name>(e))
                            name = nm->value.c_str();
                        printf("[Ecs] WARNING: object '%s' (id %u) has empty mesh "
                               "(VAO=%u, indices=%u) — not rendered\n",
                               name, (unsigned)e,
                               (unsigned)(mesh ? mesh->VAO : 0),
                               (unsigned)(mesh ? mesh->indexCount : 0));
                    }
                    return;
                }

                glUseProgram(program);

                // World matrix from the ECS transform system (hierarchy
                // included). FBX parts are children of the model root
                // node, so transforming the root moves/rotates/scales
                // every part with it.
                SetUniformMat4(program, "uModel", wt.localToWorld);
                SetUniformVec3(program, "uBaseColor", mr.material.baseColor);

                // Skinning: upload this object's bone palette (J matrices
                // from the Animator). uSkinCount = 0 leaves static meshes
                // untouched. Entity value == scene object id.
                int nBones = Animator::Get().GetBonePalette(e, bonePalette);
                GLint skinCountLoc = glGetUniformLocation(program, "uSkinCount");
                if (skinCountLoc != -1) glUniform1i(skinCountLoc, nBones);
                if (nBones > 0) {
                    GLint bonesLoc = glGetUniformLocation(program, "uBoneMatrices");
                    if (bonesLoc != -1) glUniformMatrix4fv(bonesLoc, nBones, GL_FALSE, glm::value_ptr(bonePalette[0]));
                }

                // Pass material properties
                GLint metallicLoc = glGetUniformLocation(program, "uMetallic");
                if (metallicLoc != -1) glUniform1f(metallicLoc, mr.material.metallic);
                GLint roughnessLoc = glGetUniformLocation(program, "uRoughness");
                if (roughnessLoc != -1) glUniform1f(roughnessLoc, mr.material.roughness);
                GLint aoLoc = glGetUniformLocation(program, "uAO");
                if (aoLoc != -1) glUniform1f(aoLoc, mr.material.ao);
                GLint emissiveLoc = glGetUniformLocation(program, "uEmissiveColor");
                if (emissiveLoc != -1) SetUniformVec3(program, "uEmissiveColor", mr.material.emissiveColor);

                // Textures
                GLint hasDiffuseLoc = glGetUniformLocation(program, "uHasDiffuse");
                GLint hasNormalMapLoc = glGetUniformLocation(program, "uHasNormalMap");
                GLint diffuseLoc = glGetUniformLocation(program, "uDiffuseTex");
                GLint normalMapLoc = glGetUniformLocation(program, "uNormalTex");

                if (mr.material.diffuseTexture) {
                    if (hasDiffuseLoc != -1) glUniform1i(hasDiffuseLoc, 1);
                    if (diffuseLoc != -1) {
                        glActiveTexture(GL_TEXTURE1);
                        BindTexture(mr.material.diffuseTexture, 1);
                        glUniform1i(diffuseLoc, 1);
                    }
                } else {
                    if (hasDiffuseLoc != -1) glUniform1i(hasDiffuseLoc, 0);
                }

                // Normal map texture (separate from diffuse)
                if (mr.material.normalTexture) {
                    if (hasNormalMapLoc != -1) glUniform1i(hasNormalMapLoc, 1);
                    if (normalMapLoc != -1) {
                        glActiveTexture(GL_TEXTURE2);
                        BindTexture(mr.material.normalTexture, 2);
                        glUniform1i(normalMapLoc, 2);
                    }
                } else {
                    if (hasNormalMapLoc != -1) glUniform1i(hasNormalMapLoc, 0);
                }

                glBindVertexArray(mesh->VAO);
                if (mesh->EBO) {
                    glDrawElements(GL_TRIANGLES, (GLsizei)mesh->indexCount, GL_UNSIGNED_INT, 0);
                } else {
                    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)mesh->indexCount);
                }
                glBindVertexArray(0);
            });
    }

} // namespace CoreEngine
