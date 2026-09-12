#pragma once

// Animation data model: keyframe clips extracted from FBX by the asset
// loader, plus the node tree (rest pose) they animate.
//
// Keyframe times are stored in SECONDS. Rotation keys are quaternions
// (slerp on evaluation). Tracks may be partially empty — a node often has
// rotation keys but only a single constant position/scale key — the
// evaluator falls back to the node's rest transform for empty tracks.

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace CoreEngine {

    struct AnimVec3Key {
        float time = 0.0f;
        glm::vec3 value{0.0f};
    };

    struct AnimQuatKey {
        float time = 0.0f;
        glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};  // w,x,y,z
    };

    // Keyframe tracks for ONE node (identified by node name).
    struct NodeAnimTrack {
        std::string nodeName;
        std::vector<AnimVec3Key> position;   // may be empty
        std::vector<AnimQuatKey> rotation;   // may be empty
        std::vector<AnimVec3Key> scale;      // may be empty
    };

    // One animation clip: a set of node tracks over time.
    // A character model and a mixamo-style animation-only FBX bind by
    // NODE NAME (both are parsed with the same importer, so assimp's
    // decomposed wrapper node names match between the two files).
    struct AnimationClip {
        std::string name = "clip";
        float duration = 0.0f;   // seconds
        float fps = 30.0f;       // source ticks/second (info only)
        std::vector<NodeAnimTrack> tracks;

        const NodeAnimTrack* FindTrack(const std::string& nodeName) const {
            for (const auto& t : tracks) {
                if (t.nodeName == nodeName) return &t;
            }
            return nullptr;
        }

        // Evaluate one track at time t (seconds).
        // Returns false if the track has no keys at all (caller keeps rest).
        // Empty sub-tracks fall back to the node's rest value (pass it in).
        bool Evaluate(const NodeAnimTrack& track, float t,
                      const glm::vec3& restPos, const glm::quat& restRot,
                      const glm::vec3& restScale,
                      glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale) const {
            bool any = !track.position.empty() || !track.rotation.empty() || !track.scale.empty();
            if (!any) return false;

            outPos = restPos;
            outRot = restRot;
            outScale = restScale;

            if (!track.position.empty())
                outPos = SampleVec3(track.position, t);
            if (!track.rotation.empty())
                outRot = SampleQuat(track.rotation, t);
            if (!track.scale.empty())
                outScale = SampleVec3(track.scale, t);
            return true;
        }

    private:
        static glm::vec3 SampleVec3(const std::vector<AnimVec3Key>& keys, float t) {
            const AnimVec3Key& first = keys.front();
            const AnimVec3Key& last = keys.back();
            if (t <= first.time) return first.value;
            if (t >= last.time) return last.value;

            // Binary search for the key pair bracketing t (keys are sorted).
            size_t lo = 0, hi = keys.size() - 1;
            while (hi - lo > 1) {
                size_t mid = (lo + hi) / 2;
                if (keys[mid].time <= t) lo = mid; else hi = mid;
            }
            const AnimVec3Key& a = keys[lo];
            const AnimVec3Key& b = keys[hi];
            float span = b.time - a.time;
            float f = span > 1e-6f ? (t - a.time) / span : 0.0f;
            return a.value + (b.value - a.value) * f;
        }

        static glm::quat SampleQuat(const std::vector<AnimQuatKey>& keys, float t) {
            const AnimQuatKey& first = keys.front();
            const AnimQuatKey& last = keys.back();
            if (t <= first.time) return first.value;
            if (t >= last.time) return last.value;

            size_t lo = 0, hi = keys.size() - 1;
            while (hi - lo > 1) {
                size_t mid = (lo + hi) / 2;
                if (keys[mid].time <= t) lo = mid; else hi = mid;
            }
            return glm::slerp(keys[lo].value, keys[hi].value,
                              (t - keys[lo].time) / (keys[hi].time - keys[lo].time + 1e-6f));
        }
    };

    // Node in a model's rest-pose hierarchy (flat list, parent by index).
    // Built by the FBX loader from aiScene::mRootNode. Includes assimp's
    // decomposed wrapper nodes (e.g. "mixamorig:Hips_$AssimpFbx$_Rotation")
    // so animation channels — which target those names — resolve exactly.
    struct AnimNode {
        std::string name;
        int parentIndex = -1;
        std::vector<int> childIndices;
        glm::mat4 local = glm::mat4(1.0f);   // rest local transform
        glm::mat4 world = glm::mat4(1.0f);    // rest world transform (computed)
        std::vector<int> meshIndices;          // indexes into FBXModel.rawMeshes
    };

    // A fully loaded animation file: the file's OWN rest node tree plus its
    // clips. The clips' channels target names in `nodes` (same assimp parse,
    // same decomposed wrapper nodes), so channel-to-node binding by exact
    // name always succeeds inside the file.
    //
    // NOTE: channels from a DIFFERENT file (e.g. a separate model FBX)
    // generally do NOT match this tree — the Animator therefore evaluates
    // clips on this file's tree and aligns the result to model space (see
    // animator.h).
    struct AnimationFile {
        bool success = false;
        std::string filename;
        std::vector<AnimNode> nodes;      // the file's rest-pose node tree
        std::vector<AnimationClip> clips;
    };

} // namespace CoreEngine
