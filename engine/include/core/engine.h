#pragma once

// ShadowEngine core engine — umbrella header.
//
// Includes every engine module header. Existing code that includes
// "core/engine.h" keeps working unchanged; new code may include the
// specific module headers directly (types.h, mesh.h, model.h, ...).

#include "core/types.h"
#include "core/mesh.h"
#include "core/model.h"
#include "core/texture.h"
#include "core/material.h"
#include "core/scene.h"
#include "core/camera.h"
#include "core/shadow_map.h"
#include "core/skybox.h"
#include "core/debug_draw.h"
#include "core/shader.h"
#include "core/renderer.h"
