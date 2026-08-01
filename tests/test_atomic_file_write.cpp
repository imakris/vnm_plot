// vnm_plot atomic file publication tests

#include "test_macros.h"

#include "../src/core/atomic_file_write.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace detail = vnm::plot::detail;

namespace {

struct Scoped_temp_dir
{
    std::filesystem::path path;

    Scoped_temp_dir()
    {
        path = std::filesystem::temp_directory_path() /
               ("vnm_plot_atomic_write_test_" +
                std::to_string(std::hash<const void*>{}(this)));
        std::filesystem::create_directories(path);
    }

    ~Scoped_temp_dir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    Scoped_temp_dir(const Scoped_temp_dir&)            = delete;
    Scoped_temp_dir& operator=(const Scoped_temp_dir&) = delete;
};

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

std::size_t entry_count(const std::filesystem::path& directory)
{
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        (void)entry;
        ++count;
    }
    return count;
}

bool write_text(std::ostream& out, const std::string& text)
{
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return true;
}

bool test_published_file_holds_the_written_payload()
{
    Scoped_temp_dir tmp;
    const auto      path = tmp.path / "published.bin";

    TEST_ASSERT(detail::write_file_atomically(
            path,
            [](std::ostream& out) { return write_text(out, "payload"); }),
        "a complete write should publish the destination");
    TEST_ASSERT(read_file(path) == "payload", "the destination should hold the written payload");
    TEST_ASSERT(entry_count(tmp.path) == 1u, "publication should leave no temporary behind");
    return true;
}

bool test_interrupted_body_leaves_the_destination_untouched()
{
    Scoped_temp_dir tmp;
    const auto      path = tmp.path / "interrupted.bin";

    TEST_ASSERT(detail::write_file_atomically(
            path,
            [](std::ostream& out) { return write_text(out, "original"); }),
        "the first write should publish the destination");

    TEST_ASSERT(!detail::write_file_atomically(
            path,
            [](std::ostream& out) {
                write_text(out, "half");
                return false;
            }),
        "an interrupted body must report a failed publication");
    TEST_ASSERT(read_file(path) == "original",
        "an interrupted write must leave the previous contents in place");
    TEST_ASSERT(entry_count(tmp.path) == 1u,
        "an interrupted write must not leave a temporary behind");
    return true;
}

bool test_failed_stream_write_is_not_published()
{
    Scoped_temp_dir tmp;
    const auto      path = tmp.path / "stream_failure.bin";

    TEST_ASSERT(!detail::write_file_atomically(
            path,
            [](std::ostream& out) {
                write_text(out, "start");
                // Whatever makes a write fail - a full disk, a lost handle -
                // reaches the stream as an error bit, and the body itself can
                // stay unaware of it.
                out.setstate(std::ios::badbit);
                return true;
            }),
        "a failed stream write must report a failed publication");
    TEST_ASSERT(!std::filesystem::exists(path),
        "a failed stream write must not create the destination");
    TEST_ASSERT(entry_count(tmp.path) == 0u,
        "a failed stream write must not leave a temporary behind");
    return true;
}

bool test_concurrent_writers_publish_one_whole_file()
{
    Scoped_temp_dir tmp;
    const auto      path = tmp.path / "contended.bin";

    constexpr std::size_t k_writer_count  = 8;
    constexpr std::size_t k_payload_bytes = 64 * 1024;

    std::vector<std::thread>   writers;
    std::atomic<std::size_t>   published{0};
    writers.reserve(k_writer_count);
    for (std::size_t i = 0; i < k_writer_count; ++i) {
        writers.emplace_back([&, i] {
            const std::string payload(k_payload_bytes, static_cast<char>('A' + i));
            if (detail::write_file_atomically(
                    path,
                    [&payload](std::ostream& out) { return write_text(out, payload); }))
            {
                published.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }

    TEST_ASSERT(published.load(std::memory_order_relaxed) == k_writer_count,
        "every writer of a valid payload should publish successfully");

    const std::string contents = read_file(path);
    TEST_ASSERT(contents.size() == k_payload_bytes,
        "the published file must be exactly one writer's payload");
    TEST_ASSERT(contents == std::string(k_payload_bytes, contents.front()),
        "the published file must not mix the payloads of two writers");
    TEST_ASSERT(entry_count(tmp.path) == 1u,
        "losing writers must not leave temporaries behind");
    return true;
}

} // namespace

int main()
{
    std::cout << "Atomic file publication tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_published_file_holds_the_written_payload);
    RUN_TEST(test_interrupted_body_leaves_the_destination_untouched);
    RUN_TEST(test_failed_stream_write_is_not_published);
    RUN_TEST(test_concurrent_writers_publish_one_whole_file);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
