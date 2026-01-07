# ==============================================================================
# vnm_plot Headless GLFW Benchmark
# Synthetic benchmark for vnm_plot rendering performance (CI-compatible)
# ==============================================================================
# This benchmark runs headless using GLFW with an invisible window and
# renders to a framebuffer object (FBO). It can run in CI environments
# with software rendering (Mesa/llvmpipe) or xvfb.

cmake_minimum_required(VERSION 3.16)

# Build vnm_plot_core when running standalone
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(vnm_plot_benchmark_headless
        VERSION 1.0.0
        DESCRIPTION "Headless GLFW benchmark for vnm_plot (CI-compatible)"
        LANGUAGES CXX
    )

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # Build vnm_plot_core from parent
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.."
                     "${CMAKE_CURRENT_BINARY_DIR}/vnm_plot")
endif()

# Find vnm_plot_core (should be available from parent)
if(NOT TARGET vnm_plot::core)
    message(FATAL_ERROR "vnm_plot_core target not found. Build benchmark from vnm_plot root or standalone.")
endif()

include(FetchContent)

# GLFW (pinned for stability)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)

# -----------------------------------------------------------------------------
# Headless Benchmark executable
# -----------------------------------------------------------------------------

add_executable(vnm_plot_benchmark_headless
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../benchmark/src/benchmark_frame.cpp
)

target_compile_features(vnm_plot_benchmark_headless PRIVATE cxx_std_20)

target_include_directories(vnm_plot_benchmark_headless
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../benchmark/include
)

target_link_libraries(vnm_plot_benchmark_headless PRIVATE
    vnm_plot::core
    glfw
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(vnm_plot_benchmark_headless PRIVATE NOMINMAX)
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(vnm_plot_benchmark_headless PRIVATE /W4)
else()
    target_compile_options(vnm_plot_benchmark_headless PRIVATE -Wall -Wextra)
endif()

message(STATUS "vnm_plot: Building headless GLFW benchmark executable")
