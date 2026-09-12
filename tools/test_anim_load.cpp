// Test: AssetLoader::LoadFBXAnimation on an animation-only mixamo FBX.
// Build: tools\build_test_anim.bat
#include <cstdio>
#include <core/asset_loader.h>

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: test_anim_load <file.fbx>\n"); return 1; }

    CoreEngine::AnimationFile file = AssetLoader::LoadFBXAnimation(argv[1]);
    if (!file.success || file.clips.empty()) { printf("FAILED: no clips extracted\n"); return 1; }
    printf("nodes in animation tree: %zu\n", file.nodes.size());
    const auto& clips = file.clips;

    for (const auto& c : clips) {
        printf("clip '%s'  duration=%.3fs  fps=%.1f  tracks=%zu\n",
               c.name.c_str(), c.duration, c.fps, c.tracks.size());
        for (const auto& t : c.tracks) {
            printf("  %-45s pos=%-4zu rot=%-4zu scale=%-4zu\n",
                   t.nodeName.c_str(), t.position.size(), t.rotation.size(), t.scale.size());
        }
        // Evaluate mid-clip on a rotation track to sanity-check the sampler
        for (const auto& t : c.tracks) {
            if (!t.rotation.empty() && t.rotation.size() > 2) {
                float mid = c.duration * 0.5f;
                glm::vec3 p; glm::quat r; glm::vec3 s;
                bool ok = c.Evaluate(t, mid, glm::vec3(0), glm::quat(1,0,0,0), glm::vec3(1), p, r, s);
                glm::quat q = glm::normalize(r);
                printf("  eval '%s' @ %.3f: ok=%d quat=(%.3f, %.3f, %.3f, %.3f)\n",
                       t.nodeName.c_str(), mid, (int)ok, q.x, q.y, q.z, q.w);
                break;
            }
        }
    }
    printf("OK\n");
    return 0;
}
