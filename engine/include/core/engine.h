#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace CoreEngine {

    struct Vector3 {
        float x, y, z;
        Vector3() : x(0), y(0), z(0) {}
        Vector3(float xx, float yy, float zz) : x(xx), y(yy), z(zz) {}
        Vector3(glm::vec3 v) : x(v.x), y(v.y), z(v.z) {}
        Vector3 operator-(const Vector3& other) const { return Vector3(x - other.x, y - other.y, z - other.z); }
        Vector3 operator+(const Vector3& other) const { return Vector3(x + other.x, y + other.y, z + other.z); }
    };

    struct EngineInfo {
        int majorVersion;
        int minorVersion;
        std::string name;
    };

    // Vertex layout: position(3) + normal(3) + uv(2) = 8 floats
    struct VertexPositionNormalUV {
        float pos[3];
        float normal[3];
        float uv[2];
        static int stride() { return sizeof(VertexPositionNormalUV); }
    };

    // Mesh data (produced by asset loader or primitives)
    struct PrimitiveMesh {
        std::string name;
        GLuint VAO = 0;
        GLuint VBO = 0;
        GLuint EBO = 0;
        uint32_t indexCount = 0;
        Vector3 halfExtent = {1.0f, 1.0f, 1.0f};  // AABB half-extents for bounding wireframe
        Vector3 center = {0.0f, 0.0f, 0.0f};      // AABB center in mesh-local space
    };

    // Raw mesh data (used by FBX loader for merging)
    struct RawMeshData {
        std::string name;
        int materialIndex = 0;  // index into FBX material list (aiMesh::mMaterialIndex)
        std::vector<float> vertices;
        std::vector<uint32_t> indices;
    };

    // Loaded FBX (from asset_loader.cpp)
    struct FBXModel {
        bool success = false;
        std::string filename;
        std::vector<PrimitiveMesh> meshes;
        std::vector<RawMeshData> rawMeshes;
        glm::vec3 materialColor = glm::vec3(0.7f);  // Diffuse color from FBX material
        // Embedded textures extracted from FBX (decoded pixel data)
        struct EmbeddedTexture {
            unsigned char* data = nullptr;
            int width = 0;
            int height = 0;
            int channels = 0;
        };
        std::vector<EmbeddedTexture> textures;
        // Indices into `textures` for the first material's texture slots (-1 = none)
        int diffuseTextureIndex = -1;
        int normalTextureIndex = -1;
        // Per-scene-material texture slot mapping + diffuse colors, so each
        // sub-mesh can be rendered with ITS OWN material's texture.
        struct MaterialTextureMap {
            int diffuseIndex = -1;  // index into textures (-1 = none)
            int normalIndex = -1;   // index into textures (-1 = none)
        };
        std::vector<MaterialTextureMap> materialTextures;  // one entry per scene material
        std::vector<glm::vec3> materialColors;             // diffuse color per scene material
    };

    // Shared GPU mesh handle. When the last reference is dropped, the
    // VAO/VBO/EBO are deleted. Scene objects and primitive templates
    // can safely share the same GPU mesh.
    using MeshPtr = std::shared_ptr<PrimitiveMesh>;
    MeshPtr CreateMesh(PrimitiveMesh mesh);

    // ── Textures ────────────────────────────────────────────────────
    // GPU textures are reference-counted: the GL texture is deleted when
    // the last TexturePtr referencing it is destroyed. Materials can
    // freely copy/share the same texture.

    struct Texture {
        GLuint id = 0;
        int width = 0;
        int height = 0;
        int channels = 0;
    };
    using TexturePtr = std::shared_ptr<Texture>;
    TexturePtr LoadTexture(const std::string& path);
    TexturePtr LoadTextureFromMemory(const unsigned char* data, int width, int height, int channels);
    void BindTexture(const TexturePtr& tex, GLuint unit);

    // ── Materials ───────────────────────────────────────────────────

    struct Material {
        std::string name = "default";
        glm::vec3 baseColor = glm::vec3(0.5f);
        glm::vec3 emissiveColor = glm::vec3(0.0f);
        float metallic = 0.0f;    // 0 = non-metal, 1 = metal
        float roughness = 1.0f;   // 0 = polished, 1 = rough
        float ao = 1.0f;          // ambient occlusion multiplier
        TexturePtr diffuseTexture;   // nullptr means no texture
        TexturePtr normalTexture;    // nullptr means no texture
        bool useMaterial = false;
    };
    Material CreateDefaultMaterial();

    // Scene object — placed by editor (mesh + transform + material)
    struct SceneObject {
        uint32_t id = 0;
        std::string name;
        MeshPtr mesh;
        Vector3 position  = {0, 0, 0};
        Vector3 rotation  = {0, 0, 0};   // Euler radians
        Vector3 scale     = {1, 1, 1};
        Material material;                // PBR material
    };

    static constexpr const char* ENGINE_NAME   = "ShadowEngine";
    static constexpr int         VERSION_MAJOR   = 0;
    static constexpr int         VERSION_MINOR   = 2;

    // Lifecycle
    void Init();
    void Shutdown();
    std::string GetEngineName();
    void GetVersion(int& major, int& minor);
    EngineInfo GetEngineInfo(int width, int height);

    // Renderer (existing)
    bool InitRenderer(const char* title, int width, int height);
    GLFWwindow* GetWindow();
    void RenderBegin();
    void RenderEnd();
    bool ShouldClose();

    // Shaders
    GLuint CompileShader(GLenum type, const char* source);
    GLuint CreateShaderProgram(const char* vsSource, const char* fsSource);
    void UseShader(GLuint program);
    void SetUniformMat4(GLuint program, const char* name, const glm::mat4& m);
    void SetUniformVec3(GLuint program, const char* name, const glm::vec3& v);

    // Mesh primitives (cube, plane — useful in editor/for testing)
    void InitPrimitiveMeshes();
    PrimitiveMesh CreateBox(const Vector3& size);
    PrimitiveMesh CreatePlane(float width=1.0f, float height=1.0f);
    void DestroyMesh(PrimitiveMesh& mesh);

    // Scene management
    std::vector<SceneObject>& GetSceneObjects();
    SceneObject& AddToScene(const std::string& name, MeshPtr mesh, Material mat);
    void ClearScene();
    void RemoveFromScene(uint32_t id);
    void SelectObject(uint32_t id);
    SceneObject* GetSelectedObject();
    uint32_t GetSelectedObjectId();
    uint32_t GetNextSceneObjectId();

    // Camera (orbit-style)
    void SetCameraPosition(Vector3 pos);
    void SetCameraTarget(Vector3 target);
    void ResetCamera(); // Reset to initial default camera state
    void SetCameraDirection(Vector3 dir);
    Vector3 GetCameraPosition();
    Vector3 GetCameraTarget();
    Vector3 GetCameraDirection();
    Vector3 GetCameraOffset();
    void SetCameraOffset(Vector3 offset);
    glm::mat4 GetProjectionMatrix(float fov, float aspect);
    glm::mat4 GetProjectionMatrix(float fov, float aspect, float nearPlane, float farPlane);
    GLuint GetModelUniformLocation(GLuint prog, bool& found);

    // Internal helpers (used by editor)
    GLuint GetShaderProgram();
    MeshPtr GetPrimitiveMesh(const char* name);

    // 3D grid rendering
    void DrawGrid(int divisions, float unit, float halfExtent, const glm::mat4& view, const glm::mat4& projection);

    // Bounding box wireframe for selected object
    void DrawSelectedObjectBounds(const glm::mat4& view, const glm::mat4& projection);

    // Skybox (procedural gradient with sun)
    void InitSkybox();
    void DrawSkybox(glm::vec3 cameraPosition, float aspect = 1280.0f / 720.0f);

    // ── Camera (as a scene object) ──────────────────────────────────
    uint32_t GetCameraObjectId();
    void SetCameraId(uint32_t id);
    bool IsCameraObjectId(uint32_t id);
    void CreateCameraObject();          // create camera visual in scene
    void SyncSceneToCameraObject();     // editor: pull object pos → orbit camera

    // ── Shadow Mapping ──────────────────────────────────────────────

    struct ShadowMap {
        GLuint fbo = 0;
        GLuint depthTexture = 0;
        GLuint depthRenderbuffer = 0;
        int width = 2048;
        int height = 2048;
        bool inited = false;
    };

    // Light direction in world space (normalized)
    void SetShadowLightDirection(Vector3 dir);
    Vector3 GetShadowLightDirection();
    glm::mat4 GetLightViewMatrix();
    glm::mat4 GetLightProjectionMatrix();
    glm::mat4 GetLightSpaceMatrix();

    // Shadow map initialization / rendering
    void InitShadowMap(int width = 2048, int height = 2048);
    void DrawShadowPass();          // Render scene to shadow map

    void CleanupShadowMap();        // Free shadow map FBO + texture

    // Get the shadow map for use in shaders
    GLuint GetShadowMapTexture();
    GLuint GetShadowMapFBO();
    int GetShadowMapWidth();
    int GetShadowMapHeight();

} // namespace CoreEngine
