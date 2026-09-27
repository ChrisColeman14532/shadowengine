// Embedded-texture lifetime test (headless, no GL window needed).
// Usage: test_embedded_tex.exe [model.fbx]   (default: tools/elf.fbx)
//
// Regression test for the dangling-pointer bug in AssetLoader:
// LoadFBX() used to store &model.textures.back() (a pointer into the
// LOCAL FBXModel) in s_allEmbeddedTextures. The local is destroyed on
// return, so the second import (whose ClearAll() -> CleanupEmbeddedTextures()
// dereferenced those pointers) was a use-after-free.
//
// This test hammers exactly that path: Load -> ClearAll -> Load -> ClearAll,
// reading the decoded pixels back after each load to prove the buffers are
// still valid, and exiting cleanly only if every cycle survives.
//
// Gates:
//   G1  each load succeeds and reports at least one embedded texture with data
//   G2  the first texture's pixels are readable (non-null, sized) right after load
//   G3  ClearAll() between loads does not crash (the old UAF trigger)
//   G4  a fresh load after ClearAll yields valid texture data again

#include "core/asset_loader.h"
#include "core/model.h"
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);  // no buffering: a crash must not eat the report
    std::string path = (argc > 1) ? argv[1] : "tools/elf.fbx";
    const int kCycles = 3;

    for (int cycle = 1; cycle <= kCycles; ++cycle) {
        printf("=== cycle %d/%d: %s ===\n", cycle, kCycles, path.c_str());

        // Scoped: the returned FBXModel's textures are NON-OWNING views into
        // buffers owned by the AssetLoader; they must not be touched after
        // the ClearAll() below.
        {
            CoreEngine::FBXModel m = AssetLoader::LoadFBX(path, false);

            // G1: load succeeded and has at least one real texture
            if (!m.success) {
                printf("FAIL G1: load failed on cycle %d\n", cycle);
                return 1;
            }
            int realTextures = 0;
            for (const auto& t : m.textures) if (t.data) ++realTextures;
            if (realTextures == 0) {
                printf("FAIL G1: no embedded textures with data (got %zu total)\n",
                       m.textures.size());
                return 1;
            }

            // G2: pixels are readable right after load
            const auto& t = m.textures.front();
            bool firstIsReal = t.data != nullptr;
            if (firstIsReal) {
                size_t expect = (size_t)t.width * t.height * t.channels;
                printf("  first texture: %dx%d ch=%d (%zu bytes), first bytes:",
                       t.width, t.height, t.channels, expect);
                for (int i = 0; i < 4 && i < (int)expect; ++i)
                    printf(" %u", t.data[i]);
                printf("\n");
                if (t.width <= 0 || t.height <= 0 || t.channels <= 0 || expect == 0) {
                    printf("FAIL G2: degenerate texture dims\n");
                    return 1;
                }
            } else {
                // First slot is a placeholder (decode failure); find the first real one
                const CoreEngine::FBXModel::EmbeddedTexture* real = nullptr;
                for (const auto& x : m.textures) { if (x.data) { real = &x; break; } }
                if (!real) { printf("FAIL G2: no readable texture\n"); return 1; }
                printf("  first slot is a placeholder; real texture %dx%d ch=%d, first bytes:",
                       real->width, real->height, real->channels);
                size_t expect = (size_t)real->width * real->height * real->channels;
                for (int i = 0; i < 4 && i < (int)expect; ++i)
                    printf(" %u", real->data[i]);
                printf("\n");
            }
            printf("  OK: %d embedded texture(s) with data\n", realTextures);
        }

        // G3: the old UAF trigger — ClearAll() frees the pixel buffers and
        // used to read freed EmbeddedTexture structs on top of that.
        AssetLoader::ClearAll();
        printf("  OK: ClearAll() survived\n");
    }

    printf("PASS: %d load/clear cycles, embedded texture buffers valid each time\n", kCycles);
    return 0;
}
