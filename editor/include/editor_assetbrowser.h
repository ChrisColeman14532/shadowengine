#pragma once

#include <string>
#include <vector>

namespace Editor {

    // ── Asset Browser ───────────────────────────────────────────────
    // Manages the `assets/` folder (relative to the working directory)
    // and renders the docked "Asset Browser" panel (bottom of the left
    // column, under the Scene Hierarchy).
    //
    //   • lists every file in assets/ (recursive), refreshed periodically
    //   • files dragged in from Windows Explorer (OS drop) are copied
    //     into assets/ — any type (fbx, audio, textures, ...) is accepted,
    //     with automatic renaming on name collisions
    //   • browser items can be dragged onto the Scene Hierarchy (or
    //     double-clicked) to add the asset to the scene:
    //         .fbx     → imported as a model (LoadFBXAtPath)
    //         textures → applied as diffuse on the selected object
    //         other    → console message (e.g. audio has no system yet)
    namespace AssetBrowser {

        // Assets directory (relative to the working directory).
        std::string AssetsDir();

        // (Re)scan the assets directory. Creates it if missing.
        void Refresh();

        // One listed file.
        struct AssetEntry {
            std::string relativePath;  // "models/car.fbx" (forward slashes)
            std::string fullPath;      // path as seen from the working dir
            long long   sizeBytes = 0;
        };

        // Currently listed assets (valid until the next Refresh()).
        const std::vector<AssetEntry>& Entries();

        // Render the "Asset Browser" docked panel.
        void RenderPanel();

        // Add the asset at `fullPath` to the scene (see header notes).
        // Returns true when the asset was added/applied.
        bool AddToScene(const std::string& fullPath);

        // OS file drop (GLFW drop callback, Explorer etc.). Queues paths;
        // PumpExternalDrops() imports them into assets/.
        void QueueExternalFiles(int count, const char* const* paths);

        // Call once per frame (before the ImGui frame): import the queued
        // external files into the assets folder and refresh the list.
        void PumpExternalDrops();

        // ImGui drag&drop payload type for browser → hierarchy drags.
        const char* PayloadType();

    }  // namespace AssetBrowser

}  // namespace Editor
