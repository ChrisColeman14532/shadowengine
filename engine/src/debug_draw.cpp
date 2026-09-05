// Editor debug overlays: the 3D ground grid and the selected object's
// bounding-box wireframe.

#include "core/engine.h"

#include <glm/gtc/type_ptr.hpp>

#include "engine_internal.h"

namespace CoreEngine {

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

} // namespace CoreEngine
