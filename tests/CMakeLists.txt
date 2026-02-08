cmake_minimum_required(VERSION 3.16)

# Test executables
add_executable(test_cache_invalidation test_cache_invalidation.cpp)
add_executable(test_snapshot_caching test_snapshot_caching.cpp)
add_executable(test_typed_api test_typed_api.cpp)

target_link_libraries(test_cache_invalidation PRIVATE vnm_plot_core)
target_link_libraries(test_snapshot_caching PRIVATE vnm_plot_core)
target_link_libraries(test_typed_api PRIVATE vnm_plot_core)

# Set C++17 standard
set_target_properties(test_cache_invalidation PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

set_target_properties(test_snapshot_caching PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

set_target_properties(test_typed_api PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Add tests
add_test(NAME CacheInvalidation COMMAND test_cache_invalidation)
add_test(NAME SnapshotCaching COMMAND test_snapshot_caching)
add_test(NAME TypedApi COMMAND test_typed_api)

# Print test info
message(STATUS "Configured tests:")
message(STATUS "  - test_cache_invalidation")
message(STATUS "  - test_snapshot_caching")
message(STATUS "  - test_typed_api")
