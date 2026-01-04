cmake_minimum_required(VERSION 3.16)

project(standalone_glfw
    VERSION 0.1.0
    DESCRIPTION "Qt-free vnm_plot_core example using GLFW"
    LANGUAGES CXX
)

# Build vnm_plot_core when running standalone
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.."
                     "${CMAKE_CURRENT_BINARY_DIR}/vnm_plot")
endif()

include(FetchContent)

# GLFW (pinned)
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

add_executable(standalone_glfw
    main.cpp
)

target_compile_features(standalone_glfw PRIVATE cxx_std_20)

target_link_libraries(standalone_glfw PRIVATE
    vnm_plot::core
    glfw
)
