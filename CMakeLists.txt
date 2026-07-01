cmake_minimum_required(VERSION 3.20)
project(StarReach VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ── raylib ────────────────────────────────────────────────────────────────────
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# ── Dear ImGui (source only — no CMakeLists in the imgui repo) ────────────────
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.7
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# ── rlImGui (raylib ↔ ImGui bridge — fetched for source, not built via its own CMake) ──
FetchContent_Declare(rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        Raylib_5_5
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(rlimgui)
if(NOT rlimgui_POPULATED)
    FetchContent_Populate(rlimgui)
endif()

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
 "src/shared/entities/HealthComponent.h")
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${rlimgui_SOURCE_DIR}
)
target_link_libraries(imgui_lib PUBLIC raylib)

# ── Box2D 2.4.1 ───────────────────────────────────────────────────────────────
FetchContent_Declare(box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG        v2.4.1
    GIT_SHALLOW    TRUE
)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED    OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS       OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box2d)

# ── nlohmann/json ─────────────────────────────────────────────────────────────
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

# ── ENet 1.3.18 (UDP networking for multiplayer) ──────────────────────────────
# ENet's own CMakeLists defines the `enet` target and, on Windows, links the
# required ws2_32 / winmm system libs for us.
FetchContent_Declare(enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG        v1.3.18
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(enet)

# ── StarReach executable ──────────────────────────────────────────────────────
# GLOB_RECURSE rescans on CMake configure — retrigger configure when adding new .cpp files
file(GLOB_RECURSE STARREACH_SOURCES "src/*.cpp")

add_executable(StarReach ${STARREACH_SOURCES} "src/shared/entities/HealthComponent.h")

target_include_directories(StarReach PRIVATE src/)

# ENet 1.3.x uses directory-scoped include_directories, so it does not propagate
# its headers to consumers — add the include path explicitly.
target_include_directories(StarReach PRIVATE ${enet_SOURCE_DIR}/include)

target_link_libraries(StarReach PRIVATE
    raylib
    imgui_lib
    box2d
    nlohmann_json::nlohmann_json
    enet
)

# ENet's static lib does not propagate the Win32 socket libs it needs; link them.
if(WIN32)
    target_link_libraries(StarReach PRIVATE ws2_32 winmm)
endif()

# ── Copy runtime assets to the build output directory (always, not just on rebuild) ──
add_custom_target(CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:StarReach>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config $<TARGET_FILE_DIR:StarReach>/config
    COMMENT "Copying assets and config to build output..."
)
add_dependencies(StarReach CopyAssets)
