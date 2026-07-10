// vnm_plot Benchmark - Render-thread CPU allocation tracking

#include "allocation_tracker.h"

#include <cerrno>
#include <cstdlib>
#include <new>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace {

thread_local bool g_measure_allocations = false;
thread_local std::uint32_t g_allocation_suppression_depth = 0;
thread_local std::uint64_t g_allocation_count = 0;
thread_local std::uint64_t g_allocation_bytes = 0;
thread_local vnm::benchmark::Thread_allocation_failure g_last_allocation_failure;

void record_allocation(std::size_t size) noexcept
{
    if (g_measure_allocations && g_allocation_suppression_depth == 0) {
        ++g_allocation_count;
        g_allocation_bytes += static_cast<std::uint64_t>(size);
    }
}

void* allocate(std::size_t size)
{
    const std::size_t retained_size = size == 0 ? 1 : size;
    void* memory = std::malloc(retained_size);
    if (!memory) {
        g_last_allocation_failure = {retained_size, 0, 0, errno, false};
        throw std::bad_alloc();
    }
    record_allocation(retained_size);
    return memory;
}

void* allocate_aligned(std::size_t size, std::size_t alignment)
{
    const std::size_t retained_size = size == 0 ? 1 : size;
    const std::size_t effective_alignment = alignment < sizeof(void*)
        ? sizeof(void*)
        : alignment;
    void* memory = nullptr;
#if defined(_MSC_VER)
    memory = _aligned_malloc(retained_size, effective_alignment);
    const int allocation_error = memory ? 0 : errno;
#else
    const int allocation_error = posix_memalign(
        &memory,
        effective_alignment,
        retained_size);
    if (allocation_error != 0) {
        memory = nullptr;
    }
#endif
    if (!memory) {
        g_last_allocation_failure = {
            retained_size,
            alignment,
            effective_alignment,
            allocation_error,
            true,
        };
        throw std::bad_alloc();
    }
    record_allocation(retained_size);
    return memory;
}

void free_aligned(void* memory) noexcept
{
#if defined(_MSC_VER)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

}  // namespace

namespace vnm::benchmark {

void begin_thread_allocation_measurement() noexcept
{
    g_allocation_count = 0;
    g_allocation_bytes = 0;
    g_measure_allocations = true;
}

Thread_allocation_measurement end_thread_allocation_measurement() noexcept
{
    g_measure_allocations = false;
    return {g_allocation_count, g_allocation_bytes};
}

void suspend_thread_allocation_measurement() noexcept
{
    ++g_allocation_suppression_depth;
}

void resume_thread_allocation_measurement() noexcept
{
    if (g_allocation_suppression_depth > 0) {
        --g_allocation_suppression_depth;
    }
}

void clear_thread_allocation_failure() noexcept
{
    g_last_allocation_failure = {};
}

Thread_allocation_failure last_thread_allocation_failure() noexcept
{
    return g_last_allocation_failure;
}

}  // namespace vnm::benchmark

void* operator new(std::size_t size)
{
    return allocate(size);
}

void* operator new[](std::size_t size)
{
    return allocate(size);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try {
        return allocate(size);
    }
    catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try {
        return allocate(size);
    }
    catch (...) {
        return nullptr;
    }
}

void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* memory, std::align_val_t) noexcept
{
    free_aligned(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept
{
    free_aligned(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept
{
    free_aligned(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
    free_aligned(memory);
}

void* operator new(
    std::size_t size,
    std::align_val_t alignment,
    const std::nothrow_t&) noexcept
{
    try {
        return allocate_aligned(size, static_cast<std::size_t>(alignment));
    }
    catch (...) {
        return nullptr;
    }
}

void* operator new[](
    std::size_t size,
    std::align_val_t alignment,
    const std::nothrow_t&) noexcept
{
    try {
        return allocate_aligned(size, static_cast<std::size_t>(alignment));
    }
    catch (...) {
        return nullptr;
    }
}

void operator delete(
    void* memory,
    std::align_val_t,
    const std::nothrow_t&) noexcept
{
    free_aligned(memory);
}

void operator delete[](
    void* memory,
    std::align_val_t,
    const std::nothrow_t&) noexcept
{
    free_aligned(memory);
}
