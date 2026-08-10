#include "core/engine.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <cstdlib>

// ── Default shader source ───────────────────────────────────────────

static const char* default_vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vNormal;
out vec2 vUV;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
}
)";

static const char* default_fs = R"(
#version 330 core
in vec3 vNormal;
in vec2 vUV;

out vec4 FragColor;

uniform vec3 uColor;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(dot(normalize(vNormal), lightDir), 0.0);
    vec3 ambient = 0.3 * uColor;
    vec3 result = ambient + diff * uColor;
    FragColor = vec4(result, 1.0);
}
)";

// ── Static state ────────────────────────────────────────────────────

static GLFWwindow* s_window     = nullptr;
static int         s_width      = 0;
static int         s_height     = 0;
static GLuint      s_shaderProg = 0;
static GLuint      s_vao        = 0;
static GLuint      s_vbo        = 0;

static CoreEngine::Vector3 s_cameraPos    = {0, 1.5f, 5};
static CoreEngine::Vector3 s_cameraDir    = {0, -1, -5};

static std::vector<CoreEngine::SceneObject> s_sceneObjects;
static std::vector<CoreEngine::PrimitiveMesh> s_primitiveMeshes;

static bool s_engineInited = false;

// ── Helpers ─────────────────────────────────────────────────────────

static void compileDefaultShader() {
    GLuint vs = CoreEngine::CompileShader(GL_VERTEX_SHADER, default_vs);
    GLuint fs = CoreEngine::CompileShader(GL_FRAGMENT_SHADER, default_fs);
    s_shaderProg = CoreEngine::CreateShaderProgram(default_vs, default_fs);
    glUseProgram(s_shaderProg);

    // Set initial camera uniforms
    auto view = glm::lookAt(
        glm::vec3(s_cameraPos.x, s_cameraPos.y, s_cameraPos.z),
        glm::vec3(s_cameraPos.x + s_cameraDir.x, s_cameraPos.y + s_cameraDir.y, s_cameraPos.z + s_cameraDir.z),
        glm::vec3(0, 1, 0));
    GLint viewLoc = glGetUniformLocation(s_shaderProg, "uView");
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
}

static void buildPrimitiveVAOs() {
    // Clear any existing primitive mesh VAOs
    for (auto& m : s_primitiveMeshes) {
        if (m.VAO) { glDeleteVertexArrays(1, &m.VAO); m.VAO = 0; }
        if (m.VBO) { glDeleteBuffers(1, &m.VBO); m.VBO = 0; }
        if (m.EBO) { glDeleteBuffers(1, &m.EBO); m.EBO = 0; }
    }
    s_primitiveMeshes.clear();

    // Build cube mesh data manually (not via CreateBox which is in namespace)
    CoreEngine::PrimitiveMesh cube;
    cube.name = "cube";
    cube.indexCount = 36;

    static const float cubeVerts[] = {
        // Front face (+Z)
        -0.5f, -0.5f,  0.5f,   0,0,1,  0,0,
         0.5f, -0.5f,  0.5f,   0,0,1,  1,0,
         0.5f,  0.5f,  0.5f,   0,0,1,  1,1,
        -0.5f,  0.5f,  0.5f,   0,0,1,  0,1,
        // Back face (-Z)
        -0.5f, -0.5f, -0.5f,   0,0,-1,  0,0,
        -0.5f,  0.5f, -0.5f,   0,0,-1,  1,0,
         0.5f,  0.5f, -0.5f,   0,0,-1,  1,1,
         0.5f, -0.5f, -0.5f,   0,0,-1,  0,1,
        // Top face (+Y)
        -0.5f,  0.5f,  0.5f,   0,1,0,  0,0,
         0.5f,  0.5f,  0.5f,   0,1,0,  1,0,
         0.5f,  0.5f, -0.5f,   0,1,0,  1,1,
        -0.5f,  0.5f, -0.5f,   0,1,0,  0,1,
        // Bottom face (-Y)
        -0.5f, -0.5f, -0.5f,   0,-1,0,  0,0,
         0.5f, -0.5f, -0.5f,   0,-1,0,  1,0,
         0.5f, -0.5f,  0.5f,   0,-1,0,  1,1,
        -0.5f, -0.5f,  0.5f,   0,-1,0,  0,1,
        // Right face (+X)
         0.5f, -0.5f,  0.5f,   1,0,0,  0,0,
         0.5f, -0.5f, -0.5f,   1,0,0,  1,0,
         0.5f,  0.5f, -0.5f,   1,0,0,  1,1,
         0.5f,  0.5f,  0.5f,   1,0,0,  0,1,
        // Left face (-X)
        -0.5f, -0.5f, -0.5f,  -1,0,0,  0,0,
        -0.5f, -0.5f,  0.5f,  -1,0,0,  1,0,
        -0.5f,  0.5f,  0.5f,  -1,0,0,  1,1,
        -0.5f,  0.5f, -0.5f,  -1,0,0,  0,1,
    };
    static const GLuint cubeIndices[] = {
        // Front (+Z): CCW from +Z
        0,1,2, 0,2,3,
        // Back (-Z): CCW from -Z
        4,6,5, 4,7,6,
        // Top (+Y): CCW from +Y
        8,9,10, 8,10,11,
        // Bottom (-Y): CCW from -Y
        12,13,14, 12,14,15,
        // Right (+X): CCW from +X
        16,17,18, 16,18,19,
        // Left (-X): CCW from -X
        20,22,21, 20,23,22
    };

    glGenVertexArrays(1, &cube.VAO);
    glGenBuffers(1, &cube.VBO);
    glGenBuffers(1, &cube.EBO);
    glBindVertexArray(cube.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, cube.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    s_primitiveMeshes.push_back(cube);

    // Build plane mesh
    CoreEngine::PrimitiveMesh plane;
    plane.name = "plane";
    plane.indexCount = 6;

    static const float planeVerts[] = {
        -5.0f, 0, -5.0f,   0,1,0,  0,0,
         5.0f, 0, -5.0f,   0,1,0,  1,0,
         5.0f, 0,  5.0f,   0,1,0,  1,1,
        -5.0f, 0,  5.0f,   0,1,0,  0,1,
    };
    static const GLuint planeIndices[] = { 0, 1, 2, 0, 2, 3 };

    glGenVertexArrays(1, &plane.VAO);
    glGenBuffers(1, &plane.VBO);
    glGenBuffers(1, &plane.EBO);
    glBindVertexArray(plane.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, plane.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(planeVerts), planeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, plane.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(planeIndices), planeIndices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    s_primitiveMeshes.push_back(plane);
}

// ── Engine ──────────────────────────────────────────────────────────

namespace CoreEngine {

void Init() {
    if (s_engineInited) return;
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    s_engineInited = true;
}

void Shutdown() {
    if (!s_engineInited) return;

    glDeleteVertexArrays(1, &s_vao);
    glDeleteBuffers(1, &s_vbo);
    if (s_shaderProg) glDeleteProgram(s_shaderProg);

    for (auto& m : s_primitiveMeshes) {
        if (m.VAO) glDeleteVertexArrays(1, &m.VAO);
        if (m.VBO) glDeleteBuffers(1, &m.VBO);
        if (m.EBO) glDeleteBuffers(1, &m.EBO);
    }

    if (s_window) glfwDestroyWindow(s_window);
    s_window = nullptr;
    glfwTerminate();
    s_engineInited = false;
}

std::string GetEngineName() { return ENGINE_NAME; }

void GetVersion(int& major, int& minor) { major = VERSION_MAJOR; minor = VERSION_MINOR; }

EngineInfo GetEngineInfo(int width, int height) {
    EngineInfo info;
    info.name = ENGINE_NAME;
    info.majorVersion = VERSION_MAJOR;
    info.minorVersion = VERSION_MINOR;
    return info;
}

// ── Renderer ────────────────────────────────────────────────────────

bool InitRenderer(const char* title, int width, int height) {
    s_width = width;
    s_height = height;

    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_SAMPLES, 1);

    s_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!s_window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        return false;
    }
    glfwMakeContextCurrent(s_window);
    glfwSetFramebufferSizeCallback(s_window, [](GLFWwindow* win, int w, int h) {
        s_width = w;
        s_height = h;
        glViewport(0, 0, w, h);
    });

        if (glewInit() != GLEW_OK) {
            fprintf(stderr, "Failed to initialize GLEW\n");
            return false;
        }

    printf("OpenGL version: %s\n", glGetString(GL_VERSION));
    printf("GLSL version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearDepthf(1.0f);
    glClearDepth(1.0);

    compileDefaultShader();
    buildPrimitiveVAOs();

    glfwShowWindow(s_window);
    return true;
}

GLFWwindow* GetWindow() { return s_window; }

void RenderBegin() {
    glViewport(0, 0, s_width, s_height);
    glClearColor(0.2f, 0.2f, 0.25f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthMask(GL_TRUE);
}

void RenderEnd() {
    glfwSwapBuffers(s_window);
}

bool ShouldClose() {
    return glfwWindowShouldClose(s_window) != 0;
}

// ── Shaders ─────────────────────────────────────────────────────────

GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint len;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        char* log = (char*)alloca(len);
        glGetShaderInfoLog(shader, len, nullptr, log);
        fprintf(stderr, "Shader compile error:\n%s\n", log);
    }
    return shader;
}

GLuint CreateShaderProgram(const char* vsSource, const char* fsSource) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsSource, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSource, nullptr);
    glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        GLint len;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        char* log = (char*)alloca(len);
        glGetProgramInfoLog(prog, len, nullptr, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

void UseShader(GLuint program) {
    s_shaderProg = program;
    glUseProgram(program);
}

void SetUniformMat4(GLuint program, const char* name, const glm::mat4& m) {
    glUniformMatrix4fv(glGetUniformLocation(program, name), 1, GL_FALSE, glm::value_ptr(m));
}

void SetUniformVec3(GLuint program, const char* name, const glm::vec3& v) {
    glUniform3fv(glGetUniformLocation(program, name), 1, glm::value_ptr(v));
}

// ── Mesh primitives ─────────────────────────────────────────────────

void InitPrimitiveMeshes() {
    buildPrimitiveVAOs();
}

PrimitiveMesh CreateBox(const Vector3& size) {
    PrimitiveMesh mesh;
    mesh.name = "box";

    // 8 vertices, 36 indices (6 faces x 2 tris x 3 verts)
    static const float verts[] = {
        // position (3) + normal (3) + uv (2)
        // Front face
        -size.x/2, -size.y/2,  size.z/2,   0,0,1,   0,0,
         size.x/2, -size.y/2,  size.z/2,   0,0,1,   1,0,
         size.x/2,  size.y/2,  size.z/2,   0,0,1,   1,1,
        -size.x/2,  size.y/2,  size.z/2,   0,0,1,   0,1,
        // Back face
         size.x/2, -size.y/2, -size.z/2,   0,0,-1,  0,0,
        -size.x/2, -size.y/2, -size.z/2,   0,0,-1,  1,0,
        -size.x/2,  size.y/2, -size.z/2,   0,0,-1,  1,1,
         size.x/2,  size.y/2, -size.z/2,   0,0,-1,  0,1,
        // Top face
        -size.x/2,  size.y/2,  size.z/2,   0,1,0,   0,0,
        -size.x/2,  size.y/2, -size.z/2,   0,1,0,   0,1,
         size.x/2,  size.y/2, -size.z/2,   0,1,0,   1,1,
         size.x/2,  size.y/2,  size.z/2,   0,1,0,   1,0,
        // Bottom face
        -size.x/2, -size.y/2, -size.z/2,   0,-1,0,  0,0,
         size.x/2, -size.y/2, -size.z/2,   0,-1,0,  1,0,
         size.x/2, -size.y/2,  size.z/2,   0,-1,0,  1,1,
        -size.x/2, -size.y/2,  size.z/2,   0,-1,0,  0,1,
        // Right face
         size.x/2, -size.y/2,  size.z/2,   1,0,0,   0,0,
         size.x/2, -size.y/2, -size.z/2,   1,0,0,   1,0,
         size.x/2,  size.y/2, -size.z/2,   1,0,0,   1,1,
         size.x/2,  size.y/2,  size.z/2,   1,0,0,   0,1,
        // Left face
        -size.x/2, -size.y/2, -size.z/2,  -1,0,0,   0,0,
        -size.x/2, -size.y/2,  size.z/2,  -1,0,0,   1,0,
        -size.x/2,  size.y/2,  size.z/2,  -1,0,0,   1,1,
        -size.x/2,  size.y/2, -size.z/2,  -1,0,0,   0,1,
    };

    static const GLuint indices[] = {
        0,1,2, 0,2,3,       // front
        4,5,6, 4,6,7,       // back
        8,9,10, 8,10,11,    // top
        12,13,14, 12,14,15, // bottom
        16,17,18, 16,18,19, // right
        20,21,22, 20,22,23  // left
    };

    mesh.indexCount = 36;

    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);

    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    // normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    // uv
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return mesh;
}

PrimitiveMesh CreatePlane(float width, float height) {
    PrimitiveMesh mesh;
    mesh.name = "plane";

    static const float verts[] = {
        // position (3) + normal (3) + uv (2)
        -width/2, 0, -height/2,   0,1,0,  0,0,
         width/2, 0, -height/2,   0,1,0,  1,0,
         width/2, 0,  height/2,   0,1,0,  1,1,
        -width/2, 0,  height/2,   0,1,0,  0,1,
    };

    static const GLuint indices[] = { 0, 1, 2, 0, 2, 3 };

    mesh.indexCount = 6;

    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);

    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return mesh;
}

void DestroyMesh(PrimitiveMesh& mesh) {
    if (mesh.VAO) { glDeleteVertexArrays(1, &mesh.VAO); mesh.VAO = 0; }
    if (mesh.VBO) { glDeleteBuffers(1, &mesh.VBO); mesh.VBO = 0; }
    if (mesh.EBO) { glDeleteBuffers(1, &mesh.EBO); mesh.EBO = 0; }
    mesh.indexCount = 0;
}

// ── Scene management ────────────────────────────────────────────────

std::vector<SceneObject>& GetSceneObjects() { return s_sceneObjects; }

SceneObject& AddToScene(const std::string& name, const PrimitiveMesh& mesh) {
    SceneObject obj;
    obj.mesh = mesh;
    obj.mesh.name = name;
    s_sceneObjects.push_back(obj);
    return s_sceneObjects.back();
}

void ClearScene() { s_sceneObjects.clear(); }

// ── Camera ──────────────────────────────────────────────────────────

void SetCameraPosition(Vector3 pos) { s_cameraPos = pos; }
void SetCameraDirection(Vector3 dir) { s_cameraDir = dir; }
Vector3 GetCameraPosition() { return s_cameraPos; }
Vector3 GetCameraDirection() { return s_cameraDir; }

glm::mat4 GetProjectionMatrix(float fov, float aspect) {
    return glm::perspective(glm::radians(fov), aspect, 0.1f, 100.0f);
}

GLuint GetModelUniformLocation(GLuint prog, bool& found) {
    GLint loc = glGetUniformLocation(prog, "uModel");
    found = (loc != -1);
    return (GLuint)loc;
}

GLuint GetShaderProgram() { return s_shaderProg; }

PrimitiveMesh* GetPrimitiveMesh(const char* name) {
    for (auto& m : s_primitiveMeshes) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

void RebuildSceneMeshes() {
    for (auto& obj : s_sceneObjects) {
        // Reset the scene object's mesh and re-copy from primitive template
        if (obj.mesh.VAO) { glDeleteVertexArrays(1, &obj.mesh.VAO); obj.mesh.VAO = 0; }
        if (obj.mesh.VBO) { glDeleteBuffers(1, &obj.mesh.VBO); obj.mesh.VBO = 0; }
        if (obj.mesh.EBO) { glDeleteBuffers(1, &obj.mesh.EBO); obj.mesh.EBO = 0; }
        obj.mesh.indexCount = 0;
    }
    s_sceneObjects.clear();
}

} // namespace CoreEngine
