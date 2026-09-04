#pragma once

#include <string>
#include <vector>
#include <GL/glew.h>
#include "core/engine.h"

namespace AssetLoader {

    CoreEngine::FBXModel LoadFBX(const std::string& path, bool smoothNormals = false);
    void DestroyFBX(CoreEngine::FBXModel& model);
    void ClearAll();

    CoreEngine::PrimitiveMesh MergeFromModel(const CoreEngine::FBXModel& model);
    CoreEngine::PrimitiveMesh MergeSubMesh(const CoreEngine::FBXModel& model, size_t submeshIndex);
    void ComputeModelAABB(const CoreEngine::FBXModel& model, glm::vec3& center, glm::vec3& extent);

} // namespace AssetLoader
