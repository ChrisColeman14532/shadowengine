// Animator: runtime animation evaluation + per-object bone palettes.
//
// See animator.h for the design (clip evaluated on the animation file's
// own node tree, D-alignment to model space, J = WmeshInv*D*Wa(t)*IB).

#include "core/animator.h"
#include "core/model.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <cmath>

namespace CoreEngine {

namespace {

    // local = T * R * S (matches how the FBX node transforms are stored:
    // assimp's mTransformation is the composed TRS with S last).
    glm::mat4 ComposeTRS(const glm::vec3& p, const glm::quat& q, const glm::vec3& s) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), p);
        m *= glm::mat4(q);
        m[0] *= s.x;
        m[1] *= s.y;
        m[2] *= s.z;
        return m;
    }

    // Decompose an affine TRS matrix (columns carry scale) into T, R, S.
    // Handles negative (reflective) scale by flipping the third axis.
    void DecomposeTRS(const glm::mat4& M, glm::vec3& p, glm::quat& q, glm::vec3& s) {
        p = glm::vec3(M[3].x, M[3].y, M[3].z);
        glm::vec3 c0(M[0].x, M[0].y, M[0].z);
        glm::vec3 c1(M[1].x, M[1].y, M[1].z);
        glm::vec3 c2(M[2].x, M[2].y, M[2].z);
        s = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));

        auto safe = [](const glm::vec3& c, float len) {
            return len > 1e-12f ? c / len : glm::vec3(0.0f);
        };
        glm::mat3 R(safe(c0, s.x), safe(c1, s.y), safe(c2, s.z));  // columns
        if (glm::determinant(R) < 0.0f) {
            R[2] = -R[2];
            s.z = -s.z;
        }
        q = glm::quat(R);
        q = glm::normalize(q);
    }

} // namespace

Animator& Animator::Get() {
    static Animator instance;
    return instance;
}

// ── Object registration ──────────────────────────────────────────────

void Animator::RegisterObject(uint32_t objectId, const MeshSkinBinding& binding) {
    if (!binding.valid()) return;
    for (auto& os : objects_) {
        if (os.objectId == objectId) return;  // already registered
    }
    ObjectSkin os;
    os.objectId = objectId;
    os.boneNames = binding.boneNames;
    os.IB = binding.IB;
    os.boneRestWorld = binding.boneRestWorld;
    os.Wmesh = binding.Wmesh;
    os.boneAnimNode.assign(os.boneNames.size(), -1);
    os.palette.assign(os.boneNames.size(), glm::mat4(1.0f));
    objects_.push_back(std::move(os));

    if (bound_) {
        // Keep a new object in sync with the already-bound animation.
        BindObject(objects_.back());
        UpdatePalettes();
    }
    printf("[Animator] Registered skinned object %u ('%s', %zu bones)\n",
           objectId, binding.meshName.c_str(), objects_.back().boneNames.size());
}

void Animator::UnregisterObject(uint32_t objectId) {
    for (size_t i = 0; i < objects_.size(); ++i) {
        if (objects_[i].objectId == objectId) {
            objects_.erase(objects_.begin() + i);
            return;
        }
    }
}

void Animator::Reset() {
    objects_.clear();
    Unbind();
    time_ = 0.0f;
}

// ── Binding ───────────────────────────────────────────────────────────

bool Animator::Bind(const AnimationFile& anim) {
    if (objects_.empty()) {
        fprintf(stderr, "[Animator] Bind failed: no skinned model objects registered\n");
        return false;
    }
    if (anim.clips.empty()) {
        fprintf(stderr, "[Animator] Bind failed: animation file has no clips\n");
        return false;
    }

    anim_ = anim;
    bound_ = true;
    time_ = 0.0f;
    playing_ = true;

    const auto& nodes = anim_.nodes;
    const auto& clip = anim_.clips[0];

    // Channel -> anim-node verification (always succeeds for a single file;
    // reported loudly if it ever fails).
    std::map<std::string, int> amap;
    for (size_t i = 0; i < nodes.size(); ++i) amap[nodes[i].name] = (int)i;

    int unboundChannels = 0;
    for (const auto& t : clip.tracks) {
        if (!amap.count(t.nodeName)) {
            ++unboundChannels;
            fprintf(stderr, "[Animator] WARNING: channel '%s' has no node in the animation tree\n",
                    t.nodeName.c_str());
        }
    }

    // Per-node track index + rest-pose cache + depth (pre-order tree =>
    // the parent's depth is already known).
    nodeTrack_.assign(nodes.size(), -1);
    restPoses_.resize(nodes.size());
    nodeDepth_.resize(nodes.size(), 0);
    for (size_t i = 0; i < nodes.size(); ++i) {
        nodeDepth_[i] = (nodes[i].parentIndex >= 0) ? nodeDepth_[nodes[i].parentIndex] + 1 : 0;
        DecomposeTRS(nodes[i].local, restPoses_[i].pos, restPoses_[i].rot, restPoses_[i].scale);
    }
    for (int ti = 0; ti < (int)clip.tracks.size(); ++ti) {
        auto it = amap.find(clip.tracks[ti].nodeName);
        if (it != amap.end()) nodeTrack_[it->second] = ti;
    }

    for (auto& os : objects_) BindObject(os);

    EvaluateWorlds();
    UpdatePalettes();

    printf("[Animator] Bound clip '%s' (%.3fs @ %.1f tps) to %zu skinned object(s)"
           "%s\n", clip.name.c_str(), clip.duration, clip.fps, objects_.size(),
           unboundChannels ? " (with unbound channels!)" : "");
    return true;
}

void Animator::BindObject(ObjectSkin& os) {
    const auto& nodes = anim_.nodes;
    const auto& clip = anim_.clips[0];
    (void)clip;

    std::map<std::string, int> amap;
    for (size_t i = 0; i < nodes.size(); ++i) amap[nodes[i].name] = (int)i;

    // Bone -> anim node. Bones missing from the anim tree keep their rest
    // pose (palette falls back to identity, see UpdatePalettes).
    os.boneAnimNode.assign(os.boneNames.size(), -1);
    for (size_t b = 0; b < os.boneNames.size(); ++b) {
        auto it = amap.find(os.boneNames[b]);
        if (it != amap.end()) os.boneAnimNode[b] = it->second;
    }

    // Alignment anchor: the shared skeleton bone HIGHEST in the anim tree
    // (lowest depth) — for mixamo rigs this is the Hips root, so D absorbs
    // whatever root wrapper/axis difference exists between the files.
    int best = -1;
    int bestDepth = 1 << 30;
    for (size_t b = 0; b < os.boneNames.size(); ++b) {
        auto it = amap.find(os.boneNames[b]);
        if (it == amap.end()) continue;
        if (nodeDepth_[it->second] < bestDepth) {
            bestDepth = nodeDepth_[it->second];
            best = it->second;
        }
    }

    if (best >= 0) {
        // Wm(anchor) for THIS object: find which bone index maps to `best`.
        const glm::mat4* wma = nullptr;
        for (size_t b = 0; b < os.boneNames.size(); ++b) {
            if (os.boneAnimNode[b] == best) { wma = &os.boneRestWorld[b]; break; }
        }
        if (wma) {
            os.D = (*wma) * glm::inverse(nodes[best].world);
            os.anchorAnimNode = best;
        } else {
            os.D = glm::mat4(1.0f);
            os.anchorAnimNode = -1;
        }
    } else {
        fprintf(stderr, "[Animator] WARNING: object %u shares no bones with the animation tree "
                "(alignment falls back to identity)\n", os.objectId);
        os.D = glm::mat4(1.0f);
        os.anchorAnimNode = -1;
    }
    os.WmeshInv2 = glm::inverse(os.Wmesh) * os.D;
}

void Animator::Unbind() {
    bound_ = false;
    anim_.nodes.clear();
    anim_.clips.clear();
    anim_.success = false;
    anim_.filename.clear();
    animWorlds_.clear();
    nodeTrack_.clear();
    restPoses_.clear();
    nodeDepth_.clear();
}

// ── Playback ──────────────────────────────────────────────────────────

void Animator::Tick(float dt) {
    if (!bound_) return;
    if (playing_ && dt > 0.0f) {
        time_ += dt;
        const float dur = anim_.clips[0].duration;
        if (dur > 0.0f) {
            time_ = std::fmod(time_, dur);
            if (time_ < 0.0f) time_ += dur;
        }
    }
    EvaluateWorlds();
    UpdatePalettes();
}

void Animator::SetTime(float t) {
    if (!bound_) return;
    const float dur = anim_.clips[0].duration;
    if (dur > 0.0f) {
        t = std::fmod(t, dur);
        if (t < 0.0f) t += dur;
    }
    time_ = t;
    EvaluateWorlds();
    UpdatePalettes();
}

// Forward pass over the anim tree: for every node, take its local at
// time_ (animated components when it has a track, exact rest matrix
// otherwise) and multiply by the parent's world. Pre-order indexing
// guarantees parents first.
//
// IMPORTANT: nodes WITHOUT a track keep their EXACT rest matrix —
// decomposing and recomposing (T*R*S) is lossy for matrices with shear
// or a non-orthogonal basis (assimp's residual wrapper matrices), and
// the error would leak into every child's world matrix.
void Animator::EvaluateWorlds() {
    const auto& nodes = anim_.nodes;
    const auto& clip = anim_.clips[0];
    if (animWorlds_.size() != nodes.size()) animWorlds_.resize(nodes.size());

    for (size_t i = 0; i < nodes.size(); ++i) {
        glm::mat4 local = nodes[i].local;

        int ti = nodeTrack_[i];
        if (ti >= 0) {
            const auto& track = clip.tracks[ti];
            if (!track.position.empty() || !track.rotation.empty() || !track.scale.empty()) {
                glm::vec3 p;
                glm::quat q;
                glm::vec3 s;
                clip.Evaluate(track, time_,
                              restPoses_[i].pos, restPoses_[i].rot, restPoses_[i].scale,
                              p, q, s);
                local = ComposeTRS(p, q, s);
            }
        }

        const glm::mat4 parent = (nodes[i].parentIndex >= 0)
            ? animWorlds_[nodes[i].parentIndex] : glm::mat4(1.0f);
        animWorlds_[i] = parent * local;
    }
}

// J(b, t) = WmeshInv * D * Wa(b, t) * IB(b) = WmeshInv2 * Wa(b, t) * IB(b).
// Bones without an anim node hold their rest pose: J = WmeshInv2 * D^-1 *
// Wm(b) * IB = (WmeshInv * Wm(b)) * IB = BrMesh * IB = I, so those
// vertices stay exactly where the model authored them.
void Animator::UpdatePalettes() {
    if (!bound_) return;
    for (auto& os : objects_) {
        if (os.palette.size() != os.IB.size())
            os.palette.assign(os.IB.size(), glm::mat4(1.0f));
        for (size_t b = 0; b < os.IB.size(); ++b) {
            int an = (b < os.boneAnimNode.size()) ? os.boneAnimNode[b] : -1;
            if (an >= 0 && an < (int)animWorlds_.size()) {
                os.palette[b] = os.WmeshInv2 * animWorlds_[an] * os.IB[b];
            } else {
                os.palette[b] = os.WmeshInv2 * glm::inverse(os.D) * os.boneRestWorld[b] * os.IB[b];
            }
        }
    }
}

// ── Palette queries ───────────────────────────────────────────────────

Animator::ObjectSkin* Animator::FindObject(uint32_t objectId) {
    for (auto& os : objects_) if (os.objectId == objectId) return &os;
    return nullptr;
}

const Animator::ObjectSkin* Animator::FindObject(uint32_t objectId) const {
    for (const auto& os : objects_) if (os.objectId == objectId) return &os;
    return nullptr;
}

int Animator::GetBonePalette(uint32_t objectId, glm::mat4* out) const {
    const ObjectSkin* os = FindObject(objectId);
    if (!os || os->palette.empty()) return 0;
    int n = (int)std::min<size_t>(os->palette.size(), MAX_SKIN_BONES);
    for (int i = 0; i < n; ++i) out[i] = os->palette[i];
    return n;
}

int Animator::BoneCount(uint32_t objectId) const {
    const ObjectSkin* os = FindObject(objectId);
    return (os && !os->palette.empty()) ? (int)os->palette.size() : 0;
}

} // namespace CoreEngine
