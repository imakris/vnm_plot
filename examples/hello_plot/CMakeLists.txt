cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(vnm_plot_hello
        VERSION 0.1.0
        DESCRIPTION "Minimal vnm_plot example"
        LANGUAGES CXX
    )
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)

if(NOT TARGET vnm_plot::qtquick)
    set(VNM_PLOT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.."
                     "${CMAKE_CURRENT_BINARY_DIR}/vnm_plot")
endif()

add_executable(vnm_plot_hello
    src/main.cpp
    src/plot_controller.cpp
    src/plot_controller.h
    resources.qrc
)

target_include_directories(vnm_plot_hello PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(vnm_plot_hello PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    vnm_plot::qtquick
)

# Deploy Qt runtime DLLs next to the example so it can be launched by
# double-clicking from Explorer (no manual PATH or vcvarsall shell needed).
if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
    get_target_property(_qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET vnm_plot_hello POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                    "$<TARGET_FILE:vnm_plot_hello>"
            COMMENT "Deploying Qt runtime for vnm_plot_hello"
            VERBATIM
        )
    else()
        message(WARNING
            "windeployqt not found near Qt6::qmake (${_qt_bin_dir}); "
            "vnm_plot_hello.exe will not have Qt runtime DLLs alongside it "
            "and will fail to launch from Explorer.")
    endif()
endif()
