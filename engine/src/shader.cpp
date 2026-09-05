// Shader support: file-based shader loading (multi-location search),
// GLSL compilation/linking, and the default material shader.

#include "core/engine.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <glm/gtc/type_ptr.hpp>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "engine_internal.h"

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

namespace CoreEngine {

GLuint LoadShaderProgram(const char* vsFile, const char* fsFile) {
    std::string vsSrc = LoadShaderSource(vsFile);
    std::string fsSrc = LoadShaderSource(fsFile);
    if (vsSrc.empty() || fsSrc.empty()) return 0;
    return CreateShaderProgram(vsSrc.c_str(), fsSrc.c_str());
}

void compileDefaultShader() {
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

// ── Shader compilation / uniforms ──────────────────────────────────

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

GLuint GetShaderProgram() { return s_shaderProg; }

GLuint GetModelUniformLocation(GLuint prog, bool& found) {
    GLint loc = glGetUniformLocation(prog, "uModel");
    found = (loc != -1);
    return (GLuint)loc;
}

} // namespace CoreEngine
