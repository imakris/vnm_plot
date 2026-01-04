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

include(FetchContent)

# -----------------------------------------------------------------------------
# Find or fetch required dependencies
# -----------------------------------------------------------------------------

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
        GIT_TAG        v1.0.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_Populate(glatter)

    if(_vnm_plot_reset_cmp0169)
        cmake_policy(POP)
    endif()

    set(GLATTER_SOURCE_DIR "${glatter_SOURCE_DIR}" CACHE PATH "Glatter source directory" FORCE)
endif()

# --- FreeType ---
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

# --- msdfgen ---
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

# -----------------------------------------------------------------------------
# Source files - Core (Qt-free)
# -----------------------------------------------------------------------------

set(VNM_PLOT_CORE_SOURCES
    # Core abstractions
    src/core/gl_program.cpp
    src/core/asset_loader.cpp
    src/core/utf8_utils.cpp
)

set(VNM_PLOT_CORE_HEADERS
    # Core headers
    include/vnm_plot/core/core_types.h
    include/vnm_plot/core/gl_program.h
    include/vnm_plot/core/asset_loader.h
    include/vnm_plot/core/utf8_utils.h
)

# -----------------------------------------------------------------------------
# Source files - Full library (Qt-dependent)
# -----------------------------------------------------------------------------

set(VNM_PLOT_SOURCES
    # Layout
    src/layout/layout_calculator.cpp

    # Renderers
    src/renderers/primitive_renderer.cpp
    src/renderers/font_renderer.cpp
    src/renderers/text_renderer.cpp
    src/renderers/chrome_renderer.cpp
    src/renderers/series_renderer.cpp

    # Main components
    src/plot_renderer.cpp
    src/plot_widget.cpp
)

set(VNM_PLOT_HEADERS
    # Public headers
    include/vnm_plot/data_source.h
    include/vnm_plot/function_sample.h
    include/vnm_plot/plot_algo.h
    include/vnm_plot/plot_config.h
    include/vnm_plot/plot_renderer.h
    include/vnm_plot/plot_types.h
    include/vnm_plot/plot_widget.h
    include/vnm_plot/render_types.h
    include/vnm_plot/color_palette.h

    # Layout headers
    include/vnm_plot/layout/layout_calculator.h

    # Renderer headers
    include/vnm_plot/renderers/chrome_renderer.h
    include/vnm_plot/renderers/font_renderer.h
    include/vnm_plot/renderers/primitive_renderer.h
    include/vnm_plot/renderers/series_renderer.h
    include/vnm_plot/renderers/text_renderer.h

    # Internal headers
    include/vnm_plot/internal/tls_registry.h
)

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
)

target_compile_features(vnm_plot_core PUBLIC cxx_std_20)

# Link dependencies (Qt-free)
target_link_libraries(vnm_plot_core
    PUBLIC
        glm::glm
)

# Platform-specific settings for core
if(WIN32)
    target_link_libraries(vnm_plot_core PRIVATE opengl32)
    target_compile_definitions(vnm_plot_core PRIVATE NOMINMAX)
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

    # Link dependencies
    target_link_libraries(vnm_plot
        PUBLIC
            vnm_plot_core
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::OpenGL
            glm::glm
        PRIVATE
            msdfgen::msdfgen-core
            msdfgen::msdfgen-ext
    )

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
# Installation (only when building as top-level project)
# NOTE: Installation of targets that depend on FetchContent dependencies
# requires those dependencies to also be exported. For simplicity, we only
# install headers when using fetched dependencies.
# -----------------------------------------------------------------------------

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(GNUInstallDirs)

    # Install headers
    install(DIRECTORY include/vnm_plot
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # TODO: Full installation support requires either:
    # 1. System-installed dependencies (glm, freetype, msdfgen)
    # 2. Or bundling/installing the FetchContent dependencies
endif()
