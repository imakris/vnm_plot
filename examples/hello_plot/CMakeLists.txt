set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(vnm_plot_hello
    src/main.cpp
    src/plot_controller.cpp
    src/plot_controller.h
    resources.qrc
)

target_include_directories(vnm_plot_hello PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(vnm_plot_hello PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::OpenGL
    vnm_plot::vnm_plot
)
