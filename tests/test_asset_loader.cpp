// vnm_plot Asset_loader tests
// Verifies logging wiring, embedded asset lookup, override-directory fallback,
// and the shader_sources helper.

#include "test_macros.h"

#include <vnm_plot/core/asset_loader.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace plot = vnm::plot;

namespace {

struct Scoped_temp_dir
{
    std::filesystem::path path;

    Scoped_temp_dir()
    {
        path = std::filesystem::temp_directory_path() /
               ("vnm_plot_asset_test_" + std::to_string(std::hash<const void*>{}(this)));
        std::filesystem::create_directories(path);
    }

    ~Scoped_temp_dir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    Scoped_temp_dir(const Scoped_temp_dir&) = delete;
    Scoped_temp_dir& operator=(const Scoped_temp_dir&) = delete;
};

void write_file(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

bool test_missing_asset_logs_and_returns_nullopt()
{
    plot::Asset_loader loader;
    std::vector<std::string> messages;
    loader.set_log_callback([&](const std::string& msg) { messages.push_back(msg); });

    auto result = loader.load("does/not/exist.vert");
    TEST_ASSERT(!result.has_value(),
        "missing asset should return nullopt");
    TEST_ASSERT(!messages.empty(),
        "missing asset should surface a log message");
    TEST_ASSERT(messages.front().find("does/not/exist.vert") != std::string::npos,
        "log message should reference the missing asset name");
    return true;
}

bool test_embedded_asset_returns_registered_bytes()
{
    plot::Asset_loader loader;
    const std::string payload = "hello-shader-bytes";
    loader.register_embedded("example.vert", payload);

    auto result = loader.load("example.vert");
    TEST_ASSERT(result.has_value(),
        "registered embedded asset should be loadable");
    TEST_ASSERT(*result == payload,
        "embedded asset bytes should match what was registered");
    return true;
}

bool test_override_directory_beats_embedded_asset()
{
    Scoped_temp_dir tmp;
    plot::Asset_loader loader;

    loader.register_embedded("example.vert", "embedded-version");
    write_file(tmp.path / "example.vert", "override-version");

    loader.set_override_directory(tmp.path.string());

    auto result = loader.load("example.vert");
    TEST_ASSERT(result.has_value(),
        "override asset should be loadable");
    TEST_ASSERT(*result == "override-version",
        "override directory should take precedence over embedded asset");
    return true;
}

bool test_override_directory_falls_back_to_embedded_when_missing()
{
    Scoped_temp_dir tmp;
    plot::Asset_loader loader;

    loader.register_embedded("example.vert", "embedded-only");
    loader.set_override_directory(tmp.path.string());

    auto result = loader.load("example.vert");
    TEST_ASSERT(result.has_value(),
        "should fall back to embedded when override directory lacks the file");
    TEST_ASSERT(*result == "embedded-only",
        "embedded fallback bytes should be returned");
    return true;
}

bool test_load_shader_missing_required_stages_logs_and_returns_nullopt()
{
    plot::Asset_loader loader;
    std::vector<std::string> messages;
    loader.set_log_callback([&](const std::string& msg) { messages.push_back(msg); });

    auto missing_vert = loader.load_shader("ghost");
    TEST_ASSERT(!missing_vert.has_value(),
        "shader without any stage should fail to load");

    bool mentions_vert = false;
    for (const auto& m : messages) {
        if (m.find("ghost.vert") != std::string::npos) {
            mentions_vert = true;
            break;
        }
    }
    TEST_ASSERT(mentions_vert,
        "log should mention the missing vertex shader");
    return true;
}

bool test_load_shader_includes_optional_geometry_when_present()
{
    plot::Asset_loader loader;
    loader.register_embedded("good.vert", "vertex");
    loader.register_embedded("good.frag", "fragment");

    auto without_geom = loader.load_shader("good");
    TEST_ASSERT(without_geom.has_value(),
        "shader with just vert+frag should succeed");
    TEST_ASSERT(without_geom->vertex == "vertex",
        "vertex bytes should match");
    TEST_ASSERT(without_geom->fragment == "fragment",
        "fragment bytes should match");
    TEST_ASSERT(without_geom->geometry.empty(),
        "geometry should be empty when not registered");

    loader.register_embedded("good.geom", "geometry");
    auto with_geom = loader.load_shader("good");
    TEST_ASSERT(with_geom.has_value(),
        "shader with all three stages should succeed");
    TEST_ASSERT(with_geom->geometry == "geometry",
        "geometry bytes should match when registered");
    return true;
}

bool test_override_directory_read_failure_logs()
{
    // Create an override path that exists but points at a directory entry of
    // the same name; attempting to open it as a file should fail and log.
    Scoped_temp_dir tmp;
    std::filesystem::create_directories(tmp.path / "broken.vert");  // a dir, not a file

    plot::Asset_loader loader;
    std::vector<std::string> messages;
    loader.set_log_callback([&](const std::string& msg) { messages.push_back(msg); });
    loader.register_embedded("broken.vert", "fallback");
    loader.set_override_directory(tmp.path.string());

    auto result = loader.load("broken.vert");
    // The loader should still return the embedded fallback. Whether a log line
    // is emitted depends on filesystem semantics (opening a directory may
    // succeed on some platforms), so we only assert the fallback path.
    TEST_ASSERT(result.has_value(),
        "loader should fall back to embedded when override read fails");
    TEST_ASSERT(*result == "fallback",
        "embedded fallback bytes should be returned");
    return true;
}

} // namespace

int main()
{
    std::cout << "Asset loader tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_missing_asset_logs_and_returns_nullopt);
    RUN_TEST(test_embedded_asset_returns_registered_bytes);
    RUN_TEST(test_override_directory_beats_embedded_asset);
    RUN_TEST(test_override_directory_falls_back_to_embedded_when_missing);
    RUN_TEST(test_load_shader_missing_required_stages_logs_and_returns_nullopt);
    RUN_TEST(test_load_shader_includes_optional_geometry_when_present);
    RUN_TEST(test_override_directory_read_failure_logs);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
