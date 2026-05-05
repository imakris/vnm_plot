cmake_minimum_required(VERSION 3.16)

# Test executables
set(_vnm_plot_core_tests
    test_cache_invalidation
    test_snapshot_caching
    test_typed_api
    test_core_algo
    test_asset_loader
    test_concurrent_series
    test_gpu_sample_origin
    test_layout_calculator
)

foreach(_test IN LISTS _vnm_plot_core_tests)
    add_executable(${_test} ${_test}.cpp)
    target_link_libraries(${_test} PRIVATE vnm_plot_core)
    set_target_properties(${_test} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
endforeach()

find_package(Threads QUIET)
if(TARGET Threads::Threads)
    target_link_libraries(test_concurrent_series PRIVATE Threads::Threads)
endif()

if(TARGET vnm_plot)
    add_executable(test_plot_interaction_item test_plot_interaction_item.cpp)
    target_link_libraries(test_plot_interaction_item PRIVATE vnm_plot)
endif()

if(TARGET vnm_plot)
    set_target_properties(test_plot_interaction_item PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
endif()

add_test(NAME CacheInvalidation COMMAND test_cache_invalidation)
add_test(NAME SnapshotCaching COMMAND test_snapshot_caching)
add_test(NAME TypedApi COMMAND test_typed_api)
add_test(NAME CoreAlgo COMMAND test_core_algo)
add_test(NAME AssetLoader COMMAND test_asset_loader)
add_test(NAME ConcurrentSeries COMMAND test_concurrent_series)
add_test(NAME GpuSampleOrigin COMMAND test_gpu_sample_origin)
add_test(NAME LayoutCalculator COMMAND test_layout_calculator)

if(TARGET vnm_plot)
    add_test(NAME PlotInteractionItem COMMAND test_plot_interaction_item)
endif()

# Print test info
message(STATUS "Configured tests:")
foreach(_test IN LISTS _vnm_plot_core_tests)
    message(STATUS "  - ${_test}")
endforeach()
if(TARGET vnm_plot)
    message(STATUS "  - test_plot_interaction_item")
endif()
