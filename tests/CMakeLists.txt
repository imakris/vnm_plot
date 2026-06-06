cmake_minimum_required(VERSION 3.16)

# Test executables
set(_vnm_plot_core_tests
    test_cache_invalidation
    test_snapshot_caching
    test_typed_api
    test_function_sample_source
    test_core_algo
    test_asset_loader
    test_concurrent_series
    test_gpu_sample_origin
    test_layout_calculator
    test_rhi_helpers
    test_qrhi_layer_lifecycle
    test_qrhi_public_api
)

foreach(_test IN LISTS _vnm_plot_core_tests)
    add_executable(${_test} ${_test}.cpp)
    target_link_libraries(${_test} PRIVATE vnm_plot_core)
    set_target_properties(${_test} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
endforeach()

target_include_directories(test_function_sample_source PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../examples/common
)

find_package(Threads QUIET)
if(TARGET Threads::Threads)
    target_link_libraries(test_concurrent_series PRIVATE Threads::Threads)
endif()

target_link_libraries(test_qrhi_layer_lifecycle PRIVATE Qt6::GuiPrivate Qt6::Gui)
target_link_libraries(test_qrhi_public_api PRIVATE Qt6::GuiPrivate Qt6::Gui)
target_link_libraries(test_rhi_helpers PRIVATE Qt6::GuiPrivate Qt6::Gui)

if(TARGET vnm_plot)
    add_executable(test_plot_interaction_item test_plot_interaction_item.cpp)
    target_link_libraries(test_plot_interaction_item PRIVATE vnm_plot)

    add_executable(test_plot_time_axis test_plot_time_axis.cpp)
    target_link_libraries(test_plot_time_axis PRIVATE vnm_plot)
endif()

if(TARGET vnm_plot)
    set_target_properties(test_plot_interaction_item test_plot_time_axis PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
endif()

set(_vnm_plot_test_names)

function(vnm_plot_add_test test_name target_name)
    add_test(NAME ${test_name} COMMAND ${target_name})
    list(APPEND _vnm_plot_test_names ${test_name})
    set(_vnm_plot_test_names "${_vnm_plot_test_names}" PARENT_SCOPE)
endfunction()

vnm_plot_add_test(CacheInvalidation test_cache_invalidation)
vnm_plot_add_test(SnapshotCaching test_snapshot_caching)
vnm_plot_add_test(TypedApi test_typed_api)
vnm_plot_add_test(FunctionSampleSource test_function_sample_source)
vnm_plot_add_test(CoreAlgo test_core_algo)
vnm_plot_add_test(AssetLoader test_asset_loader)
vnm_plot_add_test(ConcurrentSeries test_concurrent_series)
vnm_plot_add_test(GpuSampleOrigin test_gpu_sample_origin)
vnm_plot_add_test(LayoutCalculator test_layout_calculator)
vnm_plot_add_test(RhiHelpers test_rhi_helpers)
vnm_plot_add_test(QrhiLayerLifecycle test_qrhi_layer_lifecycle)
vnm_plot_add_test(QrhiPublicApi test_qrhi_public_api)

if(TARGET vnm_plot)
    vnm_plot_add_test(PlotInteractionItem test_plot_interaction_item)
    vnm_plot_add_test(PlotTimeAxis test_plot_time_axis)
endif()

if(WIN32)
    get_target_property(_vnm_plot_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_vnm_plot_qt_bin_dir "${_vnm_plot_qmake_executable}" DIRECTORY)
    get_filename_component(_vnm_plot_compiler_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)

    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        set_tests_properties(${_vnm_plot_test_names} PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${_vnm_plot_compiler_bin_dir};PATH=path_list_prepend:${_vnm_plot_qt_bin_dir}"
        )
    else()
        set(_vnm_plot_test_path "$ENV{PATH}")
        set(_vnm_plot_test_path "${_vnm_plot_qt_bin_dir};${_vnm_plot_compiler_bin_dir};${_vnm_plot_test_path}")
        string(REPLACE ";" "\\;" _vnm_plot_test_path "${_vnm_plot_test_path}")
        set_tests_properties(${_vnm_plot_test_names} PROPERTIES
            ENVIRONMENT "PATH=${_vnm_plot_test_path}"
        )
    endif()

    set(_vnm_plot_qt_runtime_targets Qt6::Core Qt6::Gui)
    foreach(_qt_target IN ITEMS
        Qt6::Quick
        Qt6::Qml
        Qt6::QmlModels
        Qt6::Network
        Qt6::OpenGL
        Qt6::QmlWorkerScript
    )
        if(TARGET ${_qt_target})
            list(APPEND _vnm_plot_qt_runtime_targets ${_qt_target})
        endif()
    endforeach()

    set(_vnm_plot_windows_test_targets ${_vnm_plot_core_tests})
    if(TARGET test_plot_interaction_item)
        list(APPEND _vnm_plot_windows_test_targets test_plot_interaction_item)
    endif()
    if(TARGET test_plot_time_axis)
        list(APPEND _vnm_plot_windows_test_targets test_plot_time_axis)
    endif()

    set(_vnm_plot_windows_test_runtime_dir "${CMAKE_CURRENT_BINARY_DIR}")
    set(_vnm_plot_windows_test_runtime_commands
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${_vnm_plot_windows_test_runtime_dir}"
    )

    foreach(_qt_target IN LISTS _vnm_plot_qt_runtime_targets)
        list(APPEND _vnm_plot_windows_test_runtime_commands
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${_qt_target}>"
                "${_vnm_plot_windows_test_runtime_dir}"
        )
    endforeach()

    if(EXISTS "${_vnm_plot_qt_bin_dir}/../plugins/platforms/qwindows.dll")
        list(APPEND _vnm_plot_windows_test_runtime_commands
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${_vnm_plot_windows_test_runtime_dir}/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_vnm_plot_qt_bin_dir}/../plugins/platforms/qwindows.dll"
                "${_vnm_plot_windows_test_runtime_dir}/platforms"
        )
    endif()

    foreach(_qt_plugin_dir IN ITEMS
        iconengines
        imageformats
        styles
        tls
    )
        if(EXISTS "${_vnm_plot_qt_bin_dir}/../plugins/${_qt_plugin_dir}")
            list(APPEND _vnm_plot_windows_test_runtime_commands
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_vnm_plot_qt_bin_dir}/../plugins/${_qt_plugin_dir}"
                    "${_vnm_plot_windows_test_runtime_dir}/${_qt_plugin_dir}"
            )
        endif()
    endforeach()

    foreach(_runtime_dll IN ITEMS
        libgcc_s_seh-1.dll
        libstdc++-6.dll
        libwinpthread-1.dll
    )
        if(EXISTS "${_vnm_plot_compiler_bin_dir}/${_runtime_dll}")
            list(APPEND _vnm_plot_windows_test_runtime_commands
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_vnm_plot_compiler_bin_dir}/${_runtime_dll}"
                    "${_vnm_plot_windows_test_runtime_dir}"
            )
        endif()
    endforeach()

    add_custom_target(vnm_plot_deploy_windows_test_runtime
        ${_vnm_plot_windows_test_runtime_commands}
        VERBATIM
    )

    foreach(_test IN LISTS _vnm_plot_windows_test_targets)
        add_dependencies(${_test} vnm_plot_deploy_windows_test_runtime)
    endforeach()
endif()

# Print test info
message(STATUS "Configured tests:")
foreach(_test IN LISTS _vnm_plot_core_tests)
    message(STATUS "  - ${_test}")
endforeach()
if(TARGET vnm_plot)
    message(STATUS "  - test_plot_interaction_item")
    message(STATUS "  - test_plot_time_axis")
endif()
