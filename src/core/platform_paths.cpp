#include "platform_paths.h"

#include <cstdlib>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

namespace vnm::plot {

namespace {

constexpr const char* s_app_name = "vnm_plot";

#ifdef _WIN32

std::filesystem::path get_known_folder(int folder_id)
{
    wchar_t* path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(
            folder_id == CSIDL_LOCAL_APPDATA ? FOLDERID_LocalAppData : FOLDERID_RoamingAppData,
            0, nullptr, &path))) {
        std::filesystem::path result(path);
        CoTaskMemFree(path);
        return result;
    }
    return {};
}

#else // macOS / Linux / Unix

std::filesystem::path get_home_directory()
{
    const char* home = std::getenv("HOME");
    return home ? std::filesystem::path(home) : std::filesystem::path();
}

#if !defined(__APPLE__)
std::filesystem::path get_xdg_path(const char* env_var, const char* default_subpath)
{
    const char* xdg_path = std::getenv(env_var);
    if (xdg_path && xdg_path[0] != '\0') {
        return std::filesystem::path(xdg_path);
    }
    auto home = get_home_directory();
    return home.empty() ? std::filesystem::path() : home / default_subpath;
}
#endif

#endif // _WIN32

std::filesystem::path ensure_directory(const std::filesystem::path& dir)
{
    if (dir.empty()) {
        return {};
    }

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        if (!std::filesystem::create_directories(dir, ec)) {
            return {};
        }
    }

    return dir;
}

} // anonymous namespace

std::filesystem::path get_cache_directory()
{
#ifdef _WIN32
    auto base = get_known_folder(CSIDL_LOCAL_APPDATA);
    if (base.empty()) {
        return {};
    }
    return ensure_directory(base / s_app_name / "cache");

#elif defined(__APPLE__)
    auto home = get_home_directory();
    if (home.empty()) {
        return {};
    }
    return ensure_directory(home / "Library" / "Caches" / s_app_name);

#else // Linux/Unix
    auto base = get_xdg_path("XDG_CACHE_HOME", ".cache");
    if (base.empty()) {
        return {};
    }
    return ensure_directory(base / s_app_name);
#endif
}

std::filesystem::path get_data_directory()
{
#ifdef _WIN32
    auto base = get_known_folder(CSIDL_LOCAL_APPDATA);
    if (base.empty()) {
        return {};
    }
    return ensure_directory(base / s_app_name);

#elif defined(__APPLE__)
    auto home = get_home_directory();
    if (home.empty()) {
        return {};
    }
    return ensure_directory(home / "Library" / "Application Support" / s_app_name);

#else // Linux/Unix
    auto base = get_xdg_path("XDG_DATA_HOME", ".local/share");
    if (base.empty()) {
        return {};
    }
    return ensure_directory(base / s_app_name);
#endif
}

} // namespace vnm::plot
