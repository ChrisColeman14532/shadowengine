#pragma once

// Scene file I/O: save and load the scene as a .scene JSON file.
//
// The file is human-readable JSON. GPU resources are never serialized —
// meshes and textures are referenced by stable asset paths so a .scene
// file stays portable between machines:
//
//   mesh:     "builtin"  : "cube" | "plane"          built-in primitive
//             "file"     : "assets/x.fbx"            FBX asset path
//             "submesh"  : "<raw mesh name>"         part within that file
//   texture:  "path.png"                             reloaded from disk
//             "embedded"                             baked into the object's
//                                                    FBX — re-extracted from
//                                                    that file at load time
//
// The SerializeSceneFileData / ParseSceneFileData pair is pure data with
// no GL dependency, so it can be unit-tested without a GL context.
// SaveSceneFile / LoadSceneFile are the live-scene bridge (GL required).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace CoreEngine {

    // ── Pure data model (no GL, no pointers) ────────────────────────

    struct SceneMaterialData {
        std::string name;
        glm::vec3 baseColor = glm::vec3(1.0f);
        glm::vec3 emissiveColor = glm::vec3(0.0f);
        float metallic = 0.0f;
        float roughness = 1.0f;
        float ao = 1.0f;
        bool useMaterial = true;
        // File path of the texture source, the special marker "embedded"
        // (texture baked into the object's FBX), or "" for no texture.
        std::string diffuseTexture;
        std::string normalTexture;
    };

    struct SceneMeshRef {
        std::string builtin;  // "cube" | "plane" | "" (none)
        std::string file;     // FBX asset path ("" when builtin)
        std::string submesh;  // raw mesh name within the file
        bool valid() const { return !builtin.empty() || (!file.empty() && !submesh.empty()); }
    };

    struct SceneObjectData {
        uint32_t id = 0;
        std::string name;
        uint32_t parent = 0;   // 0 = top-level
        bool isGroup = false;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 rotation = glm::vec3(0.0f);   // Euler angles, radians
        glm::vec3 scale = glm::vec3(1.0f);
        SceneMeshRef mesh;
        SceneMaterialData material;
    };

    struct SceneFileData {
        int version = 1;
        // FBX import option that was active when the scene was saved —
        // referenced FBX files are re-imported with the same setting.
        bool smoothNormals = false;
        std::vector<SceneObjectData> objects;
    };

    // ── Pure serialize / parse (no GL required) ─────────────────────

    // Serialize to a formatted JSON string (deterministic field order).
    std::string SerializeSceneFileData(const SceneFileData& data);

    // Parse JSON text into `data`. Returns false and fills `error`
    // (when non-null) on malformed input or a missing "objects" array.
    bool ParseSceneFileData(const char* text, size_t size,
                            SceneFileData& out, std::string* error = nullptr);

    // ── Live-scene bridge (requires a GL context) ───────────────────

    // Snapshot the live scene into a serializable struct. The camera
    // scene object is editor state, not scene content — it is skipped.
    // `smoothNormals` (editor state) is recorded in the file so the
    // referenced FBX assets are re-imported the same way on load.
    SceneFileData CollectSceneFileData(bool smoothNormals);

    // Save the live scene to `path` (.scene JSON). Returns false (and
    // logs) when the file cannot be written.
    bool SaveSceneFile(const std::string& path, bool smoothNormals);

    // Replace the live scene with the contents of `path`.
    //
    // Referenced FBX assets are re-imported (with the file's
    // smoothNormals setting), meshes are rebuilt, materials restored,
    // textures reloaded (from disk or re-extracted from the FBX),
    // skinned objects re-registered with the Animator, and embedded
    // animations re-bound. The camera object is recreated.
    //
    // Missing asset files degrade gracefully: the object is kept (its
    // transform and hierarchy survive) but without its mesh.
    // Returns false when the file cannot be read or parsed — in that
    // case the live scene is left untouched.
    bool LoadSceneFile(const std::string& path);

    // Path of the last loaded/saved scene file ("" when none). The
    // editor shows it in the window title and uses it for Ctrl+S.
    const std::string& GetSceneFilePath();
    void SetSceneFilePath(const std::string& path);

} // namespace CoreEngine
