cmake_minimum_required(VERSION 3.20)
project(pngclean VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

add_executable(pngclean main.cpp)
target_include_directories(pngclean PRIVATE ${stb_SOURCE_DIR})
