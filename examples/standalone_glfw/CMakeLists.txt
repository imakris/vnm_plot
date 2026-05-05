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
# Keep full history for fetched libraries because this workspace patches and
# reviews them directly instead of treating them as disposable third-party code.
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.9
    GIT_SHALLOW    FALSE
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

# Deploy Qt runtime DLLs next to the example so it can be launched by
# double-clicking from Explorer (no manual PATH or vcvarsall shell needed).
if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
    get_target_property(_qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET standalone_glfw POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    "$<TARGET_FILE:standalone_glfw>"
            COMMENT "Deploying Qt runtime for standalone_glfw"
            VERBATIM
        )
    else()
        message(WARNING
            "windeployqt not found near Qt6::qmake (${_qt_bin_dir}); "
            "standalone_glfw.exe will not have Qt runtime DLLs alongside "
            "it and will fail to launch from Explorer.")
    endif()
endif()
