#include "atomic_file_write.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vnm::plot::detail {

namespace {

// Distinguishes the temporaries of concurrent writers, including writers in
// other processes that publish the same destination.
std::string writer_tag()
{
    static const std::uint64_t process_tag = [] {
        std::random_device source;
        return
            (static_cast<std::uint64_t>(source()) << 32) ^
            static_cast<std::uint64_t>(source());
    }();
    static std::atomic<std::uint64_t> s_sequence{0};

    std::ostringstream tag;
    tag << std::hex << process_tag
        << '-' << s_sequence.fetch_add(1, std::memory_order_relaxed);
    return tag.str();
}

std::filesystem::path temporary_path_for(const std::filesystem::path& path)
{
    // Only the file name is extended, so the temporary lands in the
    // destination's own directory and the replacement below stays within one
    // filesystem and therefore stays atomic.
    std::filesystem::path temporary = path;
    temporary += ".tmp-" + writer_tag();
    return temporary;
}

bool replace_destination(
    const std::filesystem::path&   temporary,
    const std::filesystem::path&   path)
{
#if defined(_WIN32)
    // std::filesystem::rename over an existing file is not uniformly a
    // replacing rename on Windows, so ask Win32 for that behaviour directly.
    //
    // Two writers replacing the same destination contend on it: while one
    // rename retires the old file the other sees it as delete-pending and
    // fails with ERROR_ACCESS_DENIED. Measured on this workload, 8 threads
    // replacing one destination failed 162 of 400 times without a retry and 0
    // of 400 with one, needing at most 8 attempts. The destination is whole at
    // every instant either way; the retry only keeps a contending writer from
    // reporting a failure that was never its own.
    constexpr auto k_contention_budget = std::chrono::milliseconds(250);
    const auto     deadline            = std::chrono::steady_clock::now() + k_contention_budget;
    for (;;) {
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0) {
            return true;
        }
        const DWORD error = GetLastError();
        if ((error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION) ||
            std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
#else
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    return !ec;
#endif
}

} // anonymous namespace

bool write_file_atomically(
    const std::filesystem::path&                path,
    const std::function<bool(std::ostream&)>&   write_body)
{
    const std::filesystem::path temporary = temporary_path_for(path);

    // The temporary exists only to become the destination, so every exit that
    // does not rename it removes it. The guard is declared before the stream so
    // the stream is closed first, which is what lets Windows delete the file,
    // and it covers the one exit a return statement cannot: write_body throwing.
    struct Temporary_file_guard
    {
        const std::filesystem::path&   file;
        bool                           published = false;

        ~Temporary_file_guard()
        {
            if (published) {
                return;
            }
            std::error_code ec;
            std::filesystem::remove(file, ec);
        }
    } guard{temporary};

    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    // Stream error state is sticky and a failed write turns every later one
    // into a no-op, so inspecting it after the body, the flush and the close
    // covers every individual write.
    bool complete = write_body(out);
    out.flush();
    complete = complete && out.good();
    out.close();
    complete = complete && out.good();

    if (complete && replace_destination(temporary, path)) {
        guard.published = true;
        return true;
    }

    return false;
}

} // namespace vnm::plot::detail
