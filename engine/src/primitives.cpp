// Mesh primitives: reference-counted GPU mesh handles, box/plane
// creation, and the built-in primitive VAOs (cube, plane).

#include "core/engine.h"

#include <cstddef>
#include <string>
#include <utility>

#include "engine_internal.h"

namespace CoreEngine {

// ── Mesh handle ────────────────────────────────────────────────────

MeshPtr CreateMesh(PrimitiveMesh mesh) {
    return MeshPtr(new PrimitiveMesh(std::move(mesh)), [](PrimitiveMesh* m) {
        if (m->VAO) glDeleteVertexArrays(1, &m->VAO);
        if (m->VBO) glDeleteBuffers(1, &m->VBO);
        if (m->EBO) glDeleteBuffers(1, &m->EBO);
        delete m;
    });
}

void DestroyMesh(PrimitiveMesh& mesh) {
    if (mesh.VAO) { glDeleteVertexArrays(1, &mesh.VAO); mesh.VAO = 0; }
    if (mesh.VBO) { glDeleteBuffers(1, &mesh.VBO); mesh.VBO = 0; }
    if (mesh.EBO) { glDeleteBuffers(1, &mesh.EBO); mesh.EBO = 0; }
    mesh.indexCount = 0;
}

// ── Primitive factories ────────────────────────────────────────────

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

// ── Built-in primitive VAOs (cube, plane) ──────────────────────────

void buildPrimitiveVAOs() {
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

void InitPrimitiveMeshes() {
    buildPrimitiveVAOs();
}

MeshPtr GetPrimitiveMesh(const char* name) {
    for (const auto& m : s_primitiveMeshes) {
        if (m->name == name) return m;
    }
    return nullptr;
}

} // namespace CoreEngine
