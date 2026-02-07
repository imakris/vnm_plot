# ==============================================================================
# VNM Plot Library
# A high-performance, GPU-accelerated plotting library
# ==============================================================================

cmake_minimum_required(VERSION 3.16)

project(vnm_plot
    VERSION 0.1.0
    DESCRIPTION "High-performance GPU-accelerated plotting library"
    LANGUAGES CXX C
)

# Options
option(VNM_PLOT_BUILD_EXAMPLES "Build example applications" OFF)
option(VNM_PLOT_BUILD_QT "Build Qt wrapper library (requires Qt6)" ON)
option(VNM_PLOT_CORE_ONLY "Build only the Qt-free core library" OFF)
option(VNM_PLOT_ENABLE_TEXT "Enable MSDF text rendering (requires FreeType + msdfgen)" ON)
option(VNM_PLOT_BUILD_TESTS "Build core tests" OFF)

include(FetchContent)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedAssets.cmake)

# -----------------------------------------------------------------------------
# Find or fetch required dependencies
# -----------------------------------------------------------------------------

# OpenGL is required for core library (optional at configure time for header-only usage)
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    if(NOT OPENGL_opengl_LIBRARY AND NOT OPENGL_gl_LIBRARY)
        find_file(_vnm_plot_glvnd_lib
            NAMES libOpenGL.so libOpenGL.so.0
            PATHS /usr/local/lib /usr/lib
        )
        find_file(_vnm_plot_gl_lib
            NAMES libGL.so.1 libGL.so
            PATHS /usr/local/lib /usr/lib
        )
        if(_vnm_plot_glvnd_lib)
            set(OPENGL_opengl_LIBRARY "${_vnm_plot_glvnd_lib}" CACHE FILEPATH "" FORCE)
        elseif(_vnm_plot_gl_lib)
            set(OPENGL_gl_LIBRARY "${_vnm_plot_gl_lib}" CACHE FILEPATH "" FORCE)
        endif()
        unset(_vnm_plot_glvnd_lib)
        unset(_vnm_plot_gl_lib)
    endif()
endif()
find_package(OpenGL)

# Qt6 is optional - only needed if building Qt wrapper
if(VNM_PLOT_CORE_ONLY)
    set(VNM_PLOT_BUILD_QT OFF)
elseif(VNM_PLOT_BUILD_QT)
    find_package(Qt6 COMPONENTS Core Gui Quick OpenGL)
    if(NOT Qt6_FOUND)
        message(STATUS "vnm_plot: Qt6 not found, building core library only")
        set(VNM_PLOT_BUILD_QT OFF)
    endif()
endif()

# Text rendering is optional - stub implementations maintain stable layout when OFF
if(NOT VNM_PLOT_ENABLE_TEXT)
    message(STATUS "vnm_plot: Text rendering disabled (using stub implementations)")
endif()

# --- GLM ---
if(TARGET glm::glm)
    message(STATUS "vnm_plot: Using existing glm::glm target")
elseif(TARGET glm)
    add_library(glm::glm ALIAS glm)
    message(STATUS "vnm_plot: Using existing glm target (aliased)")
else()
    message(STATUS "vnm_plot: Fetching glm")
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glm)
    if(TARGET glm AND NOT TARGET glm::glm)
        add_library(glm::glm ALIAS glm)
    endif()
endif()

# --- Glatter (OpenGL extension loading) ---
if(DEFINED GLATTER_SOURCE_DIR AND EXISTS "${GLATTER_SOURCE_DIR}/include/glatter/glatter.h")
    message(STATUS "vnm_plot: Using existing GLATTER_SOURCE_DIR: ${GLATTER_SOURCE_DIR}")
else()
    message(STATUS "vnm_plot: Fetching glatter")

    # Handle CMake policy for FetchContent_Populate
    set(_vnm_plot_reset_cmp0169 FALSE)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
        set(_vnm_plot_reset_cmp0169 TRUE)
    endif()

    FetchContent_Declare(glatter
        GIT_REPOSITORY https://github.com/imakris/glatter.git
        GIT_TAG        v1.0.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_Populate(glatter)

    if(_vnm_plot_reset_cmp0169)
        cmake_policy(POP)
    endif()

    set(GLATTER_SOURCE_DIR "${glatter_SOURCE_DIR}" CACHE PATH "Glatter source directory" FORCE)
endif()

# --- FreeType + msdfgen (needed for MSDF text rendering) ---
# Used by the core font_renderer for MSDF atlas generation.
if(VNM_PLOT_ENABLE_TEXT)
if(TARGET Freetype::Freetype)
    message(STATUS "vnm_plot: Using existing Freetype::Freetype target")
elseif(TARGET freetype)
    add_library(Freetype::Freetype ALIAS freetype)
    message(STATUS "vnm_plot: Using existing freetype target (aliased)")
else()
    message(STATUS "vnm_plot: Fetching freetype")

    set(_vnm_plot_reset_cmp0169_ft FALSE)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
        set(_vnm_plot_reset_cmp0169_ft TRUE)
    endif()

    FetchContent_Declare(freetype
        GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
        GIT_TAG        VER-2-13-3
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(freetype)
    if(NOT freetype_POPULATED)
        FetchContent_Populate(freetype)

        # Patch freetype's cmake_minimum_required to avoid policy warnings
        set(_freetype_cmakelists "${freetype_SOURCE_DIR}/CMakeLists.txt")
        if(EXISTS "${_freetype_cmakelists}")
            file(READ "${_freetype_cmakelists}" _freetype_cmake_txt)
            set(_freetype_target_version "3.16")

            string(REGEX REPLACE
                   "cmake_minimum_required\\(VERSION [^\\)]+\\)"
                   "cmake_minimum_required(VERSION ${_freetype_target_version})"
                   _freetype_cmake_txt
                   "${_freetype_cmake_txt}")

            string(REGEX REPLACE
                   "cmake_policy\\(VERSION [^\\)]+\\)"
                   "cmake_policy(VERSION ${_freetype_target_version})"
                   _freetype_cmake_txt
                   "${_freetype_cmake_txt}")

            file(READ "${_freetype_cmakelists}" _freetype_original_txt)
            if(NOT _freetype_cmake_txt STREQUAL _freetype_original_txt)
                file(WRITE "${_freetype_cmakelists}" "${_freetype_cmake_txt}")
            endif()
        endif()

        # Disable optional FreeType features
        set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
        set(FT_ENABLE_ERROR_STRINGS OFF CACHE BOOL "" FORCE)

        # Build static FreeType
        set(_vnm_plot_build_shared_libs_saved "${BUILD_SHARED_LIBS}")
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

        add_subdirectory("${freetype_SOURCE_DIR}" "${freetype_BINARY_DIR}")

        # Restore BUILD_SHARED_LIBS
        if(DEFINED _vnm_plot_build_shared_libs_saved)
            set(BUILD_SHARED_LIBS "${_vnm_plot_build_shared_libs_saved}" CACHE BOOL "" FORCE)
        endif()
    endif()

    if(TARGET freetype AND NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()

    if(_vnm_plot_reset_cmp0169_ft)
        cmake_policy(POP)
    endif()
endif()

# Silence warnings when compiling FreeType
if(TARGET Freetype::Freetype)
    get_target_property(_ft_real Freetype::Freetype ALIASED_TARGET)
    if(NOT _ft_real)
        set(_ft_real Freetype::Freetype)
    endif()
    if(TARGET ${_ft_real})
        if(MSVC)
            target_compile_options(${_ft_real} PRIVATE /w)
        else()
            target_compile_options(${_ft_real} PRIVATE -w)
        endif()
    endif()
endif()

# --- msdfgen (needed for MSDF text rendering in core) ---
if(TARGET msdfgen::msdfgen-core AND TARGET msdfgen::msdfgen-ext)
    message(STATUS "vnm_plot: Using existing msdfgen targets")
else()
    message(STATUS "vnm_plot: Fetching msdfgen")
    FetchContent_Declare(msdfgen
        GIT_REPOSITORY https://github.com/Chlumsky/msdfgen.git
        GIT_TAG        v1.12.1
        GIT_SHALLOW    TRUE
    )
    set(MSDFGEN_USE_VCPKG OFF CACHE BOOL "" FORCE)
    set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
    set(MSDFGEN_DISABLE_SVG ON CACHE BOOL "" FORCE)
    set(MSDFGEN_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(MSDFGEN_USE_SKIA OFF CACHE BOOL "" FORCE)
    set(MSDFGEN_CORE_ONLY OFF CACHE BOOL "" FORCE)
    set(MSDFGEN_USE_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(msdfgen)

    # Ensure MSVC runtime matches
    if(MSVC)
        foreach(t msdfgen-core msdfgen-ext msdfgen)
            if(TARGET ${t})
                set_property(TARGET ${t} PROPERTY
                    MSVC_RUNTIME_LIBRARY "MultiThreaded$<IF:$<CONFIG:Debug>,Debug,>DLL")
            endif()
        endforeach()
    endif()
endif()

endif() # VNM_PLOT_ENABLE_TEXT (FreeType + msdfgen)

# -----------------------------------------------------------------------------
# Source files - Core (Qt-free)
# -----------------------------------------------------------------------------

set(VNM_PLOT_CORE_SOURCES
    # Core abstractions
    src/core/gl_program.cpp
    src/core/asset_loader.cpp
    src/core/utf8_utils.cpp
    src/core/platform_paths.cpp
    src/core/layout_calculator.cpp
    src/core/vertex_layout.cpp
    src/core/default_shaders.cpp
    src/core/plot_core.cpp

    # Core renderers
    src/core/primitive_renderer.cpp
    src/core/chrome_renderer.cpp
    src/core/series_renderer.cpp
)

# Add core font/text renderers when text is enabled
if(VNM_PLOT_ENABLE_TEXT)
    list(APPEND VNM_PLOT_CORE_SOURCES
        src/core/font_renderer.cpp
        src/core/text_renderer.cpp
    )
else()
    # Stub implementations for text-off builds
    list(APPEND VNM_PLOT_CORE_SOURCES
        src/core/text_renderer_stub.cpp
    )
endif()

set(VNM_PLOT_CORE_HEADERS
    # Core headers
    include/vnm_plot/core/types.h
    include/vnm_plot/core/access_policy.h
    include/vnm_plot/core/plot_config.h
    include/vnm_plot/core/default_shaders.h
    include/vnm_plot/core/function_sample.h
    include/vnm_plot/core/layout_calculator.h
    include/vnm_plot/core/constants.h
    include/vnm_plot/core/color_palette.h
    include/vnm_plot/core/algo.h
    include/vnm_plot/core/range_cache.h
    include/vnm_plot/core/primitive_renderer.h
    include/vnm_plot/core/chrome_renderer.h
    include/vnm_plot/core/font_renderer.h
    include/vnm_plot/core/text_renderer.h
    include/vnm_plot/core/series_renderer.h
    include/vnm_plot/core/series_builder.h
    include/vnm_plot/core/plot_core.h
    include/vnm_plot/core/vertex_layout.h
    include/vnm_plot/core/gl_program.h
    include/vnm_plot/core/asset_loader.h
)

# -----------------------------------------------------------------------------
# Source files - Full library (Qt-dependent)
# -----------------------------------------------------------------------------

set(VNM_PLOT_SOURCES
    # Main components
    src/qt/plot_renderer.cpp
    src/qt/plot_widget.cpp
    src/qt/plot_interaction_item.cpp
    src/qt/plot_time_axis.cpp
)

set(VNM_PLOT_HEADERS
    # Public headers
    include/vnm_plot/qt/plot_renderer.h
    include/vnm_plot/qt/plot_widget.h
    include/vnm_plot/qt/plot_interaction_item.h
    include/vnm_plot/qt/plot_time_axis.h
)

# -----------------------------------------------------------------------------
# Generate embedded assets (shaders, fonts) for core library
# -----------------------------------------------------------------------------

set(VNM_PLOT_EMBEDDED_ASSETS_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_assets.cpp")

# Core shaders (always embedded)
set(VNM_PLOT_CORE_ASSETS
    "shaders/generic_rect.vert:shaders/generic_rect.vert"
    "shaders/generic_rect.geom:shaders/generic_rect.geom"
    "shaders/generic_rect.frag:shaders/generic_rect.frag"
    "shaders/grid_quad.vert:shaders/grid_quad.vert"
    "shaders/grid_quad.frag:shaders/grid_quad.frag"
    "shaders/function_sample.vert:shaders/function_sample.vert"
    "shaders/plot_line_adjacency.geom:shaders/plot_line_adjacency.geom"
    "shaders/plot_line.frag:shaders/plot_line.frag"
    "shaders/plot_dot_quad.geom:shaders/plot_dot_quad.geom"
    "shaders/plot_dot_quad.frag:shaders/plot_dot_quad.frag"
    "shaders/plot_area.geom:shaders/plot_area.geom"
    "shaders/plot_area.frag:shaders/plot_area.frag"
    "shaders/plot_colormap_line_adjacency.geom:shaders/plot_colormap_line_adjacency.geom"
    "shaders/plot_colormap_line.frag:shaders/plot_colormap_line.frag"
)

# Text rendering assets (only when text is enabled)
if(VNM_PLOT_ENABLE_TEXT)
    list(APPEND VNM_PLOT_CORE_ASSETS
        "shaders/msdf_text.vert:shaders/msdf_text.vert"
        "shaders/msdf_text.frag:shaders/msdf_text.frag"
        "fonts/monospace.ttf:fonts/monospace.ttf"
    )
endif()

embed_assets(
    OUTPUT "${VNM_PLOT_EMBEDDED_ASSETS_OUTPUT}"
    NAMESPACE "vnm::plot"
    ASSETS ${VNM_PLOT_CORE_ASSETS}
)

# Add generated file to core sources
list(APPEND VNM_PLOT_CORE_SOURCES "${VNM_PLOT_EMBEDDED_ASSETS_OUTPUT}")

# -----------------------------------------------------------------------------
# Core Library target (Qt-free)
# -----------------------------------------------------------------------------

add_library(vnm_plot_core STATIC
    ${VNM_PLOT_CORE_SOURCES}
    ${VNM_PLOT_CORE_HEADERS}
    "${GLATTER_SOURCE_DIR}/src/glatter/glatter.c"
)

add_library(vnm_plot::core ALIAS vnm_plot_core)

target_include_directories(vnm_plot_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${GLATTER_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
)

target_compile_features(vnm_plot_core PUBLIC cxx_std_20)

# Link dependencies (Qt-free)
target_link_libraries(vnm_plot_core
    PUBLIC
        glm::glm
)

# Link OpenGL (required at link time, optional at configure time)
if(TARGET OpenGL::OpenGL)
    target_link_libraries(vnm_plot_core PUBLIC OpenGL::OpenGL)
elseif(TARGET OpenGL::GL)
    target_link_libraries(vnm_plot_core PUBLIC OpenGL::GL)
elseif(WIN32)
    target_link_libraries(vnm_plot_core PUBLIC opengl32)
elseif(APPLE)
    target_link_libraries(vnm_plot_core PUBLIC "-framework OpenGL")
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    find_file(VNM_PLOT_GL_FILE
        NAMES libOpenGL.so libOpenGL.so.0 libGL.so.1 libGL.so
        PATHS /usr/local/lib /usr/lib
    )
    if(VNM_PLOT_GL_FILE)
        target_link_libraries(vnm_plot_core PUBLIC ${VNM_PLOT_GL_FILE})
    else()
        set(_vnm_plot_saved_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
        list(APPEND CMAKE_FIND_LIBRARY_SUFFIXES ".so.1")
        find_library(VNM_PLOT_GL_LIB NAMES GL PATHS /usr/local/lib /usr/lib)
        set(CMAKE_FIND_LIBRARY_SUFFIXES "${_vnm_plot_saved_suffixes}")
        if(VNM_PLOT_GL_LIB)
            target_link_libraries(vnm_plot_core PUBLIC ${VNM_PLOT_GL_LIB})
        else()
            target_link_libraries(vnm_plot_core PUBLIC GL)
        endif()
    endif()
else()
    # Linux/BSD: GL is typically provided by the driver
    target_link_libraries(vnm_plot_core PUBLIC GL)
endif()

# GLX provides glX* symbols on GLVND-based systems (e.g., FreeBSD).
if(TARGET OpenGL::GLX)
    target_link_libraries(vnm_plot_core PUBLIC OpenGL::GLX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    find_library(VNM_PLOT_GLX_LIB NAMES GLX GLX.so.0 PATHS /usr/local/lib /usr/lib)
    if(VNM_PLOT_GLX_LIB)
        target_link_libraries(vnm_plot_core PUBLIC ${VNM_PLOT_GLX_LIB})
    endif()
endif()

# Glatter's GLX support uses X11 symbols (e.g., XGetErrorText).
# On GLVND systems OpenGL::GLX pulls X11 in transitively, but when
# the GLX target is unavailable we must link X11 explicitly.
if(UNIX AND NOT APPLE)
    find_package(X11 QUIET)
    if(X11_FOUND)
        target_link_libraries(vnm_plot_core PUBLIC ${X11_LIBRARIES})
    endif()
endif()

# Platform-specific settings for core
if(WIN32)
    target_compile_definitions(vnm_plot_core PRIVATE NOMINMAX)
endif()

# Text rendering compile definition (available to consumers for conditional code)
if(VNM_PLOT_ENABLE_TEXT)
    target_compile_definitions(vnm_plot_core PUBLIC VNM_PLOT_ENABLE_TEXT=1)

    # Link MSDF text rendering dependencies
    target_link_libraries(vnm_plot_core
        PRIVATE
            msdfgen::msdfgen-core
            msdfgen::msdfgen-ext
    )
endif()

# Compiler warnings for core
if(MSVC)
    target_compile_options(vnm_plot_core PRIVATE /W4)
else()
    target_compile_options(vnm_plot_core PRIVATE -Wall -Wextra)
endif()

message(STATUS "vnm_plot: Building Qt-free core library")

# -----------------------------------------------------------------------------
# Qt Resources (only if building Qt wrapper)
# -----------------------------------------------------------------------------

if(VNM_PLOT_BUILD_QT)
    qt6_add_resources(VNM_PLOT_QRC_SOURCES vnm_plot.qrc)
endif()

# -----------------------------------------------------------------------------
# Full Library target (Qt-dependent)
# -----------------------------------------------------------------------------

if(VNM_PLOT_BUILD_QT)
    add_library(vnm_plot STATIC
        ${VNM_PLOT_SOURCES}
        ${VNM_PLOT_HEADERS}
        ${VNM_PLOT_QRC_SOURCES}
    )

    add_library(vnm_plot::vnm_plot ALIAS vnm_plot)

    set_target_properties(vnm_plot PROPERTIES
        AUTOMOC ON
    )

    target_include_directories(vnm_plot
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${GLATTER_SOURCE_DIR}/include
    )

    target_compile_features(vnm_plot PUBLIC cxx_std_20)

    target_compile_definitions(vnm_plot PUBLIC VNM_PLOT_WITH_QT=1)

    # Link dependencies
    target_link_libraries(vnm_plot
        PUBLIC
            vnm_plot_core
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::OpenGL
            glm::glm
    )

    # Link MSDF text rendering dependencies if enabled
    # (VNM_PLOT_ENABLE_TEXT compile definition is inherited from vnm_plot_core)
    if(VNM_PLOT_ENABLE_TEXT)
        target_link_libraries(vnm_plot
            PRIVATE
                msdfgen::msdfgen-core
                msdfgen::msdfgen-ext
        )
    endif()

    # Platform-specific settings
    if(WIN32)
        target_link_libraries(vnm_plot PRIVATE opengl32)
        target_compile_definitions(vnm_plot PRIVATE NOMINMAX)
    endif()

    # Compiler warnings
    if(MSVC)
        target_compile_options(vnm_plot PRIVATE /W4)
    else()
        target_compile_options(vnm_plot PRIVATE -Wall -Wextra)
    endif()

    message(STATUS "vnm_plot: Building Qt wrapper library")
endif()

# -----------------------------------------------------------------------------
# Examples (require Qt)
# -----------------------------------------------------------------------------

if(VNM_PLOT_BUILD_EXAMPLES AND VNM_PLOT_BUILD_QT)
    add_subdirectory(examples)
endif()

# -----------------------------------------------------------------------------
# Benchmark (requires Qt for window management)
# -----------------------------------------------------------------------------

option(VNM_PLOT_BUILD_BENCHMARK "Build benchmark executable" OFF)

if(VNM_PLOT_BUILD_BENCHMARK)
    # Ensure Qt6 OpenGLWidgets is available for benchmark (QOpenGLWidget)
    if(NOT TARGET Qt6::OpenGLWidgets)
        find_package(Qt6 COMPONENTS Widgets OpenGLWidgets)
    endif()
    if(TARGET Qt6::OpenGLWidgets)
        add_subdirectory(benchmark)
    else()
        message(STATUS "vnm_plot: Benchmark requires Qt6::OpenGLWidgets, skipping")
    endif()
endif()

# -----------------------------------------------------------------------------
# Headless GLFW Benchmark (CI-compatible, no Qt required)
# -----------------------------------------------------------------------------

option(VNM_PLOT_BUILD_BENCHMARK_HEADLESS "Build headless GLFW benchmark (CI-compatible)" OFF)

if(VNM_PLOT_BUILD_BENCHMARK_HEADLESS)
    add_subdirectory(benchmark_headless)
endif()

# -----------------------------------------------------------------------------
# Tests (headless, core-only)
# -----------------------------------------------------------------------------

if(VNM_PLOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# -----------------------------------------------------------------------------
# Installation (only when building as top-level project)
# -----------------------------------------------------------------------------

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(GNUInstallDirs)

    # Install headers
    install(DIRECTORY include/vnm_plot
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Install glatter headers (required by vnm_plot_core)
    install(DIRECTORY "${GLATTER_SOURCE_DIR}/include/glatter"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Targets to install
    set(_vnm_plot_install_targets vnm_plot_core)
    if(VNM_PLOT_BUILD_QT)
        list(APPEND _vnm_plot_install_targets vnm_plot)
    endif()

    # Install library targets (without export for simplicity with FetchContent deps)
    install(TARGETS ${_vnm_plot_install_targets}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Generate version file for find_package
    include(CMakePackageConfigHelpers)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/vnm_plot-config-version.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    # Install simple config file
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vnm_plot-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/vnm_plot-config.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vnm_plot
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/vnm_plot-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/vnm_plot-config-version.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vnm_plot
    )
endif()
