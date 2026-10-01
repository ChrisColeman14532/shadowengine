// Scene file I/O: .scene JSON serialization of the live scene.
//
// The pure data layer (SerializeSceneFileData / ParseSceneFileData) has
// no GL dependency and is unit-tested in tools/test_scene_file.cpp.
// The live-scene bridge (Save/Load/Collect) requires a GL context.

#include "core/engine.h"
#include "core/scene_file.h"
#include "core/animator.h"
#include "core/asset_loader.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine_internal.h"

namespace CoreEngine {

namespace {

    constexpr int kSceneFileVersion = 1;
    constexpr const char* kEmbeddedTextureMarker = "embedded";

    // Vector3 (engine) ↔ glm::vec3 (pure data model) conversions.
    inline glm::vec3 ToGlm(const Vector3& v) { return glm::vec3(v.x, v.y, v.z); }

    // ── JSON writers ─────────────────────────────────────────────────

    void WriteVec3(rapidjson::Value& out, const glm::vec3& v,
                   rapidjson::Document::AllocatorType& a) {
        out.SetArray();
        out.PushBack(static_cast<double>(v.x), a);
        out.PushBack(static_cast<double>(v.y), a);
        out.PushBack(static_cast<double>(v.z), a);
    }

    void WriteMaterial(rapidjson::Value& out, const SceneMaterialData& m,
                       rapidjson::Document::AllocatorType& a) {
        out.SetObject();
        out.AddMember(rapidjson::StringRef("name"),
                      rapidjson::Value(m.name.c_str(), a).Move(), a);
        rapidjson::Value base, emis;
        WriteVec3(base, m.baseColor, a);
        WriteVec3(emis, m.emissiveColor, a);
        out.AddMember(rapidjson::StringRef("baseColor"), base, a);
        out.AddMember(rapidjson::StringRef("emissive"), emis, a);
        out.AddMember(rapidjson::StringRef("metallic"),
                      static_cast<double>(m.metallic), a);
        out.AddMember(rapidjson::StringRef("roughness"),
                      static_cast<double>(m.roughness), a);
        out.AddMember(rapidjson::StringRef("ao"),
                      static_cast<double>(m.ao), a);
        out.AddMember(rapidjson::StringRef("useMaterial"), m.useMaterial, a);
        if (!m.diffuseTexture.empty())
            out.AddMember(rapidjson::StringRef("diffuse"),
                          rapidjson::Value(m.diffuseTexture.c_str(), a).Move(), a);
        if (!m.normalTexture.empty())
            out.AddMember(rapidjson::StringRef("normal"),
                          rapidjson::Value(m.normalTexture.c_str(), a).Move(), a);
    }

    // ── JSON readers ─────────────────────────────────────────────────

    bool ReadVec3(const rapidjson::Value& v, glm::vec3& out) {
        if (!v.IsArray() || v.Size() != 3) return false;
        for (rapidjson::SizeType i = 0; i < 3; ++i) {
            if (!v[i].IsNumber()) return false;
            out[i] = static_cast<float>(v[i].GetDouble());
        }
        return true;
    }

    void ReadOptionalString(const rapidjson::Value& obj, const char* key,
                            std::string& out) {
        auto it = obj.FindMember(key);
        if (it != obj.MemberEnd() && it->value.IsString())
            out = it->value.GetString();
    }

    bool ReadMaterial(const rapidjson::Value& v, SceneMaterialData& out,
                      std::string& err) {
        if (!v.IsObject()) { err = "material must be an object"; return false; }
        SceneMaterialData m;
        ReadOptionalString(v, "name", m.name);
        if (v.HasMember("baseColor") && !ReadVec3(v["baseColor"], m.baseColor))
            { err = "material.baseColor malformed"; return false; }
        if (v.HasMember("emissive") && !ReadVec3(v["emissive"], m.emissiveColor))
            { err = "material.emissive malformed"; return false; }
        if (v.HasMember("metallic") && v["metallic"].IsNumber())
            m.metallic = static_cast<float>(v["metallic"].GetDouble());
        if (v.HasMember("roughness") && v["roughness"].IsNumber())
            m.roughness = static_cast<float>(v["roughness"].GetDouble());
        if (v.HasMember("ao") && v["ao"].IsNumber())
            m.ao = static_cast<float>(v["ao"].GetDouble());
        if (v.HasMember("useMaterial") && v["useMaterial"].IsBool())
            m.useMaterial = v["useMaterial"].GetBool();
        ReadOptionalString(v, "diffuse", m.diffuseTexture);
        ReadOptionalString(v, "normal", m.normalTexture);
        out = std::move(m);
        return true;
    }

    bool ReadObject(const rapidjson::Value& v, SceneObjectData& out,
                    std::string& err) {
        if (!v.IsObject()) { err = "scene object must be an object"; return false; }
        SceneObjectData o;
        if (v.HasMember("id") && v["id"].IsNumber())
            o.id = static_cast<uint32_t>(v["id"].GetUint());
        ReadOptionalString(v, "name", o.name);
        if (v.HasMember("parent") && v["parent"].IsNumber())
            o.parent = static_cast<uint32_t>(v["parent"].GetUint());
        if (v.HasMember("group") && v["group"].IsBool())
            o.isGroup = v["group"].GetBool();
        if (v.HasMember("pos") && !ReadVec3(v["pos"], o.position))
            { err = "object.pos malformed"; return false; }
        if (v.HasMember("rot") && !ReadVec3(v["rot"], o.rotation))
            { err = "object.rot malformed"; return false; }
        if (v.HasMember("scale") && !ReadVec3(v["scale"], o.scale))
            { err = "object.scale malformed"; return false; }
        if (v.HasMember("mesh") && v["mesh"].IsObject()) {
            const auto& mv = v["mesh"];
            ReadOptionalString(mv, "builtin", o.mesh.builtin);
            ReadOptionalString(mv, "file", o.mesh.file);
            ReadOptionalString(mv, "submesh", o.mesh.submesh);
        }
        if (v.HasMember("material")) {
            if (!ReadMaterial(v["material"], o.material, err)) return false;
        }
        out = std::move(o);
        return true;
    }

    // ── Embedded (FBX-baked) texture extraction ──────────────────────
    // Walks the FBX's own material chain for a sub-mesh:
    // raw mesh → materialIndex → texture slot index → embedded pixel data.

    TexturePtr ExtractEmbeddedTexture(const FBXModel& model, int rawIndex,
                                      bool normalSlot) {
        if (rawIndex < 0 || rawIndex >= static_cast<int>(model.rawMeshes.size()))
            return nullptr;
        int mi = model.rawMeshes[rawIndex].materialIndex;
        if (mi < 0 || mi >= static_cast<int>(model.materialTextures.size()))
            return nullptr;
        int ti = normalSlot ? model.materialTextures[mi].normalIndex
                            : model.materialTextures[mi].diffuseIndex;
        if (ti < 0 || ti >= static_cast<int>(model.textures.size()))
            return nullptr;
        const auto& t = model.textures[ti];
        if (!t.data) return nullptr;
        return LoadTextureFromMemory(t.data, t.width, t.height, t.channels);
    }

    int FindRawMeshIndex(const FBXModel& model, const std::string& submesh) {
        for (size_t i = 0; i < model.rawMeshes.size(); ++i)
            if (model.rawMeshes[i].name == submesh) return static_cast<int>(i);
        return -1;
    }

    // SceneMaterialData → live Material (textures are re-created by the
    // caller from the saved source references).
    Material MaterialFromData(const SceneMaterialData& d) {
        Material m;
        m.name = d.name;
        m.baseColor = d.baseColor;
        m.emissiveColor = d.emissiveColor;
        m.metallic = d.metallic;
        m.roughness = d.roughness;
        m.ao = d.ao;
        m.useMaterial = d.useMaterial;
        return m;
    }

}  // namespace

// File-local state: the last scene file path (for the editor's title bar)
static std::string g_sceneFilePath;

// ── Pure serialize / parse ─────────────────────────────────────────

std::string SerializeSceneFileData(const SceneFileData& data) {
    using namespace rapidjson;
    Document doc;
    doc.SetObject();
    auto& a = doc.GetAllocator();

    doc.AddMember(StringRef("shadowScene"), data.version, a);
    doc.AddMember(StringRef("smoothNormals"), data.smoothNormals, a);

    Value objs(kArrayType);
    for (const auto& o : data.objects) {
        Value ov(kObjectType);
        ov.AddMember(StringRef("id"), o.id, a);
        ov.AddMember(StringRef("name"), Value(o.name.c_str(), a).Move(), a);
        ov.AddMember(StringRef("parent"), o.parent, a);
        if (o.isGroup) ov.AddMember(StringRef("group"), true, a);

        Value pv, rv, sv;
        WriteVec3(pv, o.position, a);
        WriteVec3(rv, o.rotation, a);
        WriteVec3(sv, o.scale, a);
        ov.AddMember(StringRef("pos"), pv, a);
        ov.AddMember(StringRef("rot"), rv, a);
        ov.AddMember(StringRef("scale"), sv, a);

        if (o.mesh.valid()) {
            Value mv(kObjectType);
            if (!o.mesh.builtin.empty()) {
                mv.AddMember(StringRef("builtin"),
                             Value(o.mesh.builtin.c_str(), a).Move(), a);
            } else {
                mv.AddMember(StringRef("file"),
                             Value(o.mesh.file.c_str(), a).Move(), a);
                mv.AddMember(StringRef("submesh"),
                             Value(o.mesh.submesh.c_str(), a).Move(), a);
            }
            ov.AddMember(StringRef("mesh"), mv, a);
        }

        Value mat;
        WriteMaterial(mat, o.material, a);
        ov.AddMember(StringRef("material"), mat, a);

        objs.PushBack(ov, a);
    }
    doc.AddMember(StringRef("objects"), objs, a);

    StringBuffer sb;
    PrettyWriter<StringBuffer> writer(sb);
    doc.Accept(writer);
    return sb.GetString();
}

bool ParseSceneFileData(const char* text, size_t size,
                        SceneFileData& out, std::string* error) {
    using namespace rapidjson;
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    if (!text || size == 0) return fail("empty scene file");
    Document doc;
    doc.Parse(text, size);
    if (doc.HasParseError())
        return fail("JSON parse error at offset " +
                    std::to_string(doc.GetErrorOffset()));
    if (!doc.IsObject()) return fail("root must be a JSON object");
    if (!doc.HasMember("shadowScene") || !doc["shadowScene"].IsInt())
        return fail("missing 'shadowScene' version field");
    if (doc["shadowScene"].GetInt() != kSceneFileVersion)
        return fail("unsupported scene file version " +
                    std::to_string(doc["shadowScene"].GetInt()));
    if (!doc.HasMember("objects") || !doc["objects"].IsArray())
        return fail("missing 'objects' array");

    SceneFileData data;
    data.version = kSceneFileVersion;
    if (doc.HasMember("smoothNormals") && doc["smoothNormals"].IsBool())
        data.smoothNormals = doc["smoothNormals"].GetBool();

    for (auto it = doc["objects"].Begin(); it != doc["objects"].End(); ++it) {
        SceneObjectData o;
        std::string err;
        if (!ReadObject(*it, o, err))
            return fail("object[" + std::to_string(data.objects.size()) + "]: " + err);
        if (o.name.empty()) o.name = "object_" + std::to_string(o.id);
        data.objects.push_back(std::move(o));
    }

    out = std::move(data);
    return true;
}

// ── Live-scene bridge ──────────────────────────────────────────────

SceneFileData CollectSceneFileData(bool smoothNormals) {
    SceneFileData data;
    data.smoothNormals = smoothNormals;

    for (const auto& obj : s_sceneObjects) {
        if (obj.id == s_cameraObjectId) continue;  // editor state, not scene content

        SceneObjectData o;
        o.id = obj.id;
        o.name = obj.name;
        o.parent = obj.parentId;
        o.isGroup = obj.isGroup;
        o.position = ToGlm(obj.position);
        o.rotation = ToGlm(obj.rotation);
        o.scale = ToGlm(obj.scale);

        // Mesh provenance: FBX parts carry their source file + sub-mesh
        // name; everything else is a built-in primitive (or no mesh).
        if (!obj.meshFile.empty()) {
            o.mesh.file = obj.meshFile;
            o.mesh.submesh = obj.meshSubmesh;
        } else if (obj.mesh) {
            o.mesh.builtin = obj.mesh->name;
        }

        // Material by value; textures by source (file path, "embedded"
        // when baked into the object's FBX, or dropped when procedural).
        o.material.name = obj.material.name;
        o.material.baseColor = obj.material.baseColor;
        o.material.emissiveColor = obj.material.emissiveColor;
        o.material.metallic = obj.material.metallic;
        o.material.roughness = obj.material.roughness;
        o.material.ao = obj.material.ao;
        o.material.useMaterial = obj.material.useMaterial;

        auto textureSource = [&](const std::string& path, const TexturePtr& tex) -> std::string {
            if (!path.empty()) return path;
            if (tex && !obj.meshFile.empty()) return kEmbeddedTextureMarker;
            return std::string();  // procedural (e.g. checkerboard) — not restorable
        };
        o.material.diffuseTexture =
            textureSource(obj.material.diffuseTexturePath, obj.material.diffuseTexture);
        o.material.normalTexture =
            textureSource(obj.material.normalTexturePath, obj.material.normalTexture);

        data.objects.push_back(std::move(o));
    }
    return data;
}

bool SaveSceneFile(const std::string& path, bool smoothNormals) {
    const SceneFileData data = CollectSceneFileData(smoothNormals);
    const std::string json = SerializeSceneFileData(data);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        printf("[Engine] SaveSceneFile: cannot open '%s' for writing\n", path.c_str());
        return false;
    }
    size_t written = std::fwrite(json.data(), 1, json.size(), f);
    std::fclose(f);
    if (written != json.size()) {
        printf("[Engine] SaveSceneFile: short write to '%s'\n", path.c_str());
        return false;
    }
    SetSceneFilePath(path);
    printf("[Engine] Scene saved: %s (%zu object(s))\n",
           path.c_str(), data.objects.size());
    return true;
}

bool LoadSceneFile(const std::string& path) {
    // 1) Read + parse (pure layer — the live scene is untouched on failure)
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        printf("[Engine] LoadSceneFile: cannot open '%s'\n", path.c_str());
        return false;
    }
    std::string text;
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    std::fclose(f);
    if (text.empty()) {
        printf("[Engine] LoadSceneFile: '%s' is empty\n", path.c_str());
        return false;
    }

    SceneFileData data;
    std::string err;
    if (!ParseSceneFileData(text.data(), text.size(), data, &err)) {
        printf("[Engine] LoadSceneFile: %s: %s\n", path.c_str(), err.c_str());
        return false;
    }

    // 2) Re-import every referenced FBX (deduped, first-seen order).
    //    The model values stay alive until the end of this function —
    //    textures are copied to the GPU, meshes are rebuilt below.
    std::vector<FBXModel> models;
    std::map<std::string, size_t> modelIndex;
    for (const auto& od : data.objects) {
        if (od.mesh.file.empty() || modelIndex.count(od.mesh.file)) continue;
        FBXModel m = AssetLoader::LoadFBX(od.mesh.file, data.smoothNormals);
        if (!m.success)
            printf("[Engine] LoadSceneFile: could not import '%s' — objects using it keep their transform but lose their mesh\n",
                   od.mesh.file.c_str());
        modelIndex[od.mesh.file] = models.size();
        models.push_back(std::move(m));
    }

    // 3) Reset skinning + scene state (the camera object is editor
    //    state; it is recreated at the end).
    Animator::Get().Reset();
    ClearScene();

    // 4) Rebuild the objects. Ids are remapped to fresh ones; parenting
    //    is fixed up in a second pass (parents may appear later in the
    //    file). Skinned sub-meshes are re-registered with the Animator.
    std::vector<SceneObject> built;
    std::unordered_map<uint32_t, uint32_t> oldToNew;
    built.reserve(data.objects.size());

    for (const auto& od : data.objects) {
        SceneObject obj;
        obj.id = s_nextSceneObjectId++;
        obj.name = od.name;
        obj.position = Vector3(od.position);
        obj.rotation = Vector3(od.rotation);
        obj.scale = Vector3(od.scale);
        obj.isGroup = od.isGroup;
        obj.material = MaterialFromData(od.material);

        // Mesh + embedded-texture restoration
        int rawIdx = -1;
        const FBXModel* model = nullptr;
        if (!od.mesh.builtin.empty()) {
            obj.mesh = GetPrimitiveMesh(od.mesh.builtin.c_str());
            if (!obj.mesh)
                printf("[Engine] LoadSceneFile: unknown builtin mesh '%s'\n",
                       od.mesh.builtin.c_str());
        } else if (!od.mesh.file.empty() && !od.mesh.submesh.empty()) {
            auto mit = modelIndex.find(od.mesh.file);
            if (mit == modelIndex.end() || !models[mit->second].success)
                printf("[Engine] LoadSceneFile: no import for '%s'\n", od.mesh.file.c_str());
            else {
                model = &models[mit->second];
                rawIdx = FindRawMeshIndex(*model, od.mesh.submesh);
                if (rawIdx < 0)
                    printf("[Engine] LoadSceneFile: submesh '%s' not found in '%s'\n",
                           od.mesh.submesh.c_str(), od.mesh.file.c_str());
                else {
                    obj.mesh = CreateMesh(AssetLoader::MergeSubMesh(*model, static_cast<size_t>(rawIdx)));
                    obj.meshFile = od.mesh.file;
                    obj.meshSubmesh = od.mesh.submesh;

                    if (od.material.diffuseTexture == kEmbeddedTextureMarker) {
                        obj.material.diffuseTexture = ExtractEmbeddedTexture(*model, rawIdx, false);
                        if (!obj.material.diffuseTexture)
                            printf("[Engine] LoadSceneFile: could not re-extract embedded diffuse for '%s'\n",
                                   od.mesh.submesh.c_str());
                    }
                    if (od.material.normalTexture == kEmbeddedTextureMarker) {
                        obj.material.normalTexture = ExtractEmbeddedTexture(*model, rawIdx, true);
                        if (!obj.material.normalTexture)
                            printf("[Engine] LoadSceneFile: could not re-extract embedded normal for '%s'\n",
                                   od.mesh.submesh.c_str());
                    }
                }
            }
        }

        // File-path textures (embedded markers were resolved above;
        // anything left non-empty is a disk path)
        if (!od.material.diffuseTexture.empty() && od.material.diffuseTexture != kEmbeddedTextureMarker)
            obj.material.diffuseTexture = LoadTexture(od.material.diffuseTexture);
        if (!od.material.normalTexture.empty() && od.material.normalTexture != kEmbeddedTextureMarker)
            obj.material.normalTexture = LoadTexture(od.material.normalTexture);

        // Skinned sub-mesh: re-register bind data so a (re-bound)
        // animation can drive this object
        if (model && rawIdx >= 0 && model->rawMeshes[rawIdx].isSkinned()) {
            const RawMeshData& raw = model->rawMeshes[rawIdx];
            MeshSkinBinding bind;
            bind.meshName = od.mesh.submesh;
            bind.Wmesh = raw.meshWorldRest;
            for (const auto& mb : raw.meshBones) {
                bind.boneNames.push_back(mb.name);
                bind.IB.push_back(mb.IB);
                bind.boneRestWorld.push_back(mb.restWorld);
            }
            if (bind.valid())
                Animator::Get().RegisterObject(obj.id, bind);
        }

        oldToNew[od.id] = obj.id;
        built.push_back(std::move(obj));
    }

    // Parent fix-up (old parent id → new id; unknown parents stay top-level)
    for (size_t i = 0; i < built.size(); ++i) {
        uint32_t oldParent = data.objects[i].parent;
        if (oldParent == 0) continue;
        auto it = oldToNew.find(oldParent);
        if (it != oldToNew.end() && it->second != built[i].id)
            built[i].parentId = it->second;
    }

    s_sceneObjects.insert(s_sceneObjects.end(),
                          std::make_move_iterator(built.begin()),
                          std::make_move_iterator(built.end()));

    // 5) Re-bind embedded animations (first model that carries any,
    //    mirroring the editor's FBX import behavior).
    for (auto& m : models) {
        if (m.success && !m.animations.empty()) {
            AnimationFile af;
            af.filename = m.filename;
            af.nodes = std::move(m.nodes);
            af.clips = std::move(m.animations);
            af.success = true;
            Animator::Get().Bind(af);
            break;
        }
    }

    // 6) Recreate the camera scene object (editor state, not in the file).
    if (s_cameraObjectId == 0) CreateCameraObject();

    SetSceneFilePath(path);
    printf("[Engine] Scene loaded: %s (%zu object(s))\n", path.c_str(), built.size());
    return true;
}

const std::string& GetSceneFilePath() { return g_sceneFilePath; }
void SetSceneFilePath(const std::string& path) { g_sceneFilePath = path; }

} // namespace CoreEngine
