// Unit test for the PURE layer of the .scene file format
// (CoreEngine::SerializeSceneFileData / ParseSceneFileData).
//
// No GL context is needed: only the data model + JSON round-trip are
// exercised. The live-scene bridge (SaveSceneFile / LoadSceneFile) needs
// GL + real FBX assets and is verified manually in the editor.
//
// Build with build_test_scene_file.bat, run from anywhere.

#include "core/scene_file.h"

#include <cstdio>
#include <string>

using namespace CoreEngine;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++g_failures; \
    } \
} while (0)

// ── Comparison helpers ─────────────────────────────────────────────

static bool SameMaterial(const SceneMaterialData& a, const SceneMaterialData& b) {
    return a.name == b.name
        && a.baseColor == b.baseColor
        && a.emissiveColor == b.emissiveColor
        && a.metallic == b.metallic
        && a.roughness == b.roughness
        && a.ao == b.ao
        && a.useMaterial == b.useMaterial
        && a.diffuseTexture == b.diffuseTexture
        && a.normalTexture == b.normalTexture;
}

static bool SameMeshRef(const SceneMeshRef& a, const SceneMeshRef& b) {
    return a.builtin == b.builtin && a.file == b.file && a.submesh == b.submesh;
}

static bool SameObject(const SceneObjectData& a, const SceneObjectData& b) {
    return a.id == b.id
        && a.name == b.name
        && a.parent == b.parent
        && a.isGroup == b.isGroup
        && a.position == b.position
        && a.rotation == b.rotation
        && a.scale == b.scale
        && SameMeshRef(a.mesh, b.mesh)
        && SameMaterial(a.material, b.material);
}

static bool SameData(const SceneFileData& a, const SceneFileData& b) {
    if (a.version != b.version) return false;
    if (a.smoothNormals != b.smoothNormals) return false;
    if (a.objects.size() != b.objects.size()) return false;
    for (size_t i = 0; i < a.objects.size(); ++i)
        if (!SameObject(a.objects[i], b.objects[i])) return false;
    return true;
}

// ── Tests ──────────────────────────────────────────────────────────

static void TestRoundTrip() {
    printf("Test: full round-trip\n");

    SceneFileData in;
    in.smoothNormals = true;

    // 1) Transform group (model root) — no mesh
    SceneObjectData root;
    root.id = 1;
    root.name = "Arissa@Idle";
    root.isGroup = true;
    root.position = glm::vec3(0.5f, 0.25f, -1.5f);
    root.rotation = glm::vec3(0.0f, 0.7853981f, 0.0f);
    root.scale = glm::vec3(1.0f, 1.0f, 1.0f);
    in.objects.push_back(root);

    // 2) FBX part with EMBEDDED (FBX-baked) textures. The file path
    //    deliberately contains backslashes + a space + '@' to exercise
    //    JSON escaping.
    SceneObjectData part;
    part.id = 2;
    part.name = "Arissa_Joint";
    part.parent = 1;
    part.position = glm::vec3(-0.1f, 1.7f, 0.02f);
    part.rotation = glm::vec3(0.01f, -0.02f, 0.005f);
    part.scale = glm::vec3(0.99f, 1.01f, 0.98f);
    part.mesh.file = "C:\\Dev\\shadow engine\\tools\\Arissa@Idle.fbx";
    part.mesh.submesh = "Arissa_Joint";
    part.material.name = "Arissa_Joint_material";
    part.material.baseColor = glm::vec3(0.9f, 0.8f, 0.7f);
    part.material.emissiveColor = glm::vec3(0.0f, 0.1f, 0.0f);
    part.material.metallic = 0.2f;
    part.material.roughness = 0.8f;
    part.material.ao = 0.9f;
    part.material.useMaterial = true;
    part.material.diffuseTexture = "embedded";
    part.material.normalTexture = "embedded";
    in.objects.push_back(part);

    // 3) Built-in primitive with a file-path texture
    SceneObjectData cube;
    cube.id = 3;
    cube.name = "my_cube";
    cube.mesh.builtin = "cube";
    cube.material.name = "cube_mat";
    cube.material.useMaterial = false;
    cube.material.diffuseTexture = "assets/textures/checker.png";
    cube.material.roughness = 0.35f;
    in.objects.push_back(cube);

    // 4) Hostile name: quotes, backslash, newlines, non-ASCII. The UTF-8
    //    bytes are appended one by one because C++ \x hex escapes are
    //    greedy (they swallow following hex-digit characters).
    SceneObjectData weird;
    weird.id = 4;
    weird.name = "we\"ird \\ name\nline2 ";
    auto utf8 = [&](unsigned hi, unsigned lo) {
        weird.name += (char)hi;
        weird.name += (char)lo;
    };
    utf8(0xC3, 0xBC);  // u-umlaut
    utf8(0xC3, 0xAF);  // i-diaeresis
    utf8(0xC3, 0xB8);  // o-slash
    weird.name += 'd';
    utf8(0xC3, 0xA9);  // e-acute
    weird.parent = 2;
    in.objects.push_back(weird);

    std::string json = SerializeSceneFileData(in);
    CHECK(!json.empty());
    CHECK(json.find("\"shadowScene\": 1") != std::string::npos);
    CHECK(json.find("\"smoothNormals\": true") != std::string::npos);

    SceneFileData out;
    std::string err;
    bool ok = ParseSceneFileData(json.data(), json.size(), out, &err);
    CHECK(ok);
    if (!ok) printf("  parse error: %s\n", err.c_str());
    CHECK(SameData(in, out));
    if (!SameData(in, out)) {
        printf("  round-trip mismatch (%zu objects in, %zu out)\n",
               in.objects.size(), out.objects.size());
        for (size_t i = 0; i < in.objects.size() && i < out.objects.size(); ++i)
            printf("   obj %zu: in='%s' out='%s'\n", i,
                   in.objects[i].name.c_str(), out.objects[i].name.c_str());
    }
}

static void TestEmptyScene() {
    printf("Test: empty scene\n");
    SceneFileData in;  // version 1, no objects
    std::string json = SerializeSceneFileData(in);

    SceneFileData out;
    std::string err;
    CHECK(ParseSceneFileData(json.data(), json.size(), out, &err));
    CHECK(SameData(in, out));
}

static void TestMalformed() {
    printf("Test: malformed input rejected\n");
    SceneFileData out;
    std::string err;

    struct Case { const char* what; const char* text; };
    const Case cases[] = {
        { "garbage",            "{ this is not json" },
        { "empty string",       "" },
        { "root array",         "[1,2,3]" },
        { "missing objects",    "{ \"shadowScene\": 1 }" },
        { "objects not array",  "{ \"shadowScene\": 1, \"objects\": 5 }" },
        { "no version field",   "{ \"objects\": [] }" },
        { "unsupported version","{ \"shadowScene\": 999, \"objects\": [] }" },
        { "object not object",  "{ \"shadowScene\": 1, \"objects\": [42] }" },
        { "bad vec3 size",      "{ \"shadowScene\": 1, \"objects\": "
                                "[{ \"id\": 1, \"pos\": [1.0, 2.0] }]" },
        { "material not object","{ \"shadowScene\": 1, \"objects\": "
                                "[{ \"id\": 1, \"material\": \"x\" }]" },
    };
    for (const auto& c : cases) {
        err.clear();
        bool ok = ParseSceneFileData(c.text, std::string(c.text).size(), out, &err);
        CHECK(!ok);
        CHECK(!err.empty());
        if (ok) printf("  unexpectedly accepted: %s\n", c.what);
    }

    // null / zero-size
    err.clear();
    CHECK(!ParseSceneFileData(nullptr, 0, out, &err));
}

static void TestOmittedFieldsUseDefaults() {
    printf("Test: omitted fields default\n");
    const char* text =
        "{ \"shadowScene\": 1, \"objects\": [ { \"id\": 7, \"name\": \"bare\" } ] }";
    SceneFileData out;
    std::string err;
    CHECK(ParseSceneFileData(text, std::string(text).size(), out, &err));
    CHECK(out.objects.size() == 1);
    const SceneObjectData& o = out.objects[0];
    CHECK(o.id == 7);
    CHECK(o.name == "bare");
    CHECK(o.parent == 0);
    CHECK(!o.isGroup);
    CHECK(o.position == glm::vec3(0.0f));
    CHECK(o.scale == glm::vec3(1.0f));
    CHECK(!o.mesh.valid());
    CHECK(o.material.roughness == 1.0f);
    CHECK(o.material.ao == 1.0f);
    CHECK(o.material.useMaterial == true);
    CHECK(o.material.diffuseTexture.empty());
}

static void TestUnnamedObjectGetsName() {
    printf("Test: object without name gets a fallback\n");
    const char* text =
        "{ \"shadowScene\": 1, \"objects\": [ { \"id\": 42 } ] }";
    SceneFileData out;
    CHECK(ParseSceneFileData(text, std::string(text).size(), out));
    CHECK(out.objects.size() == 1);
    CHECK(out.objects[0].name == "object_42");
}

static void TestVersionAndFlagRoundTrip() {
    printf("Test: version + smoothNormals flag\n");
    SceneFileData in;
    in.smoothNormals = false;
    in.version = 1;
    std::string json = SerializeSceneFileData(in);
    SceneFileData out;
    CHECK(ParseSceneFileData(json.data(), json.size(), out));
    CHECK(out.version == 1);
    CHECK(out.smoothNormals == false);
}

int main() {
    printf("== scene file pure-layer tests ==\n");

    TestRoundTrip();
    TestEmptyScene();
    TestMalformed();
    TestOmittedFieldsUseDefaults();
    TestUnnamedObjectGetsName();
    TestVersionAndFlagRoundTrip();

    printf("== %d checks, %d failure(s) ==\n", g_checks, g_failures);
    if (g_failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    return 1;
}
