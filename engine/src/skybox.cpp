// Skybox: large inverted cube with a procedural gradient + sun shader.

#include "core/engine.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine_internal.h"

namespace CoreEngine {

void InitSkybox() {
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

void DrawSkybox(glm::vec3 cameraPosition, float aspect) {
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

} // namespace CoreEngine
