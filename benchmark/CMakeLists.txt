# ==============================================================================
# vnm_plot Benchmark
# Synthetic benchmark for vnm_plot rendering performance
# ==============================================================================
# Note: This file is included as a subdirectory from the root CMakeLists.txt
# and inherits cmake_minimum_required and project settings from parent.

# Find Qt6 (required for window management and OpenGL context)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

# Find vnm_plot_core (should be available from parent)
if(NOT TARGET vnm_plot::core)
    message(FATAL_ERROR "vnm_plot_core target not found. Build benchmark from vnm_plot root.")
endif()

# Benchmark should include text rendering by default.
if(NOT VNM_PLOT_ENABLE_TEXT)
    message(FATAL_ERROR "vnm_plot benchmark requires text rendering. Configure with -DVNM_PLOT_ENABLE_TEXT=ON or disable the benchmark.")
endif()

# -----------------------------------------------------------------------------
# Benchmark executable
# -----------------------------------------------------------------------------

set(BENCHMARK_SOURCES
    src/main.cpp
    src/benchmark_window.cpp
    src/benchmark_frame.cpp
)

set(BENCHMARK_HEADERS
    include/ring_buffer.h
    include/sample_types.h
    include/brownian_generator.h
    include/benchmark_profiler.h
    include/benchmark_data_source.h
    include/benchmark_window.h
    include/benchmark_frame.h
)

add_executable(vnm_plot_benchmark
    ${BENCHMARK_SOURCES}
    ${BENCHMARK_HEADERS}
)

# Enable Qt MOC for benchmark window
set_target_properties(vnm_plot_benchmark PROPERTIES
    AUTOMOC ON
)

target_include_directories(vnm_plot_benchmark
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(vnm_plot_benchmark PRIVATE cxx_std_20)

# Link dependencies
target_link_libraries(vnm_plot_benchmark
    PRIVATE
        vnm_plot::core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(vnm_plot_benchmark PRIVATE NOMINMAX)
    # Console application for benchmark output
    set_target_properties(vnm_plot_benchmark PROPERTIES
        WIN32_EXECUTABLE FALSE
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(vnm_plot_benchmark PRIVATE /W4)
else()
    target_compile_options(vnm_plot_benchmark PRIVATE -Wall -Wextra)
endif()

message(STATUS "vnm_plot: Building benchmark executable")

# -----------------------------------------------------------------------------
# Ring buffer test executable
# -----------------------------------------------------------------------------

add_executable(test_ring_buffer
    src/test_ring_buffer.cpp
    ${BENCHMARK_HEADERS}
)

target_include_directories(test_ring_buffer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(test_ring_buffer PRIVATE cxx_std_20)

if(WIN32)
    target_compile_definitions(test_ring_buffer PRIVATE NOMINMAX)
endif()

if(MSVC)
    target_compile_options(test_ring_buffer PRIVATE /W4)
else()
    target_compile_options(test_ring_buffer PRIVATE -Wall -Wextra)
endif()

# Register with CTest
include(CTest)
add_test(NAME ring_buffer_tests COMMAND test_ring_buffer)

message(STATUS "vnm_plot: Building ring buffer tests")

# -----------------------------------------------------------------------------
# Brownian generator test executable
# -----------------------------------------------------------------------------

add_executable(test_brownian_generator
    src/test_brownian_generator.cpp
    ${BENCHMARK_HEADERS}
)

target_include_directories(test_brownian_generator
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(test_brownian_generator PRIVATE cxx_std_20)

if(WIN32)
    target_compile_definitions(test_brownian_generator PRIVATE NOMINMAX)
endif()

if(MSVC)
    target_compile_options(test_brownian_generator PRIVATE /W4)
else()
    target_compile_options(test_brownian_generator PRIVATE -Wall -Wextra)
endif()

add_test(NAME brownian_generator_tests COMMAND test_brownian_generator)

message(STATUS "vnm_plot: Building Brownian generator tests")

# -----------------------------------------------------------------------------
# Benchmark profiler test executable
# -----------------------------------------------------------------------------

add_executable(test_benchmark_profiler
    src/test_benchmark_profiler.cpp
    ${BENCHMARK_HEADERS}
)

target_include_directories(test_benchmark_profiler
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(test_benchmark_profiler PRIVATE cxx_std_20)

# Link vnm_plot_core for Profiler interface
target_link_libraries(test_benchmark_profiler PRIVATE vnm_plot::core)

if(WIN32)
    target_compile_definitions(test_benchmark_profiler PRIVATE NOMINMAX)
endif()

if(MSVC)
    target_compile_options(test_benchmark_profiler PRIVATE /W4)
else()
    target_compile_options(test_benchmark_profiler PRIVATE -Wall -Wextra)
endif()

add_test(NAME benchmark_profiler_tests COMMAND test_benchmark_profiler)

message(STATUS "vnm_plot: Building benchmark profiler tests")

# -----------------------------------------------------------------------------
# Benchmark data source test executable
# -----------------------------------------------------------------------------

add_executable(test_benchmark_data_source
    src/test_benchmark_data_source.cpp
    ${BENCHMARK_HEADERS}
)

target_include_directories(test_benchmark_data_source
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(test_benchmark_data_source PRIVATE cxx_std_20)

# Link vnm_plot_core for Data_source interface
target_link_libraries(test_benchmark_data_source PRIVATE vnm_plot::core)

if(WIN32)
    target_compile_definitions(test_benchmark_data_source PRIVATE NOMINMAX)
endif()

if(MSVC)
    target_compile_options(test_benchmark_data_source PRIVATE /W4)
else()
    target_compile_options(test_benchmark_data_source PRIVATE -Wall -Wextra)
endif()

add_test(NAME benchmark_data_source_tests COMMAND test_benchmark_data_source)

message(STATUS "vnm_plot: Building benchmark data source tests")
