#pragma once

// Runtime animation: the Animator owns the playback state and produces the
// per-object bone palettes (J matrices) that the vertex shaders blend with.
//
// ── Skinning design (validated in tools/test_skin_eval.cpp) ──────────
//
// A clip is evaluated on the ANIMATION FILE's own node tree (its channels
// bind there by exact name). This gives Wa(b, t) — bone b's world transform
// in animation-root space at time t.
//
// Model space and animation space are then aligned with
//     D = Wm(anchor) * Wa(anchor_rest)^-1
// where `anchor` is the highest shared skeleton bone (e.g. mixamorig:Hips)
// and Wm/Wa are the bone's REST world transforms in the model tree /
// animation tree. For same-file animations D is the identity.
//
// Each skinned mesh then gets its bone palette
//     J(b, t) = WmeshInv * D * Wa(b, t) * IB(b)
// with Wmesh = the mesh node's rest world transform (model space) and
// IB = BrMesh^-1, BrMesh = WmeshInv * Wm(b_rest)  (formula N).
//
// J maps MESH-LOCAL vertex positions to skinned mesh-local positions, so
// the scene object's uModel transform (editor placement) applies on top,
// exactly like it does for static meshes.

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/animation.h"

namespace CoreEngine {

    // Everything the Animator needs for ONE skinned scene object. Copied
    // out of RawMeshData at import time (the model value may be destroyed
    // afterwards). Bone lists use the mesh's own aiMesh bone order — the
    // same order the per-vertex bone indices use.
    struct MeshSkinBinding {
        std::string meshName;
        std::vector<std::string> boneNames;   // per bone
        std::vector<glm::mat4> IB;            // inverse bind per bone
        std::vector<glm::mat4> boneRestWorld; // Wm(bone_rest) per bone (model space)
        glm::mat4 Wmesh = glm::mat4(1.0f);    // mesh node rest world (model space)

        bool valid() const { return !boneNames.empty() && IB.size() == boneNames.size(); }
    };

    class Animator {
    public:
        static Animator& Get();

        // ── Object registration (called by the editor at import) ──────
        // Register a skinned scene object. Safe to call while an animation
        // is already bound (it gets the current palette immediately).
        void RegisterObject(uint32_t objectId, const MeshSkinBinding& binding);
        void UnregisterObject(uint32_t objectId);
        // Drop all registered objects AND any bound animation.
        void Reset();
        size_t GetSkinObjectCount() const { return objects_.size(); }

        // ── Binding ────────────────────────────────────────────────────
        // Bind an animation file to the registered objects. Returns false
        // (and logs) when there is nothing to bind.
        bool Bind(const AnimationFile& anim);
        void Unbind();
        bool IsBound() const { return bound_; }
        const AnimationFile* Animation() const { return bound_ ? &anim_ : nullptr; }

        // ── Playback ──────────────────────────────────────────────────
        // Advance time (when playing, looping at clip end) and refresh all
        // bone palettes. Cheap to call every frame even when unbound.
        void Tick(float dt);
        void SetPlaying(bool playing) { playing_ = playing; }
        bool IsPlaying() const { return playing_; }
        void SetTime(float t);   // clamped/looped into [0, duration); updates palettes
        float GetTime() const { return time_; }
        const AnimationClip* ActiveClip() const { return bound_ ? &anim_.clips[0] : nullptr; }

        // ── Per-object palette query (render passes) ──────────────────
        // Returns the number of bones written to `out` (0 = object is not
        // skinned or unbound). `out` must hold at least MAX_SKIN_BONES.
        int GetBonePalette(uint32_t objectId, glm::mat4* out) const;
        int BoneCount(uint32_t objectId) const;

    private:
        struct ObjectSkin {
            uint32_t objectId = 0;
            std::vector<std::string> boneNames;
            std::vector<glm::mat4> IB;
            std::vector<glm::mat4> boneRestWorld;
            glm::mat4 Wmesh = glm::mat4(1.0f);

            // Bind-time results
            int anchorAnimNode = -1;          // anim-tree node used for D (-1 = none)
            glm::mat4 D = glm::mat4(1.0f);    // anim-root -> model-root alignment
            glm::mat4 WmeshInv2 = glm::mat4(1.0f);  // WmeshInv * D
            std::vector<int> boneAnimNode;     // anim-tree node per bone (-1 = unbound)

            // Per-frame result: J(b, t) for each bone
            std::vector<glm::mat4> palette;
        };

        Animator() = default;
        void BindObject(ObjectSkin& os);
        void EvaluateWorlds();
        void UpdatePalettes();
        ObjectSkin* FindObject(uint32_t objectId);
        const ObjectSkin* FindObject(uint32_t objectId) const;

        std::vector<ObjectSkin> objects_;

        // Bound animation state
        AnimationFile anim_;
        bool bound_ = false;
        std::vector<glm::mat4> animWorlds_;    // per anim node: world at current time
        std::vector<int> nodeTrack_;           // per anim node: clip track index (-1 = none)
        struct RestPose { glm::vec3 pos{0}; glm::quat rot{1, 0, 0, 0}; glm::vec3 scale{1}; };
        std::vector<RestPose> restPoses_;      // per anim node (decomposed rest local)
        std::vector<int> nodeDepth_;          // per anim node (for anchor selection)

        bool playing_ = true;
        float time_ = 0.0f;
    };

} // namespace CoreEngine
