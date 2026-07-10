// vnm_plot Benchmark - Native filesystem path adaptation

#ifndef VNM_PLOT_BENCHMARK_PATH_IO_H
#define VNM_PLOT_BENCHMARK_PATH_IO_H

#include <filesystem>
#include <string>

namespace vnm::benchmark {

inline std::filesystem::path path_for_file_io(
    const std::filesystem::path& logical_path)
{
#if defined(_WIN32)
    if (logical_path.empty()) {
        return logical_path;
    }

    const std::wstring& logical_native = logical_path.native();
    if (logical_native.starts_with(L"\\\\?\\") ||
        logical_native.starts_with(L"\\\\.\\")) {
        return logical_path;
    }

    const std::filesystem::path absolute_path = (
        logical_path.is_absolute()
            ? logical_path
            : std::filesystem::absolute(logical_path)).lexically_normal();
    const std::wstring& native = absolute_path.native();
    if (native.starts_with(L"\\\\")) {
        return std::filesystem::path(L"\\\\?\\UNC\\" + native.substr(2));
    }
    return std::filesystem::path(L"\\\\?\\" + native);
#else
    return logical_path;
#endif
}

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_PATH_IO_H
