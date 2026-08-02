cmake_minimum_required(VERSION 3.16)

# ============================================================
# Test3Game - Game engine version 0
# A C++ / OpenGL game engine with an editor
# ============================================================

project(Test3Game VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Find GLFW & OpenGL ----
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

# ---- Engine library ----
add_library(engine STATIC
    engine/src/engine.cpp
)
target_include_directories(engine PUBLIC
    engine/include
)
target_link_libraries(engine PUBLIC
    glfw
    OpenGL::GL
)

# ---- Editor executable ----
add_executable(editor
    editor/src/main.cpp
    editor/src/editor.cpp
)
target_include_directories(editor PRIVATE
    editor/include
)
target_link_libraries(editor PRIVATE engine)
