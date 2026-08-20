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
    };

    // Raw mesh data (used by FBX loader for merging)
    struct RawMeshData {
        std::string name;
        std::vector<float> vertices;
        std::vector<uint32_t> indices;
    };

    // Loaded FBX (from asset_loader.cpp)
    struct FBXModel {
        bool success = false;
        std::string filename;
        std::vector<PrimitiveMesh> meshes;
        std::vector<RawMeshData> rawMeshes;
    };

    // Shared GPU mesh handle. When the last reference is dropped, the
    // VAO/VBO/EBO are deleted. Scene objects and primitive templates
    // can safely share the same GPU mesh.
    using MeshPtr = std::shared_ptr<PrimitiveMesh>;
    MeshPtr CreateMesh(PrimitiveMesh mesh);

    // Scene object — placed by editor
    struct SceneObject {
        uint32_t id = 0;
        std::string name;
        MeshPtr mesh;
        Vector3 position  = {0, 0, 0};
        Vector3 rotation  = {0, 0, 0};   // Euler radians
        Vector3 scale     = {1, 1, 1};
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
    SceneObject& AddToScene(const std::string& name, MeshPtr mesh);
    void ClearScene();
    void RemoveFromScene(uint32_t id);
    void SelectObject(uint32_t id);
    SceneObject* GetSelectedObject();
    uint32_t GetSelectedObjectId();
    uint32_t GetNextSceneObjectId();

    // Camera (orbit-style)
    void SetCameraPosition(Vector3 pos);
    void SetCameraTarget(Vector3 target);
    void SetCameraDirection(Vector3 dir);
    Vector3 GetCameraPosition();
    Vector3 GetCameraTarget();
    Vector3 GetCameraDirection();
    Vector3 GetCameraOffset();
    void SetCameraOffset(Vector3 offset);
    glm::mat4 GetProjectionMatrix(float fov, float aspect);
    GLuint GetModelUniformLocation(GLuint prog, bool& found);

    // Internal helpers (used by editor)
    GLuint GetShaderProgram();
    MeshPtr GetPrimitiveMesh(const char* name);

    // 3D grid rendering
    void DrawGrid(int divisions = 20, float unit = 1.0f, float halfExtent = 10.0f);

    // Bounding box wireframe for selected object
    void DrawSelectedObjectBounds();

} // namespace CoreEngine
