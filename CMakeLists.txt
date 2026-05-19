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
option(VNM_PLOT_BUILD_FUNCTION_PLOTTER "Build the Qt Multimedia function_plotter example" ON)
option(VNM_PLOT_ENABLE_TEXT "Enable MSDF text rendering (requires FreeType + msdfgen)" ON)
option(VNM_PLOT_BUILD_TESTS "Build test suite" OFF)
option(VNM_PLOT_USE_SYSTEM_LIBS "Prefer find_package dependencies over FetchContent when available" OFF)
set(VNM_PLOT_VNM_MSDF_TEXT_VERSION 0.2.0)

include(FetchContent)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedAssets.cmake)

function(vnm_plot_set_fetchcontent_build_dir name dir_name)
    string(TOLOWER "${CMAKE_CXX_COMPILER_ID}" _fc_compiler_tag)
    set(_fc_config_suffix "-${_fc_compiler_tag}")
    if(CMAKE_BUILD_TYPE)
        string(TOLOWER "${CMAKE_BUILD_TYPE}" _fc_bt_lower)
        string(APPEND _fc_config_suffix "-${_fc_bt_lower}")
    endif()

    set(FETCHCONTENT_BINARY_DIR_${name}
        "${FETCHCONTENT_BASE_DIR}/${dir_name}-build${_fc_config_suffix}"
        CACHE PATH "" FORCE)
endfunction()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/workspace_deps_overrides.cmake OPTIONAL)
if(VNM_PLOT_DEP_OVERRIDES_FILE AND EXISTS "${VNM_PLOT_DEP_OVERRIDES_FILE}")
    message(STATUS "vnm_plot: Loading dependency overrides from ${VNM_PLOT_DEP_OVERRIDES_FILE}")
    include("${VNM_PLOT_DEP_OVERRIDES_FILE}")
endif()

# -----------------------------------------------------------------------------
# Find or fetch required dependencies
# -----------------------------------------------------------------------------

# QRhi is the only renderer backend.
find_package(Qt6 REQUIRED COMPONENTS Core Gui GuiPrivate Quick)
find_package(Qt6ShaderTools QUIET)
if(NOT Qt6ShaderTools_FOUND)
    message(FATAL_ERROR
        "vnm_plot: Qt6ShaderTools is required for the QSB shader pipeline. "
        "Install the Qt 6 'Shader Tools' module on the build machine.")
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
    if(VNM_PLOT_USE_SYSTEM_LIBS)
        find_package(glm CONFIG QUIET)
        if(TARGET glm::glm)
            message(STATUS "vnm_plot: Using glm from find_package")
        endif()
    endif()
    if(NOT TARGET glm::glm)
        if(FETCHCONTENT_SOURCE_DIR_GLM AND
           EXISTS "${FETCHCONTENT_SOURCE_DIR_GLM}/CMakeLists.txt")
            message(STATUS "vnm_plot: Using local glm checkout: ${FETCHCONTENT_SOURCE_DIR_GLM}")
        else()
            message(STATUS "vnm_plot: Fetching glm")
        endif()
        # Keep full history for fetched libraries because this workspace patches and
        # reviews them directly instead of treating them as disposable third-party code.
        FetchContent_Declare(glm
            GIT_REPOSITORY https://github.com/g-truc/glm.git
            GIT_TAG        1.0.1
            GIT_SHALLOW    FALSE
        )
        FetchContent_MakeAvailable(glm)
        if(TARGET glm AND NOT TARGET glm::glm)
            add_library(glm::glm ALIAS glm)
        endif()
    endif()
endif()

# --- Shared MSDF text atlas builder ---
if(VNM_PLOT_ENABLE_TEXT)
    set(VNM_MSDF_TEXT_USE_SYSTEM_LIBS "${VNM_PLOT_USE_SYSTEM_LIBS}" CACHE BOOL "" FORCE)
    if(TARGET vnm_msdf_text::vnm_msdf_text)
        message(STATUS "vnm_plot: Using existing vnm_msdf_text target")
    else()
        if(VNM_PLOT_USE_SYSTEM_LIBS)
            find_package(vnm_msdf_text ${VNM_PLOT_VNM_MSDF_TEXT_VERSION} CONFIG QUIET)
            if(TARGET vnm_msdf_text::vnm_msdf_text)
                message(STATUS "vnm_plot: Using vnm_msdf_text from find_package")
            endif()
        endif()
        if(NOT TARGET vnm_msdf_text::vnm_msdf_text)
            get_filename_component(_vnm_plot_msdf_text_source
                "${CMAKE_CURRENT_SOURCE_DIR}/../vnm_msdf_text"
                ABSOLUTE)
            if(EXISTS "${_vnm_plot_msdf_text_source}/CMakeLists.txt")
                message(STATUS "vnm_plot: Using sibling vnm_msdf_text source")
                vnm_plot_set_fetchcontent_build_dir(vnm_msdf_text vnm_msdf_text)
                FetchContent_Declare(vnm_msdf_text
                    SOURCE_DIR  "${_vnm_plot_msdf_text_source}"
                    BINARY_DIR  "${FETCHCONTENT_BINARY_DIR_vnm_msdf_text}"
                )
            else()
                message(STATUS "vnm_plot: Fetching vnm_msdf_text")
                vnm_plot_set_fetchcontent_build_dir(vnm_msdf_text vnm_msdf_text)
                FetchContent_Declare(vnm_msdf_text
                    GIT_REPOSITORY https://github.com/imakris/vnm_msdf_text.git
                    GIT_TAG        master
                    GIT_SHALLOW    FALSE
                    BINARY_DIR     "${FETCHCONTENT_BINARY_DIR_vnm_msdf_text}"
                )
            endif()
            FetchContent_MakeAvailable(vnm_msdf_text)
            unset(_vnm_plot_msdf_text_source)
        endif()
    endif()
endif() # VNM_PLOT_ENABLE_TEXT

# -----------------------------------------------------------------------------
# Source files - Core
# -----------------------------------------------------------------------------

set(VNM_PLOT_CORE_SOURCES
    # Core abstractions
    src/core/asset_loader.cpp
    src/core/utf8_utils.cpp
    src/core/platform_paths.cpp
    src/core/layout_calculator.cpp

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
        src/core/font_renderer_stub.cpp
        src/core/text_renderer_stub.cpp
    )
endif()

set(VNM_PLOT_CORE_HEADERS
    # Core headers
    include/vnm_plot/core/types.h
    include/vnm_plot/core/access_policy.h
    include/vnm_plot/core/plot_config.h
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
    include/vnm_plot/qt/plot_widget.h
    include/vnm_plot/qt/plot_interaction_item.h
    include/vnm_plot/qt/plot_time_axis.h
    include/vnm_plot/qt/qrhi_series_layer.h
    # Private internal header (lives next to its sources, not installed)
    src/qt/plot_renderer.h
)

# -----------------------------------------------------------------------------
# Generate embedded assets (shaders, fonts) for core library
# -----------------------------------------------------------------------------

set(VNM_PLOT_EMBEDDED_ASSETS_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_assets.cpp")

# Core assets
set(VNM_PLOT_CORE_ASSETS
)

# Text rendering assets (only when text is enabled)
if(VNM_PLOT_ENABLE_TEXT)
    list(APPEND VNM_PLOT_CORE_ASSETS
        "${VNM_MSDF_TEXT_DEFAULT_FONT_FILE}:fonts/monospace.ttf"
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
# Core Library target
# -----------------------------------------------------------------------------

add_library(vnm_plot_core STATIC
    ${VNM_PLOT_CORE_SOURCES}
    ${VNM_PLOT_CORE_HEADERS}
)

add_library(vnm_plot::core ALIAS vnm_plot_core)
set_target_properties(vnm_plot_core PROPERTIES EXPORT_NAME core)

target_include_directories(vnm_plot_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
)

target_compile_features(vnm_plot_core PUBLIC cxx_std_20)

# Link dependencies
target_link_libraries(vnm_plot_core
    PUBLIC
        glm::glm
)

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
            vnm_msdf_text::vnm_msdf_text
    )
endif()

# Compiler warnings for core
if(MSVC)
    target_compile_options(vnm_plot_core PRIVATE /W4)
else()
    target_compile_options(vnm_plot_core PRIVATE -Wall -Wextra)
endif()

message(STATUS "vnm_plot: Building QRhi core library")

# Renderer .cpp files use QRhi, whose public-by-private headers live behind
# Qt6::GuiPrivate. Keep it PRIVATE so consumers do not inherit Qt private
# include paths through the public interface.
target_link_libraries(vnm_plot_core PRIVATE Qt6::GuiPrivate Qt6::Gui)

# -----------------------------------------------------------------------------
# Qt Resources (only if building Qt wrapper)
# -----------------------------------------------------------------------------

qt6_add_resources(VNM_PLOT_QRC_SOURCES vnm_plot.qrc)

# -----------------------------------------------------------------------------
# QSB shader pipeline (Qt RHI)
#
# Bake every shader in shaders/qsb/ into a multi-profile .qsb artifact and
# embed it under :/vnm_plot/shaders/qsb/<name>.qsb. Profile set covers Vulkan
# (SPIR-V), Metal 1.2 + 2.1, D3D11 (HLSL 5.0), and desktop GL 4.10 so RHI can
# pick a working artifact on every supported backend.
# -----------------------------------------------------------------------------

set(VNM_PLOT_QSB_SHADERS
    shaders/qsb/generic_rect.vert
    shaders/qsb/generic_rect.frag
    shaders/qsb/grid_quad.vert
    shaders/qsb/grid_quad.frag
    shaders/qsb/plot_line.vert
    shaders/qsb/plot_line.frag
    shaders/qsb/plot_dot_quad.vert
    shaders/qsb/plot_dot_quad.frag
    shaders/qsb/plot_area.vert
    shaders/qsb/plot_area.frag
)

if(VNM_PLOT_ENABLE_TEXT)
    list(APPEND VNM_PLOT_QSB_SHADERS
        shaders/qsb/msdf_text.vert
        shaders/qsb/msdf_text.frag
    )
endif()

# -----------------------------------------------------------------------------
# Full Library target (Qt-dependent)
# -----------------------------------------------------------------------------

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
)

target_compile_features(vnm_plot PUBLIC cxx_std_20)

target_compile_definitions(vnm_plot PUBLIC VNM_PLOT_WITH_QT=1)

# Link dependencies. Qt6::GuiPrivate exposes the rhi/qrhi.h public-by-private
# header tree that the QQuickRhiItem renderer uses inside plot_renderer.cpp;
# keep it PRIVATE so consumers do not inherit Qt's private headers.
target_link_libraries(vnm_plot
    PUBLIC
        vnm_plot_core
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        glm::glm
    PRIVATE
        Qt6::GuiPrivate
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(vnm_plot PRIVATE NOMINMAX)
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(vnm_plot PRIVATE /W4)
else()
    target_compile_options(vnm_plot PRIVATE -Wall -Wextra)
endif()

# Each input shader yields :/vnm_plot/shaders/qsb/<name>.<ext>.qsb.
# GLSL profile is desktop 410 only; mobile GL ES is out of scope for
# The RHI shaders use SSBOs that are unavailable below ES 3.1,
# so emitting a 300 ES artifact would compile-fail at runtime on a real
# ES 3.0 driver.
qt_add_shaders(vnm_plot "vnm_plot_qsb_shaders"
    PREFIX "/vnm_plot"
    BASE   "${CMAKE_CURRENT_SOURCE_DIR}"
    GLSL   "410"
    HLSL   "50"
    MSL    "12,21"
    FILES  ${VNM_PLOT_QSB_SHADERS}
)

message(STATUS "vnm_plot: Building Qt wrapper library")

# -----------------------------------------------------------------------------
# Examples (require Qt)
# -----------------------------------------------------------------------------

if(VNM_PLOT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# -----------------------------------------------------------------------------
# Benchmark (requires Qt for window management)
# -----------------------------------------------------------------------------

option(VNM_PLOT_BUILD_BENCHMARK "Build benchmark executable" OFF)

if(VNM_PLOT_BUILD_BENCHMARK)
    add_subdirectory(benchmark)
endif()

# -----------------------------------------------------------------------------
# Tests (headless/unit-style)
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

    set(_vnm_plot_install_targets vnm_plot_core vnm_plot)

    # install(EXPORT) needs every link dependency to be IMPORTED (or in a
    # peer export set). glm and vnm_msdf_text only satisfy that when
    # VNM_PLOT_USE_SYSTEM_LIBS=ON brought them in via find_package; the
    # FetchContent path produces locally-built targets which can't be
    # exported here.
    set(_vnm_plot_export_ok TRUE)
    foreach(_dep IN ITEMS glm::glm)
        if(TARGET ${_dep})
            get_target_property(_dep_imported ${_dep} IMPORTED)
            if(NOT _dep_imported)
                set(_vnm_plot_export_ok FALSE)
            endif()
        endif()
    endforeach()
    if(VNM_PLOT_ENABLE_TEXT AND TARGET vnm_msdf_text::vnm_msdf_text)
        get_target_property(_dep_imported vnm_msdf_text::vnm_msdf_text IMPORTED)
        if(NOT _dep_imported)
            set(_vnm_plot_export_ok FALSE)
        endif()
    endif()

    if(_vnm_plot_export_ok)
        install(TARGETS ${_vnm_plot_install_targets}
            EXPORT vnm_plotTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )

        install(EXPORT vnm_plotTargets
            NAMESPACE vnm_plot::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vnm_plot
        )

        include(CMakePackageConfigHelpers)

        write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/vnm_plot-config-version.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion
        )

        set(VNM_PLOT_CONFIG_ENABLE_TEXT ${VNM_PLOT_ENABLE_TEXT})

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
    else()
        install(TARGETS ${_vnm_plot_install_targets}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        message(STATUS
            "vnm_plot: Skipping find_package export because glm or vnm_msdf_text "
            "is built locally via FetchContent. Configure with "
            "-DVNM_PLOT_USE_SYSTEM_LIBS=ON (and have those packages "
            "find_package-able) to install a find_package-ready vnm_plot.")
    endif()
endif()
