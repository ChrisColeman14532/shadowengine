#include "core/engine.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#endif

// ── Shader file loader ─────────────────────────────────────────────
// Attempts to load a shader file from several possible locations:
//   1. shader/<filename>       (relative to current working directory)
//   2. <filename>              (relative to current directory)
//   3. Relative to executable path

static std::string LoadShaderSource(const std::string& filename) {
    // Track every path we try so a failure can print a full trace
    struct Attempt { std::string path; const char* source; };
    std::vector<Attempt> attempts;

    auto tryFile = [&](const std::string& path, const char* source) -> std::string {
        std::ifstream f(path);
        if (f.good()) {
            fprintf(stderr, "[Shader] Loaded '%s' from: %s\n", filename.c_str(), path.c_str());
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }
        attempts.push_back({path, source});
        return "";
    };

    // 1. SHADOW_ENGINE_SHADERS env var
    const char* env = std::getenv("SHADOW_ENGINE_SHADERS");
    if (env && env[0]) {
        std::string base(env);
        if (base.back() != '/' && base.back() != '\\') base += '/';
        std::string src = tryFile(base + filename, "env");
        if (!src.empty()) return src;
    }

    // 2/3. Relative to current working directory
    const std::vector<std::string> cwdCandidates = {
        "shader/" + filename,
        filename,
    };
    for (const auto& c : cwdCandidates) {
        std::string src = tryFile(c, "cwd");
        if (!src.empty()) return src;
    }

    // 4. Relative to executable directory (independent of CWD).
    //    Handles both '/' and '\\' separators.
    char buf[4096] = {0};
    bool haveExe = false;
#if defined(_WIN32)
    if (GetModuleFileNameA(nullptr, buf, sizeof(buf)) > 0) haveExe = true;
#else
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) { buf[len] = '\0'; haveExe = true; }
#endif
    std::string exePath(buf);
    std::string exeDir;
    if (haveExe) {
        exeDir = exePath;
        auto slash = exeDir.find_last_of("/\\");
        if (slash != std::string::npos) exeDir.erase(slash + 1);  // keep trailing separator
        else exeDir = ".";

        // Ensure a trailing separator, then use the path's own separator
        // style so the printed path is clean
        if (exeDir.back() != '/' && exeDir.back() != '\\') exeDir += '/';
        const std::string sep(1, exeDir.back());

        // CMake copies shaders next to the exe (build/<Config>/shader/).
        // Also try the legacy layout one level up (build/shader/).
        const std::vector<std::string> exeCandidates = {
            exeDir + "shader" + sep + filename,
            exeDir + ".." + sep + "shader" + sep + filename,
        };
        for (const auto& c : exeCandidates) {
            std::string src = tryFile(c, "exe");
            if (!src.empty()) return src;
        }
    }

    // All paths failed — print full diagnostic
#if defined(_WIN32)
    char cwd[4096] = {0};
    GetCurrentDirectoryA(sizeof(cwd), cwd);
#else
    char cwd[4096] = {0};
    getcwd(cwd, sizeof(cwd));
#endif
    fprintf(stderr, "[Shader] FAILED to load '%s' (CWD: %s | EXE: %s)\n",
            filename.c_str(), cwd, haveExe ? exePath.c_str() : "?");
    for (const auto& a : attempts) {
        fprintf(stderr, "[Shader]     tried [%s]: %s\n", a.source, a.path.c_str());
    }
    return "";
}

static GLuint LoadShaderProgram(const char* vsFile, const char* fsFile) {
    std::string vsSrc = LoadShaderSource(vsFile);
    std::string fsSrc = LoadShaderSource(fsFile);
    if (vsSrc.empty() || fsSrc.empty()) return 0;
    return CoreEngine::CreateShaderProgram(vsSrc.c_str(), fsSrc.c_str());
}



// ── Static state ────────────────────────────────────────────────────

static GLFWwindow* s_window     = nullptr;
static int         s_width      = 0;
static int         s_height     = 0;
static GLuint      s_shaderProg = 0;
static GLuint      s_vao        = 0;
static GLuint      s_vbo        = 0;

static CoreEngine::Vector3 s_cameraPos    = {15, 12, 25};
static CoreEngine::Vector3 s_cameraTarget = {0, 0, 0};
static CoreEngine::Vector3 s_cameraOffset = {0, -1.5f, -5};

static std::vector<CoreEngine::SceneObject> s_sceneObjects;
static std::vector<std::shared_ptr<CoreEngine::PrimitiveMesh>> s_primitiveMeshes;
static uint32_t s_nextSceneObjectId = 1;
static uint32_t s_selectedObjectId = 0;

// ── Camera object ───────────────────────────────────────────────
static uint32_t s_cameraObjectId = 0;

static bool s_engineInited = false;

// Skybox state
static GLuint s_skyboxVBO = 0;
static GLuint s_skyboxVAO = 0;
static GLuint s_skyboxProg = 0;
static bool   s_skyboxInited = false;

// ── Shadow mapping state ──────────────────────────────────────────
static GLuint s_shadowFBO = 0;
static GLuint s_shadowDepthTex = 0;
static GLuint s_shadowDepthRB = 0;
static GLuint s_shadowDepthProg = 0;
static bool   s_shadowInited = false;
static int    s_shadowWidth  = 2048;
static int    s_shadowHeight = 2048;
static CoreEngine::Vector3 s_shadowLightDir = {0.5f, 1.0f, 0.3f};  // Sun-like direction
static const float SHADOW_NEAR = 0.5f;
static const float SHADOW_FAR  = 50.0f;
static const float SHADOW_PLANE_HALF = 15.0f;  // Half extent of shadow frustum
static const float SHADOW_NEAR_PLANE = 5.0f;  // Distance from light to near plane (so camera is behind light)

// ── Helpers ─────────────────────────────────────────────────────────

static void compileDefaultShader() {
    s_shaderProg = LoadShaderProgram("material.vert", "material.frag");
    if (s_shaderProg == 0) {
        fprintf(stderr, "[Shader] Failed to compile default shader program!\n");
        return;
    }
    glUseProgram(s_shaderProg);

    auto view = glm::lookAt(
        glm::vec3(s_cameraPos.x, s_cameraPos.y, s_cameraPos.z),
        glm::vec3(s_cameraTarget.x, s_cameraTarget.y, s_cameraTarget.z),
        glm::vec3(0, 1, 0));
    GLint viewLoc = glGetUniformLocation(s_shaderProg, "uView");
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
}

static bool s_primitivesBuilt = false;

static void buildPrimitiveVAOs() {
    if (s_primitivesBuilt) return;  // Build once — don't clear, or we destroy VAOs!
    s_primitivesBuilt = true;

    // Build cube mesh data manually (not via CreateBox which is in namespace)
    CoreEngine::PrimitiveMesh cube;
    cube.name = "cube";

    // 6 faces * 2 triangles per face * 3 vertices per triangle = 36 vertices
    // Each vertex: pos(3) + normal(3) + uv(2) = 8 floats
    static const float cubeVerts[] = {
        // Front face (+Z): 2 triangles = 6 vertices
        -0.5f, -0.5f,  0.5f,   0,0,1,  0,0,
         0.5f, -0.5f,  0.5f,   0,0,1,  1,0,
         0.5f,  0.5f,  0.5f,   0,0,1,  1,1,
        -0.5f, -0.5f,  0.5f,   0,0,1,  0,0,
         0.5f,  0.5f,  0.5f,   0,0,1,  1,1,
        -0.5f,  0.5f,  0.5f,   0,0,1,  0,1,
        // Back face (-Z): 2 triangles = 6 vertices
         0.5f, -0.5f, -0.5f,   0,0,-1,  0,0,
        -0.5f, -0.5f, -0.5f,   0,0,-1,  1,0,
        -0.5f,  0.5f, -0.5f,   0,0,-1,  1,1,
         0.5f, -0.5f, -0.5f,   0,0,-1,  0,0,
        -0.5f,  0.5f, -0.5f,   0,0,-1,  1,1,
         0.5f,  0.5f, -0.5f,   0,0,-1,  0,1,
        // Top face (+Y): 2 triangles = 6 vertices
        -0.5f,  0.5f,  0.5f,   0,1,0,  0,0,
         0.5f,  0.5f,  0.5f,   0,1,0,  1,0,
         0.5f,  0.5f, -0.5f,   0,1,0,  1,1,
        -0.5f,  0.5f,  0.5f,   0,1,0,  0,0,
         0.5f,  0.5f, -0.5f,   0,1,0,  1,1,
        -0.5f,  0.5f, -0.5f,   0,1,0,  0,1,
        // Bottom face (-Y): 2 triangles = 6 vertices
        -0.5f, -0.5f, -0.5f,   0,-1,0,  0,0,
         0.5f, -0.5f, -0.5f,   0,-1,0,  1,0,
         0.5f, -0.5f,  0.5f,   0,-1,0,  1,1,
        -0.5f, -0.5f, -0.5f,   0,-1,0,  0,0,
         0.5f, -0.5f,  0.5f,   0,-1,0,  1,1,
        -0.5f, -0.5f,  0.5f,   0,-1,0,  0,1,
        // Right face (+X): 2 triangles = 6 vertices
         0.5f, -0.5f,  0.5f,   1,0,0,  0,0,
         0.5f, -0.5f, -0.5f,   1,0,0,  1,0,
         0.5f,  0.5f, -0.5f,   1,0,0,  1,1,
         0.5f, -0.5f,  0.5f,   1,0,0,  0,0,
         0.5f,  0.5f, -0.5f,   1,0,0,  1,1,
         0.5f,  0.5f,  0.5f,   1,0,0,  0,1,
        // Left face (-X): 2 triangles = 6 vertices
        -0.5f, -0.5f, -0.5f,  -1,0,0,  0,0,
        -0.5f, -0.5f,  0.5f,  -1,0,0,  1,0,
        -0.5f,  0.5f,  0.5f,  -1,0,0,  1,1,
        -0.5f, -0.5f, -0.5f,  -1,0,0,  0,0,
        -0.5f,  0.5f,  0.5f,  -1,0,0,  1,1,
        -0.5f,  0.5f, -0.5f,  -1,0,0,  0,1,
    };

    glGenVertexArrays(1, &cube.VAO);
    glGenBuffers(1, &cube.VBO);
    glBindVertexArray(cube.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, cube.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // No EBO — use glDrawArrays
    cube.EBO = 0;
    cube.indexCount = sizeof(cubeVerts) / (sizeof(float) * 8);
    cube.halfExtent = {0.5f, 0.5f, 0.5f};  // vertices go from -0.5 to +0.5
    s_primitiveMeshes.push_back(CoreEngine::CreateMesh(std::move(cube)));

    // Build plane mesh - 2 triangles = 6 vertices
    CoreEngine::PrimitiveMesh plane;
    plane.name = "plane";

    static const float planeVerts[] = {
        -5.0f, 0, -5.0f,   0,1,0,  0,0,
         5.0f, 0, -5.0f,   0,1,0,  1,0,
         5.0f, 0,  5.0f,   0,1,0,  1,1,
        -5.0f, 0, -5.0f,   0,1,0,  0,0,
         5.0f, 0,  5.0f,   0,1,0,  1,1,
        -5.0f, 0,  5.0f,   0,1,0,  0,1,
    };

    glGenVertexArrays(1, &plane.VAO);
    glGenBuffers(1, &plane.VBO);
    glBindVertexArray(plane.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, plane.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(planeVerts), planeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // No EBO — use glDrawArrays
    plane.EBO = 0;
    plane.indexCount = sizeof(planeVerts) / (sizeof(float) * 8);
    plane.halfExtent = {5.0f, 0.01f, 5.0f};  // vertices go from -5 to +5
    s_primitiveMeshes.push_back(CoreEngine::CreateMesh(std::move(plane)));
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

    // Clean up shadow map (must be before context destruction)
    CleanupShadowMap();

    glDeleteVertexArrays(1, &s_vao);
    glDeleteBuffers(1, &s_vbo);
    if (s_shaderProg) glDeleteProgram(s_shaderProg);

    // Release GPU meshes while the GL context is still alive;
    // shared refs (scene objects, templates) free their VAOs/VBOs/EBOs here
    s_primitiveMeshes.clear();

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

    // Initialize offset from default camera position to target
    s_cameraOffset = s_cameraPos - s_cameraTarget;

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
        if (len <= 1) return shader;
        std::vector<char> logBuffer(len);
        glGetShaderInfoLog(shader, len, nullptr, logBuffer.data());
        fprintf(stderr, "Shader compile error:\n%s\n", logBuffer.data());
    }
    return shader;
}

GLuint CreateShaderProgram(const char* vsSource, const char* fsSource) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsSource, nullptr);
    glCompileShader(vs);
    GLint vsSuccess;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &vsSuccess);
    if (!vsSuccess) {
        GLint logLen;
        glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &logLen);
        if (logLen > 1) {
            char* log = new char[logLen];
            glGetShaderInfoLog(vs, logLen, nullptr, log);
            fprintf(stderr, "[SHADER ERR] Vertex shader compile error:\n%s\n", log);
            delete[] log;
        }
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSource, nullptr);
    glCompileShader(fs);
    GLint fsSuccess;
    glGetShaderiv(fs, GL_COMPILE_STATUS, &fsSuccess);
    if (!fsSuccess) {
        GLint logLen;
        glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &logLen);
        if (logLen > 1) {
            char* log = new char[logLen];
            glGetShaderInfoLog(fs, logLen, nullptr, log);
            fprintf(stderr, "[SHADER ERR] Fragment shader compile error:\n%s\n", log);
            delete[] log;
        }
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        GLint len;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        char* log = new char[len];
        glGetProgramInfoLog(prog, len, nullptr, log);
        fprintf(stderr, "[SHADER ERR] Program link error:\n%s\n", log);
        delete[] log;
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

MeshPtr CreateMesh(PrimitiveMesh mesh) {
    return MeshPtr(new PrimitiveMesh(std::move(mesh)), [](PrimitiveMesh* m) {
        if (m->VAO) glDeleteVertexArrays(1, &m->VAO);
        if (m->VBO) glDeleteBuffers(1, &m->VBO);
        if (m->EBO) glDeleteBuffers(1, &m->EBO);
        delete m;
    });
}

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
    glGenBuffers(1, &mesh.EBO);

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

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Box vertices go from -size/2 to +size/2
    mesh.halfExtent = {size.x / 2.0f, size.y / 2.0f, size.z / 2.0f};

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
    glGenBuffers(1, &mesh.EBO);

    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Plane vertices go from -width/2 to +width/2 and -height/2 to +height/2
    mesh.halfExtent = {width / 2.0f, 0.01f, height / 2.0f};

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

SceneObject& AddToScene(const std::string& name, MeshPtr mesh, Material mat) {
    SceneObject obj;
    obj.id = s_nextSceneObjectId++;
    obj.name = name;
    obj.mesh = std::move(mesh);
    obj.material = std::move(mat);
    s_sceneObjects.push_back(std::move(obj));
    return s_sceneObjects.back();
}

void ClearScene() {
    s_selectedObjectId = 0;
    s_cameraObjectId = 0;  // Reset so camera gets recreated on next CreateCameraObject()
    s_sceneObjects.clear();
}

void RemoveFromScene(uint32_t id) {
    for (auto it = s_sceneObjects.begin(); it != s_sceneObjects.end(); ++it) {
        if (it->id == id) {
            if (s_selectedObjectId == id) s_selectedObjectId = 0;
            s_sceneObjects.erase(it);
            return;
        }
    }
}

void SelectObject(uint32_t id) {
    s_selectedObjectId = id;
}

SceneObject* GetSelectedObject() {
    for (auto& obj : s_sceneObjects) {
        if (obj.id == s_selectedObjectId) {
            return &obj;
        }
    }
    return nullptr;
}

uint32_t GetSelectedObjectId() { return s_selectedObjectId; }

uint32_t GetNextSceneObjectId() { return s_nextSceneObjectId; }

// ── Camera ──────────────────────────────────────────────────────────

void SetCameraPosition(Vector3 pos) {
    s_cameraPos = pos;
    s_cameraOffset = s_cameraPos - s_cameraTarget;
}
void SetCameraTarget(Vector3 target) {
    s_cameraTarget = target;
    s_cameraOffset = s_cameraPos - s_cameraTarget;
}
void SetCameraDirection(Vector3 dir) { s_cameraOffset = dir; }
Vector3 GetCameraPosition() { return s_cameraPos; }
Vector3 GetCameraTarget() { return s_cameraTarget; }
Vector3 GetCameraDirection() { return s_cameraOffset; }
Vector3 GetCameraOffset() { return s_cameraOffset; }
void SetCameraOffset(Vector3 offset) {
    s_cameraOffset = offset;
    s_cameraPos = s_cameraTarget + offset;
}
void ResetCamera() {
    s_cameraPos = {15, 12, 25};
    s_cameraTarget = {0, 0, 0};
    s_cameraOffset = s_cameraPos - s_cameraTarget;
}

glm::mat4 GetProjectionMatrix(float fov, float aspect) {
    return GetProjectionMatrix(fov, aspect, 0.1f, 100.0f);
}

glm::mat4 GetProjectionMatrix(float fov, float aspect, float nearPlane, float farPlane) {
    if (farPlane <= nearPlane) farPlane = nearPlane + 1.0f;
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}

GLuint GetModelUniformLocation(GLuint prog, bool& found) {
    GLint loc = glGetUniformLocation(prog, "uModel");
    found = (loc != -1);
    return (GLuint)loc;
}

GLuint GetShaderProgram() { return s_shaderProg; }

MeshPtr GetPrimitiveMesh(const char* name) {
    for (const auto& m : s_primitiveMeshes) {
        if (m->name == name) return m;
    }
    return nullptr;
}

// ── 3D Grid ─────────────────────────────────────────────────────────

void DrawGrid(int divisions, float unit, float halfExtent, const glm::mat4& view, const glm::mat4& projection) {
    const float step = halfExtent * 2.0f / divisions;
    const float lineWidth = 0.02f;  // half-width of each grid line

    static std::vector<float> gridVerts;
    static std::vector<GLuint> gridIndices;
    static GLuint gridVBO = 0;
    static GLuint gridEBO = 0;
    static GLuint gridVAO = 0;
    static GLuint gridShaderProg = 0;
    static bool gridInited = false;
    static int cachedDivisions = 0;

    if (!gridInited || cachedDivisions != divisions) {
        gridVerts.clear();
        gridIndices.clear();

        // Lines along X axis at each Z division
        for (int i = 0; i <= divisions; ++i) {
            float pos = -halfExtent + i * step;
            float z1 = pos - lineWidth;
            float z2 = pos + lineWidth;

            gridVerts.push_back(-halfExtent); gridVerts.push_back(0.01f); gridVerts.push_back(z1);
            gridVerts.push_back( halfExtent); gridVerts.push_back(0.01f); gridVerts.push_back(z1);
            gridVerts.push_back( halfExtent); gridVerts.push_back(0.01f); gridVerts.push_back(z2);
            gridVerts.push_back(-halfExtent); gridVerts.push_back(0.01f); gridVerts.push_back(z2);

            GLuint base = (GLuint)gridVerts.size() / 3 - 4;
            gridIndices.push_back(base + 0);
            gridIndices.push_back(base + 1);
            gridIndices.push_back(base + 2);
            gridIndices.push_back(base + 0);
            gridIndices.push_back(base + 2);
            gridIndices.push_back(base + 3);
        }

        // Lines along Z axis at each X division
        for (int i = 0; i <= divisions; ++i) {
            float pos = -halfExtent + i * step;
            float x1 = pos - lineWidth;
            float x2 = pos + lineWidth;

            gridVerts.push_back(x1); gridVerts.push_back(0.01f); gridVerts.push_back(-halfExtent);
            gridVerts.push_back(x2); gridVerts.push_back(0.01f); gridVerts.push_back(-halfExtent);
            gridVerts.push_back(x2); gridVerts.push_back(0.01f); gridVerts.push_back( halfExtent);
            gridVerts.push_back(x1); gridVerts.push_back(0.01f); gridVerts.push_back( halfExtent);

            GLuint base = (GLuint)gridVerts.size() / 3 - 4;
            gridIndices.push_back(base + 0);
            gridIndices.push_back(base + 1);
            gridIndices.push_back(base + 2);
            gridIndices.push_back(base + 0);
            gridIndices.push_back(base + 2);
            gridIndices.push_back(base + 3);
        }

        if (gridVAO) glDeleteVertexArrays(1, &gridVAO);
        if (gridVBO) glDeleteBuffers(1, &gridVBO);
        if (gridEBO) glDeleteBuffers(1, &gridEBO);

        glGenVertexArrays(1, &gridVAO);
        glGenBuffers(1, &gridVBO);
        glGenBuffers(1, &gridEBO);
        glBindVertexArray(gridVAO);
        glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, gridVerts.size() * sizeof(float), gridVerts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, gridIndices.size() * sizeof(GLuint), gridIndices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, (void*)0);
        glBindVertexArray(0);
        cachedDivisions = divisions;

        if (gridShaderProg) glDeleteProgram(gridShaderProg);
        gridShaderProg = LoadShaderProgram("grid.vert", "grid.frag");
        gridInited = true;
    }

    if (gridVAO && gridShaderProg && !gridIndices.empty()) {
        GLuint savedShader = s_shaderProg;
        glUseProgram(gridShaderProg);

        GLint viewLoc = glGetUniformLocation(gridShaderProg, "uView");
        GLint projLoc = glGetUniformLocation(gridShaderProg, "uProjection");
        GLint colorLoc = glGetUniformLocation(gridShaderProg, "uColor");

        if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
        if (colorLoc != -1) glUniform3f(colorLoc, 0.85f, 0.85f, 0.85f);

        glBindVertexArray(gridVAO);
        glDisable(GL_DEPTH_TEST);
        glDrawElements(GL_TRIANGLES, (GLsizei)gridIndices.size(), GL_UNSIGNED_INT, 0);
        glEnable(GL_DEPTH_TEST);
        glBindVertexArray(0);

        if (savedShader) glUseProgram(savedShader);
    }
}

// ── Selected Object Bounds ──────────────────────────────────────────

void DrawSelectedObjectBounds(const glm::mat4& view, const glm::mat4& projection) {
    // Find the selected object
    SceneObject* sel = nullptr;
    uint32_t selectedId = s_selectedObjectId;
    for (auto& obj : s_sceneObjects) {
        if (obj.id == selectedId) { sel = &obj; break; }
    }

    if (!sel) return;
    if (!sel->mesh) return;
    if (sel->mesh->indexCount == 0) return;

    // Use the mesh's stored AABB (set at creation time)
    Vector3 he = sel->mesh->halfExtent;
    Vector3 ctr = sel->mesh->center;
    glm::vec3 extents(he.x, he.y, he.z);
    glm::vec3 ctrOffset(ctr.x, ctr.y, ctr.z);

    // Define a unit cube (local space) and scale it via the model matrix.
    // The 8 corners of a unit cube centered at origin (from -1 to +1).
    static const glm::vec3 localCorners[8] = {
        glm::vec3(-1, -1, -1),  // 0
        glm::vec3( 1, -1, -1),  // 1
        glm::vec3( 1,  1, -1),  // 2
        glm::vec3(-1,  1, -1),  // 3
        glm::vec3(-1, -1,  1),  // 4
        glm::vec3( 1, -1,  1),  // 5
        glm::vec3( 1,  1,  1),  // 6
        glm::vec3(-1,  1,  1),  // 7
    };

    // 12 edges (2 vertices per edge = 24 verts)
    const int edgePairs[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0}, // bottom
        {4,5}, {5,6}, {6,7}, {7,4}, // top
        {0,4}, {1,5}, {2,6}, {3,7}  // verticals
    };

    // Build edge vertex data: scale local corners by halfExtent and offset
    // by the AABB center to get mesh-space bounds
    float edgeVerts[24 * 3];
    for (int i = 0; i < 12; ++i) {
        glm::vec3 p0 = localCorners[edgePairs[i][0]] * extents + ctrOffset;
        glm::vec3 p1 = localCorners[edgePairs[i][1]] * extents + ctrOffset;
        edgeVerts[(i*6+0)] = p0.x;
        edgeVerts[(i*6+1)] = p0.y;
        edgeVerts[(i*6+2)] = p0.z;
        edgeVerts[(i*6+3)] = p1.x;
        edgeVerts[(i*6+4)] = p1.y;
        edgeVerts[(i*6+5)] = p1.z;
    }

    // Build model matrix from object transform (position + rotation + scale)
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(sel->position.x, sel->position.y, sel->position.z));
    model = glm::rotate(model, (float)sel->rotation.x, glm::vec3(1, 0, 0));
    model = glm::rotate(model, (float)sel->rotation.y, glm::vec3(0, 1, 0));
    model = glm::rotate(model, (float)sel->rotation.z, glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(sel->scale.x, sel->scale.y, sel->scale.z));

    static GLuint boundsVBO = 0;
    static GLuint boundsVAO = 0;
    if (boundsVAO == 0) {
        glGenVertexArrays(1, &boundsVAO);
        glGenBuffers(1, &boundsVBO);
        glBindVertexArray(boundsVAO);
        glBindBuffer(GL_ARRAY_BUFFER, boundsVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(edgeVerts), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, (void*)0);
        glBindVertexArray(0);
    }

    // Update vertex data each frame (bounds change with selection)
    glBindVertexArray(boundsVAO);
    glBindBuffer(GL_ARRAY_BUFFER, boundsVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(edgeVerts), edgeVerts);

    // Use a yellow color shader for the bounds
    static GLuint boundsShaderProg = 0;
    if (boundsShaderProg == 0) {
        boundsShaderProg = LoadShaderProgram("bounds.vert", "bounds.frag");
    }

    // Save the original program before switching
    GLuint savedShader = s_shaderProg;
    glUseProgram(boundsShaderProg);
    GLint viewLoc = glGetUniformLocation(boundsShaderProg, "uView");
    GLint projLoc = glGetUniformLocation(boundsShaderProg, "uProjection");
    GLint modelLoc = glGetUniformLocation(boundsShaderProg, "uModel");

    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
    // Pass the actual model matrix: the shader transforms local-space vertices.
    if (modelLoc != -1) glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

    glLineWidth(2.0f);
    glBindVertexArray(boundsVAO);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_LINES, 0, 24);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Restore original shader
    if (savedShader) glUseProgram(savedShader);
}

// ── Skybox ──────────────────────────────────────────────────────────

void CoreEngine::InitSkybox() {
    if (s_skyboxInited) return;

    // Skybox is a cube (6 faces, 24 vertices, 36 indices)
    const float cubeVerts[] = {
        // Front face
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        // Back face
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
        // Top face
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
        // Bottom face
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,
        // Right face
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        // Left face
        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,
    };

    const GLuint cubeIndices[] = {
        0,1,2, 0,2,3,       // front
        4,5,6, 4,6,7,       // back
        8,9,10, 8,10,11,    // top
        12,13,14, 12,14,15, // bottom
        16,17,18, 16,18,19, // right
        20,21,22, 20,22,23  // left
    };

    GLuint skyboxVBO, skyboxEBO;
    glGenVertexArrays(1, &s_skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glGenBuffers(1, &skyboxEBO);

    glBindVertexArray(s_skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, (void*)0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skyboxEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);
    glBindVertexArray(0);

    // Create skybox shader
    s_skyboxProg = LoadShaderProgram("skybox.vert", "skybox.frag");

    s_skyboxInited = true;
}

void CoreEngine::DrawSkybox(glm::vec3 cameraPosition, float aspect) {
    if (!s_skyboxInited || !s_window) return;

    // Use the passed camera position so the skybox tracks the actual view camera
    glm::mat4 view = glm::lookAt(
        cameraPosition,
        glm::vec3(0.0f),  // look at origin (center of skybox)
        glm::vec3(0, 1, 0));

    glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

    // Use skybox shader
    glUseProgram(s_skyboxProg);
    GLint viewLoc = glGetUniformLocation(s_skyboxProg, "uView");
    GLint projLoc = glGetUniformLocation(s_skyboxProg, "uProjection");
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

    // Draw skybox (disable depth write, use depth equal)
    glBindVertexArray(s_skyboxVAO);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_LEQUAL);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glUseProgram(s_shaderProg);
}

// ── Textures ────────────────────────────────────────────────────────

#include "stb_image_loader.h"

namespace {
    // Shared ownership: the GL texture is deleted when the last
    // TexturePtr referencing it is destroyed (main thread, context current).
    CoreEngine::TexturePtr MakeTexture() {
        return CoreEngine::TexturePtr(new CoreEngine::Texture(), [](CoreEngine::Texture* t) {
            if (t->id) glDeleteTextures(1, &t->id);
            delete t;
        });
    }

    // Upload raw pixel data to the GPU. Returns nullptr on failure.
    CoreEngine::TexturePtr UploadTexture(const unsigned char* data, int width, int height, int channels) {
        CoreEngine::TexturePtr tex = MakeTexture();
        tex->width = width;
        tex->height = height;
        tex->channels = channels;

        glGenTextures(1, &tex->id);
        if (tex->id == 0) return nullptr;
        glBindTexture(GL_TEXTURE_2D, tex->id);

        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Determine internal format and source format to match channel count
        GLenum internalFormat = GL_RGBA;
        GLenum format = GL_RGBA;
        if (channels == 3) { internalFormat = GL_RGB; format = GL_RGB; }
        else if (channels == 1) { internalFormat = GL_R; format = GL_RED; }

        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    }
}

CoreEngine::TexturePtr CoreEngine::LoadTexture(const std::string& path) {
    // stb_image loads with origin at top-left for most formats.
    // OpenGL expects bottom-left origin, so flip during load.
    // The flip flag is global stb state, so restore the default afterwards.
    stbi_set_flip_vertically_on_load(true);
    int w = 0, h = 0, c = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 4);
    stbi_set_flip_vertically_on_load(false);
    if (!data) {
        fprintf(stderr, "[Texture] Failed to load: %s (stb error: %s)\n",
            path.c_str(), stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        return nullptr;
    }
    CoreEngine::TexturePtr tex = UploadTexture(data, w, h, 4);
    stbi_image_free(data);
    return tex;
}

CoreEngine::TexturePtr CoreEngine::LoadTextureFromMemory(const unsigned char* data, int width, int height, int channels) {
    return UploadTexture(data, width, height, channels);
}

void CoreEngine::BindTexture(const CoreEngine::TexturePtr& tex, GLuint unit) {
    if (!tex || tex->id == 0) return;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex->id);
}

// ── Materials ───────────────────────────────────────────────────────

Material CoreEngine::CreateDefaultMaterial() {
    return Material{
        "default",
        glm::vec3(0.7f, 0.7f, 0.7f),  // base color - mid gray
        glm::vec3(0.0f, 0.0f, 0.0f),  // emissive
        0.0f,             // metallic
        1.0f,             // roughness
        1.0f,             // AO
        {}, {},           // diffuseTexture, normalTexture (nullptr = no texture)
        false
    };
}



// ── Shadow Mapping Implementation ─────────────────────────────────

void CoreEngine::SetShadowLightDirection(Vector3 dir) {
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len > 0.001f) {
        s_shadowLightDir = {dir.x / len, dir.y / len, dir.z / len};
    }
}

Vector3 CoreEngine::GetShadowLightDirection() {
    return s_shadowLightDir;
}

glm::mat4 CoreEngine::GetLightViewMatrix() {
    // Light looks down its direction
    auto dir = s_shadowLightDir;
    glm::vec3 lightPos(
        dir.x * SHADOW_PLANE_HALF + SHADOW_NEAR_PLANE,
        dir.y * SHADOW_PLANE_HALF + SHADOW_NEAR_PLANE,
        dir.z * SHADOW_PLANE_HALF + SHADOW_NEAR_PLANE
    );
    glm::vec3 target(0.0f, 0.0f, 0.0f);
    return glm::lookAt(lightPos, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 CoreEngine::GetLightProjectionMatrix() {
    // Use an orthographic projection for directional light shadow map
    float half = SHADOW_PLANE_HALF;
    return glm::ortho(-half, half, -half, half, SHADOW_NEAR, SHADOW_FAR);
}

glm::mat4 CoreEngine::GetLightSpaceMatrix() {
    return GetLightProjectionMatrix() * GetLightViewMatrix();
}

void CoreEngine::InitShadowMap(int width, int height) {
    if (s_shadowInited) {
        // Recreate if dimensions changed
        if (s_shadowWidth != width || s_shadowHeight != height) {
            CleanupShadowMap();
        } else {
            return;
        }
    }

    s_shadowWidth = width;
    s_shadowHeight = height;

    // Create depth texture
    glGenTextures(1, &s_shadowDepthTex);
    glBindTexture(GL_TEXTURE_2D, s_shadowDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    
    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create FBO with depth attachment
    glGenFramebuffers(1, &s_shadowFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s_shadowDepthTex, 0);
    
    // Use GL_NONE - we only need depth, no color attachments
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[ShadowMap] FBO incomplete!\n");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Compile shadow depth shader
    s_shadowDepthProg = LoadShaderProgram("shadow_depth.vert", "shadow_depth.frag");

    s_shadowInited = true;
    printf("[ShadowMap] Initialized %dx%d shadow map\n", width, height);
}

void CoreEngine::DrawShadowPass() {
    if (!s_shadowInited) return;

    // Save the caller's viewport so we don't leak the shadow-map
    // viewport into the main pass.
    GLint savedViewport[4];
    glGetIntegerv(GL_VIEWPORT, savedViewport);

    // Bind shadow FBO for depth rendering
    glBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
    glViewport(0, 0, s_shadowWidth, s_shadowHeight);
    glClear(GL_DEPTH_BUFFER_BIT);
    
    // Use shadow shader
    glUseProgram(s_shadowDepthProg);
    
    // Compute light space matrix
    glm::mat4 lightSpaceMat = GetLightSpaceMatrix();
    GLint lsmLoc = glGetUniformLocation(s_shadowDepthProg, "uLightSpaceMatrix");
    if (lsmLoc != -1) {
        glUniformMatrix4fv(lsmLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMat));
    }

    // Render all scene objects (skip the camera visual — it must not
    // cast a shadow blob at the camera position)
    auto& scene = GetSceneObjects();
    for (auto& obj : scene) {
        if (obj.id == s_cameraObjectId) continue;
        auto& mesh = obj.mesh;
        if (!mesh || !mesh->VAO || mesh->indexCount == 0) continue;

        // Build model matrix
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(obj.position.x, obj.position.y, obj.position.z));
        model = glm::rotate(model, (float)obj.rotation.x, glm::vec3(1, 0, 0));
        model = glm::rotate(model, (float)obj.rotation.y, glm::vec3(0, 1, 0));
        model = glm::rotate(model, (float)obj.rotation.z, glm::vec3(0, 0, 1));
        model = glm::scale(model, glm::vec3(obj.scale.x, obj.scale.y, obj.scale.z));

        GLint modelLoc = glGetUniformLocation(s_shadowDepthProg, "uModel");
        if (modelLoc != -1) {
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        }

        glBindVertexArray(mesh->VAO);
        if (mesh->EBO) {
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->indexCount, GL_UNSIGNED_INT, 0);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)mesh->indexCount);
        }
        glBindVertexArray(0);
    }

    // Restore default framebuffer and the caller's viewport
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
}

void CoreEngine::CleanupShadowMap() {
    if (s_shadowDepthTex) {
        glDeleteTextures(1, &s_shadowDepthTex);
        s_shadowDepthTex = 0;
    }
    if (s_shadowDepthRB) {
        glDeleteRenderbuffers(1, &s_shadowDepthRB);
        s_shadowDepthRB = 0;
    }
    if (s_shadowFBO) {
        glDeleteFramebuffers(1, &s_shadowFBO);
        s_shadowFBO = 0;
    }
    if (s_shadowDepthProg) {
        glDeleteProgram(s_shadowDepthProg);
        s_shadowDepthProg = 0;
    }
    s_shadowInited = false;
}

GLuint CoreEngine::GetShadowMapTexture() {
    return s_shadowDepthTex;
}

GLuint CoreEngine::GetShadowMapFBO() {
    return s_shadowFBO;
}

int CoreEngine::GetShadowMapWidth() {
    return s_shadowWidth;
}

int CoreEngine::GetShadowMapHeight() {
    return s_shadowHeight;
}

// ── Camera as scene object ────────────────────────────────────────

static CoreEngine::SceneObject* GetCameraObject() {
    for (auto& obj : s_sceneObjects) {
        if (obj.id == s_cameraObjectId) return &obj;
    }
    return nullptr;
}

void CoreEngine::SetCameraId(uint32_t id) { s_cameraObjectId = id; }
uint32_t CoreEngine::GetCameraObjectId() { return s_cameraObjectId; }
bool CoreEngine::IsCameraObjectId(uint32_t id) { return id == s_cameraObjectId; }

// Called from editor to create the camera object
void CoreEngine::CreateCameraObject() {
    if (s_cameraObjectId != 0) return; // already created

    auto cubeMesh = CreateBox({1, 1, 1});
    cubeMesh.name = "cube";
    s_cameraObjectId = s_nextSceneObjectId++;

    SceneObject cam;
    cam.id = s_cameraObjectId;
    cam.name = "Camera";
    cam.mesh = CreateMesh(std::move(cubeMesh));
    cam.position = s_cameraPos;
    cam.scale = {0.3f, 0.3f, 0.3f};
    cam.material.name = "camera_material";
    cam.material.baseColor = glm::vec3(0.2f, 0.6f, 1.0f); // blue
    cam.material.useMaterial = true;
    s_sceneObjects.push_back(std::move(cam));
}

// Called when camera object is moved via inspector
void CoreEngine::SyncSceneToCameraObject() {
    auto* cam = GetCameraObject();
    if (!cam) return;
    s_cameraPos = cam->position;
    s_cameraOffset = s_cameraPos - s_cameraTarget;
}

} // namespace CoreEngine
