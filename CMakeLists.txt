cmake_minimum_required(VERSION 3.16)

# ============================================================
# ShadowEngine - Game engine with asset loading support
# C++17, MSVC / Visual Studio generator
# ============================================================

project(ShadowEngine VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Find dependencies via vcpkg (GLEW, assimp), FetchContent for GLFW ----
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
    )
    FetchContent_MakeAvailable(glfw)
endif()

find_package(OpenGL REQUIRED)
find_package(GLEW CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# ---- Dear ImGui (via FetchContent) ----
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
)
FetchContent_MakeAvailable(imgui)

# ---- Engine library ----
add_library(engine STATIC
    engine/src/engine.cpp
    engine/src/shader.cpp
    engine/src/primitives.cpp
    engine/src/scene.cpp
    engine/src/camera.cpp
    engine/src/texture.cpp
    engine/src/material.cpp
    engine/src/shadow_map.cpp
    engine/src/skybox.cpp
    engine/src/debug_draw.cpp
    engine/src/animation.cpp
    engine/src/asset_loader.cpp
    engine/src/stb_image_loader.cpp
)
target_include_directories(engine PUBLIC
    engine/include
    engine/src
)
target_link_libraries(engine PUBLIC
    glfw
    OpenGL::GL
    GLEW::GLEW
    assimp::assimp
    glm::glm-header-only
)

# ---- Editor executable ----
add_executable(editor
    editor/src/main.cpp
    editor/src/editor.cpp
    editor/src/editor_state.cpp
    editor/src/editor_camera.cpp
    editor/src/editor_ui.cpp
    editor/src/editor_render.cpp
    editor/src/editor_asset.cpp
    
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(editor PRIVATE
    editor/include
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(editor PRIVATE engine)

# Copy vcpkg DLLs to build output directory
add_custom_command(TARGET editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows/bin
        $<TARGET_FILE_DIR:editor>
    COMMENT "Copying vcpkg DLLs to build output"
)

# Copy shader files to build output directory
add_custom_command(TARGET editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:editor>/shader
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/shader/material.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shader/material.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shader/shadow_depth.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shader/shadow_depth.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shader/skybox.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shader/skybox.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shader/grid.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shader/grid.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shader/bounds.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shader/bounds.frag
        $<TARGET_FILE_DIR:editor>/shader/
    COMMENT "Copying shader files to build output"
)
