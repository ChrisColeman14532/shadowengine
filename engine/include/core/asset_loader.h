#pragma once

#include <string>
#include <vector>
#include <GL/glew.h>
#include "core/engine.h"

namespace AssetLoader {

    CoreEngine::FBXModel LoadFBX(const std::string& path);
    void DestroyFBX(CoreEngine::FBXModel& model);
    void ClearAll();

    CoreEngine::PrimitiveMesh MergeFromModel(const CoreEngine::FBXModel& model);
    void ComputeModelAABB(const CoreEngine::FBXModel& model, glm::vec3& center, glm::vec3& extent);

} // namespace AssetLoader
