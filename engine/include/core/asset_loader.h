#pragma once

#include <string>
#include <vector>
#include <GL/glew.h>
#include "core/engine.h"

namespace AssetLoader {

    CoreEngine::FBXModel LoadFBX(const std::string& path, bool smoothNormals = false);

    // Load an animation file: its OWN rest node tree plus its clips (mixamo
    // downloads: skeleton + keyframes, no geometry). The node tree is needed
    // by the Animator — clips are evaluated on the animation file's own tree,
    // where their channels bind by exact name.
    CoreEngine::AnimationFile LoadFBXAnimation(const std::string& path);

    void DestroyFBX(CoreEngine::FBXModel& model);
    void ClearAll();

    CoreEngine::PrimitiveMesh MergeFromModel(const CoreEngine::FBXModel& model);
    CoreEngine::PrimitiveMesh MergeSubMesh(const CoreEngine::FBXModel& model, size_t submeshIndex);
    void ComputeModelAABB(const CoreEngine::FBXModel& model, glm::vec3& center, glm::vec3& extent);

} // namespace AssetLoader
