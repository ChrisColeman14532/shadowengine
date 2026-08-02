# Test3Game Development Guide

## Structure

```
CMakeLists.txt       # root build config (C++17, MSVC)
engine/              # CoreEngine library — static lib, links GLFW + OpenGL
  include/core/engine.h  # public API: Init, Shutdown, Render, etc.
  src/engine.cpp         # implementation
editor/              # Editor executable — links engine
  include/editor.h
  src/main.cpp   main entry (Editor::Init -> loop -> ShutDown)
  src/editor.cpp   Editor namespace wiring to CoreEngine
```

## Build

CMake configures to **Visual Studio** generator. Reconfigure in `build/` if CMakeLists.txt changes. GLFW 3.4 is fetched via FetchContent on first run.

No tests, lint, or CI exist.

## Commands

- **Before pushing**: run tests (none currently exist). Add a test framework when tests are ready.
- **Build artifacts in `build/` only** — never commit generated files. Already ignored by `.gitignore`.

## Gotchas

- GLFW is pulled from git if not found on the system; no vendored copy lives in the repo.
- `opencode.json` contains Mnemoverse API credentials for MCP memory -- it's gitignored but should be rotated if leaked.
- The engine has a global static `s_window` in two files (engine.cpp + editor.cpp) that shadows each other; this is intentional wiring, not a bug.
