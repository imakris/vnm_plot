# ==============================================================================
# Function Plotter Example
# Demonstrates vnm_plot with mexce for plotting arbitrary mathematical functions
# ==============================================================================

cmake_minimum_required(VERSION 3.16)

# Only define project when building standalone (not as subdirectory)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(function_plotter
        VERSION 0.1.0
        DESCRIPTION "Interactive function plotter using vnm_plot and mexce"
        LANGUAGES CXX
    )
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 OpenGL Multimedia)

# vnm_plot library (from parent repo)
if(NOT TARGET vnm_plot::vnm_plot)
    set(VNM_PLOT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.."
                     "${CMAKE_CURRENT_BINARY_DIR}/vnm_plot")
endif()

# mexce - use local checkout if available, otherwise fetch from GitHub
set(MEXCE_LOCAL_PATH "" CACHE PATH "Path to local mexce checkout (optional)")

if(MEXCE_LOCAL_PATH AND EXISTS "${MEXCE_LOCAL_PATH}/mexce.h")
    message(STATUS "Using local mexce checkout: ${MEXCE_LOCAL_PATH}")
    set(mexce_SOURCE_DIR "${MEXCE_LOCAL_PATH}")
else()
    include(FetchContent)
    FetchContent_Declare(mexce
        GIT_REPOSITORY https://github.com/imakris/mexce.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(mexce)
endif()

# Create mexce interface library (header is at repo root, not in include/)
add_library(mexce_lib INTERFACE)
target_include_directories(mexce_lib INTERFACE ${mexce_SOURCE_DIR})

# -----------------------------------------------------------------------------
# Application
# -----------------------------------------------------------------------------

add_executable(function_plotter
    main.cpp
    function_plotter.cpp
    function_plotter.h
    resources.qrc
)

if(WIN32)
    target_sources(function_plotter PRIVATE app_icon.rc)
endif()

target_link_libraries(function_plotter PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::OpenGL
    Qt6::Multimedia
    vnm_plot::vnm_plot
    mexce_lib
)

target_include_directories(function_plotter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# -----------------------------------------------------------------------------
# Resources
# -----------------------------------------------------------------------------

# Copy QML files to build directory for development
add_custom_command(TARGET function_plotter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/qml"
        "$<TARGET_FILE_DIR:function_plotter>/qml"
    COMMENT "Copying QML files"
)
