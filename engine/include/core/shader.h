#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

namespace CoreEngine {

    // Shaders
    GLuint CompileShader(GLenum type, const char* source);
    GLuint CreateShaderProgram(const char* vsSource, const char* fsSource);
    void UseShader(GLuint program);
    void SetUniformMat4(GLuint program, const char* name, const glm::mat4& m);
    void SetUniformVec3(GLuint program, const char* name, const glm::vec3& v);

    // Internal helpers (used by editor)
    GLuint GetShaderProgram();
    GLuint GetModelUniformLocation(GLuint prog, bool& found);

} // namespace CoreEngine
