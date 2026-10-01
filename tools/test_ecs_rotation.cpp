// Regression tests for two ECS fixes:
//
//  1. Rotation units: Ecs::Transform.rotation holds RADIANS (copied
//     verbatim from SceneObject::rotation by SyncSceneToEcs), and the
//     transform system must build the same matrix as
//     Scene::ObjectLocalMatrix (T*Rx*Ry*Rz*S, radians). Before the fix,
//     TransformLocalMatrix applied glm::radians() a second time, so a
//     90-degree part rendered rotated by only ~1.57 degrees.
//
//  2. Free-list consistency: World::CreateEntity(id) must remove the
//     id from the free list; otherwise a later no-arg CreateEntity()
//     hands out an id that is already alive (double allocation).
//
// No GL context needed: only the scene projection and the pure ECS core.
// Build with build_test_ecs_rotation.bat, run from anywhere.

#include "core/ecs.h"
#include "core/scene.h"

#include <cmath>
#include <cstdio>

using namespace CoreEngine;

static int g_failures = 0, g_checks = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++g_failures; \
    } \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
    ++g_checks; \
    if (!(std::abs((a) - (b)) <= (eps))) { \
        printf("  FAIL %s:%d: |%.6g - %.6g| > %g\n", \
               __FILE__, __LINE__, (double)(a), (double)(b), (double)(eps)); \
        ++g_failures; \
    } \
} while (0)

// Max abs element difference between two mat4s.
static float Mat4Diff(const glm::mat4& a, const glm::mat4& b) {
    float d = 0.0f;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            d = fmaxf(d, fabsf(a[i][j] - b[i][j]));
    return d;
}

// Lock-step of Scene::ObjectLocalMatrix (engine/src/scene.cpp):
// T * Rx * Ry * Rz * S, all angles in RADIANS.
static glm::mat4 LegacyLocal(const glm::vec3& p, const glm::vec3& r,
                             const glm::vec3& s) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), p);
    m = glm::rotate(m, r.x, glm::vec3(1, 0, 0));
    m = glm::rotate(m, r.y, glm::vec3(0, 1, 0));
    m = glm::rotate(m, r.z, glm::vec3(0, 0, 1));
    return glm::scale(m, s);
}

static void TestRotationUnits() {
    printf("Test: ECS rotation units (radians end-to-end)\n");

    ClearScene();
    const float PI = 3.14159265358979323846f;

    // Mixed angles including an exact 90-degree Y, off-axis position,
    // non-uniform scale.
    const glm::vec3 pos(1.0f, 2.0f, 3.0f);
    const glm::vec3 rot(0.3f, PI / 2.0f, -0.7f);
    const glm::vec3 scl(2.0f, 1.0f, 0.5f);

    const uint32_t id = AddToScene("rot_test", nullptr, Material());
    SceneObject* o = GetSceneObject(id);
    CHECK(o != nullptr);
    if (!o) return;
    o->position = pos;
    o->rotation = rot;
    o->scale = scl;

    TickEcs(0.016f);

    const Ecs::WorldTransform* wt = GetEcsWorld().Get<Ecs::WorldTransform>(id);
    CHECK(wt != nullptr);
    if (!wt) return;

    CHECK(Mat4Diff(wt->localToWorld, LegacyLocal(pos, rot, scl)) < 1e-5f);

    // Exact 90-degree Y (unit scale/position): the +X basis must land on
    // -Z. Before the fix the rotation was ~1.57 degrees, so the +X basis
    // stayed near (1,0,0) and this failed.
    ClearScene();
    const uint32_t id2 = AddToScene("rot_90y", nullptr, Material());
    SceneObject* o2 = GetSceneObject(id2);
    CHECK(o2 != nullptr);
    if (!o2) return;
    o2->rotation = glm::vec3(0.0f, PI / 2.0f, 0.0f);

    TickEcs(0.016f);
    const Ecs::WorldTransform* wt2 = GetEcsWorld().Get<Ecs::WorldTransform>(id2);
    CHECK(wt2 != nullptr);
    if (!wt2) return;
    const glm::vec4 col0 = wt2->localToWorld[0];
    CHECK_NEAR(col0.x, 0.0f, 1e-5f);
    CHECK_NEAR(col0.y, 0.0f, 1e-5f);
    CHECK_NEAR(col0.z, -1.0f, 1e-5f);
}

static void TestHierarchyWorldMatrix() {
    printf("Test: parent-child world matrix through the transform system\n");

    ClearScene();
    const float PI = 3.14159265358979323846f;

    const uint32_t parentId = AddToScene("parent", nullptr, Material());
    const uint32_t childId  = AddToScene("child",  nullptr, Material());
    SceneObject* p = GetSceneObject(parentId);
    SceneObject* c = GetSceneObject(childId);
    CHECK(p && c);
    if (!p || !c) return;

    p->position = glm::vec3(0.0f, 5.0f, 0.0f);
    p->rotation = glm::vec3(0.0f, PI / 2.0f, 0.0f);
    c->position = glm::vec3(1.0f, 0.0f, 0.0f);
    c->parentId = parentId;

    TickEcs(0.016f);

    // child world = parentWorld * childLocal
    const glm::mat4 parentLocal = LegacyLocal(glm::vec3(p->position.x, p->position.y, p->position.z),
                                              glm::vec3(p->rotation.x, p->rotation.y, p->rotation.z),
                                              glm::vec3(p->scale.x, p->scale.y, p->scale.z));
    const glm::mat4 childLocal  = LegacyLocal(glm::vec3(c->position.x, c->position.y, c->position.z),
                                              glm::vec3(c->rotation.x, c->rotation.y, c->rotation.z),
                                              glm::vec3(c->scale.x, c->scale.y, c->scale.z));
    const glm::mat4 expected = parentLocal * childLocal;

    const Ecs::WorldTransform* cwt = GetEcsWorld().Get<Ecs::WorldTransform>(childId);
    CHECK(cwt != nullptr);
    if (!cwt) return;
    CHECK(Mat4Diff(cwt->localToWorld, expected) < 1e-5f);

    // Concrete spot check: child sits at (0,5,0) + Ry(90)*(1,0,0) = (0,5,-1).
    const glm::vec3 origin(cwt->localToWorld[3].x,
                            cwt->localToWorld[3].y,
                            cwt->localToWorld[3].z);
    CHECK_NEAR(origin.x, 0.0f, 1e-5f);
    CHECK_NEAR(origin.y, 5.0f, 1e-5f);
    CHECK_NEAR(origin.z, -1.0f, 1e-5f);
}

static void TestFreeListConsistency() {
    printf("Test: CreateEntity(id) keeps the free list consistent\n");

    // Plain recycling still works: destroyed id is reused by no-arg create.
    {
        Ecs::World w;
        const Ecs::Entity a = w.CreateEntity();
        w.Destroy(a);
        const Ecs::Entity b = w.CreateEntity();
        CHECK(b == a);
    }

    // Explicit reservation must pull the id out of the free list: a later
    // no-arg CreateEntity() must not hand out an already-alive id.
    {
        Ecs::World w;
        const Ecs::Entity a = w.CreateEntity();  // id 1
        const Ecs::Entity b = w.CreateEntity();  // id 2
        w.Destroy(b);                            // freeList = [2]
        const Ecs::Entity c = w.CreateEntity(b); // re-reserve 2 explicitly
        CHECK(c == b);
        CHECK(w.IsAlive(c));
        const Ecs::Entity d = w.CreateEntity();  // must NOT be 2
        CHECK(d != c);
        CHECK(w.IsAlive(d));
        (void)a;
    }
}

int main() {
    TestRotationUnits();
    TestHierarchyWorldMatrix();
    TestFreeListConsistency();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    printf(g_failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
