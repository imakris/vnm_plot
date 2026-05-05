add_subdirectory(hello_plot)
add_subdirectory(preview_config)

if(VNM_PLOT_BUILD_FUNCTION_PLOTTER)
    add_subdirectory(function_plotter)
endif()
