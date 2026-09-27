// Editor debug overlays: the 3D ground grid and the selected object's
// bounding-box wireframe.

#include "core/engine.h"

#include <glm/gtc/type_ptr.hpp>

#include "engine_internal.h"

namespace CoreEngine {

// Shared fog parameters for the ground plane + grid so both dissolve into
// the same sky horizon color at the same distance (no seam between the
// ground's fade and the lines' fade).
static const float kFogDensity   = 0.0f;     // 0 = no fog (scene shown without distance fade)
static const float kFogR = 106.0f / 255.0f;
static const float kFogG =  99.0f / 255.0f;
static const float kFogB =  95.0f / 255.0f;
static const float kGroundR = 100.0f / 255.0f;
static const float kGroundG =  93.0f / 255.0f;
static const float kGroundB =  88.0f / 255.0f;

// ── Ground Plane ─────────────────────────────────────────────────────

void DrawGroundPlane(const glm::mat4& view, const glm::mat4& projection) {
    // A single large quad on y=0. It follows the camera (snapped to the
    // grid cell below the camera) so it is effectively infinite, and the
    // fragment shader fades it into the sky's horizon color with distance.
    static GLuint groundVAO = 0;
    static GLuint groundVBO = 0;
    static GLuint groundEBO = 0;
    static GLuint groundProg = 0;

    if (groundVAO == 0) {
        // Four corners of a big square centered on the origin (in local
        // space; the world offset is applied in the shader). The plane is
        // large enough that its edges are always beyond the fog fade, so
        // the hard edge is invisible.
        const float kHalf = 512.0f;  // match the grid extent (±512 cells)
        const float verts[] = {
            -kHalf, 0.0f, -kHalf,
             kHalf, 0.0f, -kHalf,
             kHalf, 0.0f,  kHalf,
            -kHalf, 0.0f,  kHalf,
        };
        const unsigned int indices[] = { 0, 1, 2, 0, 2, 3 };

        glGenVertexArrays(1, &groundVAO);
        glGenBuffers(1, &groundVBO);
        glGenBuffers(1, &groundEBO);
        glBindVertexArray(groundVAO);
        glBindBuffer(GL_ARRAY_BUFFER, groundVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, (void*)0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, groundEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
        glBindVertexArray(0);

        groundProg = LoadShaderProgram("ground.vert", "ground.frag");
    }
    if (!groundProg) return;

    // Camera world position from the view matrix.
    const float tvecX = view[3][0], tvecY = view[3][1], tvecZ = view[3][2];
    const glm::vec3 camPos(
        -(view[0][0] * tvecX + view[0][1] * tvecY + view[0][2] * tvecZ),
        -(view[1][0] * tvecX + view[1][1] * tvecY + view[1][2] * tvecZ),
        -(view[2][0] * tvecX + view[2][1] * tvecY + view[2][2] * tvecZ));

    // Fixed at the world origin (where the scene lives), not camera-
    // following, so the ground always covers the scene. Large enough that
    // its edges are beyond the fog fade and off-screen.
    const float offX = 0.0f;
    const float offZ = 0.0f;

    GLuint savedShader = s_shaderProg;
    glUseProgram(groundProg);
    GLint viewLoc = glGetUniformLocation(groundProg, "uView");
    GLint projLoc = glGetUniformLocation(groundProg, "uProjection");
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
    GLint loc = -1;
    if ((loc = glGetUniformLocation(groundProg, "uOffset")) != -1)
        glUniform3f(loc, offX, 0.0f, offZ);
    if ((loc = glGetUniformLocation(groundProg, "uCameraPos")) != -1)
        glUniform3f(loc, camPos.x, camPos.y, camPos.z);
    if ((loc = glGetUniformLocation(groundProg, "uFogDensity")) != -1)
        glUniform1f(loc, kFogDensity);
    if ((loc = glGetUniformLocation(groundProg, "uGroundColor")) != -1)
        glUniform3f(loc, kGroundR, kGroundG, kGroundB);
    if ((loc = glGetUniformLocation(groundProg, "uFogColor")) != -1)
        glUniform3f(loc, kFogR, kFogG, kFogB);

    // Opaque: write color + depth so scene objects correctly occlude it and
    // it occludes the skybox behind.
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(groundVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    if (savedShader) glUseProgram(savedShader);
}

// ── 3D Grid ─────────────────────────────────────────────────────────

void DrawGrid(float spacing, const glm::mat4& view, const glm::mat4& projection) {
    if (spacing <= 0.0f) spacing = 1.0f;

    const int kCells = 512;         // grid extends ±512 cells around the origin
    const int kMajorEvery = 10;     // (kept for the aMajor attribute; lines are uniform now)
    const float kHeight = 0.01f;    // slightly above the y=0 ground plane

    // ── Geometry (built once): one thin filled QUAD per grid line ──
    // Each line is a 4-corner quad (two triangles) of constant pixel width,
    // which the vertex shader offsets perpendicular to the line. Filled
    // triangles render as a single solid, anti-aliased line with no double-
    // edge artifact (unlike the old two-coincident-line-edge approach).
    // Per vertex:
    //   aPos   (vec2)  cell coordinates (x, z) — the line's CENTER point
    //   aDir   (vec2)  line direction in grid space
    //   aSide  (float) +1 / -1  (which perpendicular edge of the quad)
    //   aMajor (float) 1.0 = major line
    struct GridVertex {
        float cellX, cellZ;  // aPos
        float dirX, dirZ;    // aDir
        float side;          // aSide
        float major;         // aMajor
    };

    static std::vector<GridVertex> gridVerts;
    static GLuint gridVBO = 0;
    static GLuint gridVAO = 0;
    static GLuint gridShaderProg = 0;
    static int gridVertCount = 0;

    if (gridVertCount == 0) {
        auto addLine = [&](int x0, int z0, int x1, int z1, float dirX, float dirZ, float major) {
            // One quad = 6 vertices (two triangles). The vertex shader offsets
            // each corner perpendicular to the line by aSide * halfWidth, so
            // the quad is centered on the line and has a constant pixel width.
            auto corner = [&](int cx, int cz, float side) -> GridVertex {
                GridVertex v;
                v.cellX = (float)cx; v.cellZ = (float)cz;
                v.dirX = dirX; v.dirZ = dirZ;
                v.side = side;
                v.major = major;
                return v;
            };
            GridVertex a = corner(x0, z0,  1.0f);
            GridVertex b = corner(x0, z0, -1.0f);
            GridVertex c = corner(x1, z1, -1.0f);
            GridVertex d = corner(x1, z1,  1.0f);
            // Two triangles: (a,b,c) and (a,c,d)
            gridVerts.push_back(a);
            gridVerts.push_back(b);
            gridVerts.push_back(c);
            gridVerts.push_back(a);
            gridVerts.push_back(c);
            gridVerts.push_back(d);
        };

        // Lines running along Z, one per X cell
        for (int k = -kCells; k <= kCells; ++k) {
            float major = (k % kMajorEvery == 0) ? 1.0f : 0.0f;
            addLine(k, -kCells, k, kCells, 0.0f, 1.0f, major);
        }
        // Lines running along X, one per Z cell
        for (int k = -kCells; k <= kCells; ++k) {
            float major = (k % kMajorEvery == 0) ? 1.0f : 0.0f;
            addLine(-kCells, k, kCells, k, 1.0f, 0.0f, major);
        }

        glGenVertexArrays(1, &gridVAO);
        glGenBuffers(1, &gridVBO);
        glBindVertexArray(gridVAO);
        glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(gridVerts.size() * sizeof(GridVertex)),
                     gridVerts.data(), GL_STATIC_DRAW);

        const GLsizei stride = (GLsizei)sizeof(GridVertex);
        glEnableVertexAttribArray(0);  // aPos
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (const void*)0);
        glEnableVertexAttribArray(1);  // aDir
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(sizeof(float) * 2));
        glEnableVertexAttribArray(2);  // aSide
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (const void*)(sizeof(float) * 4));
        glEnableVertexAttribArray(3);  // aMajor
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (const void*)(sizeof(float) * 5));
        glBindVertexArray(0);

        gridVertCount = (int)gridVerts.size();
        gridShaderProg = LoadShaderProgram("grid.vert", "grid.frag");
    }

    static bool gridEarlyDiag = false;
    if (!gridEarlyDiag) {
        gridEarlyDiag = true;
        printf("[Grid] EARLY vertCount=%d prog=%u\n", gridVertCount, gridShaderProg);
        fflush(stdout);
    }
    if (gridVertCount == 0 || !gridShaderProg) return;

    // ── Per-frame state ────────────────────────────────────────────

    // Camera world position from the view matrix: cam = -A^T * tvec, where
    // A is the rotation part and tvec the translation column.
    // Component i: -(column_i(A) . tvec).
    const float tvecX = view[3][0], tvecY = view[3][1], tvecZ = view[3][2];
    const glm::vec3 camPos(
        -(view[0][0] * tvecX + view[0][1] * tvecY + view[0][2] * tvecZ),
        -(view[1][0] * tvecX + view[1][1] * tvecY + view[1][2] * tvecZ),
        -(view[2][0] * tvecX + view[2][1] * tvecY + view[2][2] * tvecZ));

    // The grid is a large fixed reference centered on the world origin
    // (where the scene lives), not camera-following. This keeps the lines
    // locked to world coordinates and ensures the grid covers the scene
    // regardless of where the camera orbits.
    const float offX = 0.0f;
    const float offZ = 0.0f;

    // World units per screen pixel at unit depth (for the vertex shader's
    // constant-pixel-width offset). projection[1][1] = 1/tan(fovY/2).
    int vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    float worldPerPixel = 1.0f;
    if (vp[3] > 0 && projection[1][1] > 0.0f)
        worldPerPixel = (2.0f / projection[1][1]) / (float)vp[3];

    // ── Zoom-invariant spacing ────────────────────────────────────────
    // Pick the grid cell spacing so that adjacent lines are always a fixed
    // number of pixels apart on screen, no matter how far the camera is.
    // As the camera zooms out, the spacing steps up by powers of 10 (lines
    // merge into coarser lines); as it zooms in, the spacing steps down so
    // finer lines appear. The result: the grid looks the same at any zoom.
    //
    // The on-screen distance between two lines = (spacing / worldPerPixel)
    // pixels *at unit depth*. But the grid is at some distance from the
    // camera, so the apparent spacing is scaled by the perspective: a
    // feature at distance D appears (unitDepth / D) times smaller. We use
    // the camera's distance to the grid center (origin) as D.
    const float kTargetPx = 48.0f;  // desired on-screen pixels between lines
    const float distToCenter = glm::length(glm::vec2(camPos.x, camPos.z));
    // Apparent world-units-per-pixel at the grid's distance:
    float worldPerPxAtGrid = worldPerPixel * distToCenter;
    // Desired spacing in world units so that lines are kTargetPx apart:
    float desiredSpacing = kTargetPx * worldPerPxAtGrid;
    // Snap to a power-of-10 multiple of the base spacing so the lines stay
    // aligned to a consistent metric (1, 10, 100, 1000, ... units).
    float adaptiveSpacing = spacing;
    {
        float s = spacing;
        // Round desiredSpacing up to the nearest power-of-10 multiple of the
        // base spacing, so lines land on "nice" world coordinates.
        while (s < desiredSpacing * 0.5f) s *= 10.0f;
        while (s > desiredSpacing * 2.0f) s /= 10.0f;
        adaptiveSpacing = s;
    }

    GLuint savedShader = s_shaderProg;
    glUseProgram(gridShaderProg);

    GLint viewLoc = glGetUniformLocation(gridShaderProg, "uView");
    GLint projLoc = glGetUniformLocation(gridShaderProg, "uProjection");
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
    GLint loc = -1;
    if ((loc = glGetUniformLocation(gridShaderProg, "uOffset")) != -1)
        glUniform3f(loc, offX, kHeight, offZ);
    if ((loc = glGetUniformLocation(gridShaderProg, "uSpacing")) != -1)
        glUniform1f(loc, adaptiveSpacing);  // zoom-invariant cell size
    if ((loc = glGetUniformLocation(gridShaderProg, "uCameraPos")) != -1)
        glUniform3f(loc, camPos.x, camPos.y, camPos.z);
    // Unity-style grid: the ground fades to a flat warm-gray sky color at
    // the horizon, and the lines dissolve into it. The fog color + density
    // are shared with the ground plane (see kFog*/kGround* above) so the
    // lines and the ground vanish together. Lines are a subtle lightening
    // of the ground with no separate major/minor tone.
    // Unity-style: lines are a subtle lightening of the ground, no separate
    // major/minor tone. Slightly lighter than the fogged ground so they read
    // as faint etched lines that dissolve into the sky at the horizon.
    if ((loc = glGetUniformLocation(gridShaderProg, "uMinorColor")) != -1)
        glUniform3f(loc, 120.0f/255.0f, 114.0f/255.0f, 108.0f/255.0f);
    if ((loc = glGetUniformLocation(gridShaderProg, "uMajorColor")) != -1)
        glUniform3f(loc, 120.0f/255.0f, 114.0f/255.0f, 108.0f/255.0f);
    if ((loc = glGetUniformLocation(gridShaderProg, "uFogColor")) != -1)
        glUniform3f(loc, kFogR, kFogG, kFogB);
    if ((loc = glGetUniformLocation(gridShaderProg, "uFadeExp")) != -1)
        glUniform1f(loc, kFogDensity);              // shared with the ground plane
    if ((loc = glGetUniformLocation(gridShaderProg, "uWorldPerPixel")) != -1)
        glUniform1f(loc, worldPerPixel);
    if ((loc = glGetUniformLocation(gridShaderProg, "uLineHalfPx")) != -1)
        glUniform1f(loc, 0.1f);                   // 0.2px total line width (80% thinner)

    // Blended into the ground; depth test stays off (the ground plane is
    // drawn first and writes depth, but the grid is a reference overlay so it
    // is drawn on top; scene objects are drawn afterwards and simply
    // overwrite grid pixels that are behind them).
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(gridVAO);
    glDrawArrays(GL_TRIANGLES, 0, gridVertCount);

    // ── Origin axis lines (red = +X, blue = +Z) ─────────────────────
    // Two colored quads along the world axes at the origin, drawn on top of
    // the neutral grid with the same pixel-width shader. They mark the
    // origin/orientation the way Unity's grid does. Each axis is a quad (6
    // verts), same layout as the main grid.
    static GLuint axisVAO = 0;
    static GLuint axisVBO = 0;
    if (axisVAO == 0) {
        auto addAxisQuad = [](std::vector<GridVertex>& v, float x0, float z0, float x1, float z1,
                              float dirX, float dirZ) {
            auto corner = [&](float cx, float cz, float side) -> GridVertex {
                GridVertex gv;
                gv.cellX = cx; gv.cellZ = cz;
                gv.dirX = dirX; gv.dirZ = dirZ;
                gv.side = side;
                gv.major = 0.0f;
                return gv;
            };
            GridVertex a = corner(x0, z0,  1.0f);
            GridVertex b = corner(x0, z0, -1.0f);
            GridVertex c = corner(x1, z1, -1.0f);
            GridVertex d = corner(x1, z1,  1.0f);
            v.push_back(a);
            v.push_back(b);
            v.push_back(c);
            v.push_back(a);
            v.push_back(c);
            v.push_back(d);
        };
        std::vector<GridVertex> ax;
        addAxisQuad(ax, -kCells, 0, kCells, 0, 1.0f, 0.0f);  // +X axis (red)
        addAxisQuad(ax, 0, -kCells, 0, kCells, 0.0f, 1.0f);  // +Z axis (blue)

        glGenVertexArrays(1, &axisVAO);
        glGenBuffers(1, &axisVBO);
        glBindVertexArray(axisVAO);
        glBindBuffer(GL_ARRAY_BUFFER, axisVBO);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(ax.size() * sizeof(GridVertex)),
                     ax.data(), GL_STATIC_DRAW);
        const GLsizei stride = (GLsizei)sizeof(GridVertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (const void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(sizeof(float) * 2));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (const void*)(sizeof(float) * 4));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (const void*)(sizeof(float) * 5));
        glBindVertexArray(0);
    }

    // Draw the axis quads every frame (VAO built once above).
    // Red: +X axis (first 6 verts). Blue: +Z axis (next 6 verts).
    if ((loc = glGetUniformLocation(gridShaderProg, "uMinorColor")) != -1)
        glUniform3f(loc, 0.85f, 0.15f, 0.15f);
    if ((loc = glGetUniformLocation(gridShaderProg, "uMajorColor")) != -1)
        glUniform3f(loc, 0.85f, 0.15f, 0.15f);
    glBindVertexArray(axisVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    if ((loc = glGetUniformLocation(gridShaderProg, "uMinorColor")) != -1)
        glUniform3f(loc, 0.20f, 0.35f, 0.90f);
    if ((loc = glGetUniformLocation(gridShaderProg, "uMajorColor")) != -1)
        glUniform3f(loc, 0.20f, 0.35f, 0.90f);
    glBindVertexArray(axisVAO);
    glDrawArrays(GL_TRIANGLES, 6, 6);
    glBindVertexArray(0);

    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    // One-time diagnostic: confirm the grid actually issued draw calls.
    static bool gridDiagLogged = false;
    if (!gridDiagLogged) {
        gridDiagLogged = true;
        printf("[Grid] vertCount=%d prog=%u glErr=0x%04X\n", gridVertCount, gridShaderProg, glGetError());
    }

    if (savedShader) glUseProgram(savedShader);
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

    // World matrix = parent chain * local TRS, so the bounds box follows
    // the object's ancestors (e.g. a model root's rotation).
    glm::mat4 model = ComputeObjectWorldMatrix(sel->id);

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
    // Depth-test against the scene so the wireframe is occluded by
    // geometry in front of it (behind-side edges hide, front-side show).
    // LEQUAL keeps edges that lie exactly on the object's own surface
    // from flickering out against co-located triangle fragments.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDrawArrays(GL_LINES, 0, 24);
    glDepthFunc(GL_LESS);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Restore original shader
    if (savedShader) glUseProgram(savedShader);
}

} // namespace CoreEngine
