// vnm_plot Synthetic Benchmark
// Stage 7: CLI polish with validation and improved UX

#include "benchmark_build_metadata.h"
#include "benchmark_profiler.h"
#include "benchmark_window.h"

#include <QByteArray>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace {

constexpr const char* k_version = "1.0.0";

// Exit codes
constexpr int k_exit_success     = 0;
constexpr int k_exit_invalid_args = 1;
constexpr int k_exit_runtime_error = 2;

std::string compiler_identity()
{
#if defined(__GNUC__) && !defined(__clang__)
    return "GNU " + std::to_string(__GNUC__) + "." +
        std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(__clang__)
    return "Clang " + std::to_string(__clang_major__) + "." +
        std::to_string(__clang_minor__) + "." + std::to_string(__clang_patchlevel__);
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER) + "." + std::to_string(_MSC_FULL_VER);
#else
    return "unknown";
#endif
}

void print_version()
{
    std::cout << "vnm_plot_benchmark version " << k_version << "\n";
}

void print_usage(const char* program_name)
{
    std::cout << "Usage: " << program_name << " [options]\n"
              << "\n"
              << "Synthetic benchmark for vnm_plot rendering performance.\n"
              << "Generates Brownian motion data and measures rendering throughput.\n"
              << "\n"
              << "Options:\n"
              << "  --duration <seconds>    Benchmark duration (default: 30, min: 1)\n"
              << "  --session <name>        Session name for report header (default: benchmark_run)\n"
              << "  --stream <name>         Stream name for report (default: SIM)\n"
              << "  --data-type <type>      bars|trades (default: bars)\n"
              << "  --backend <backend>     qrhi|qrhi-offscreen (default: qrhi-offscreen)\n"
              << "  --graphics-backend <b> native|d3d11|metal|vulkan|opengl|null (default: native)\n"
              << "  --render-style <style>  dots|line|area (default: dots for trades, area for bars)\n"
              << "  --static                Skip the generator and render a fixed 4-segment line (visual-diff mode)\n"
              << "  --line-px <pixels>      Line thickness for line/area rendering (default: 1.5)\n"
              << "  --point-px <pixels>     Dot diameter for dots rendering (default: 1.0)\n"
              << "  --output-dir <path>     Output directory for reports (default: current dir)\n"
              << "  --seed <number>         RNG seed for reproducibility (default: time-based)\n"
              << "  --volatility <value>    Brownian volatility (default: 0.02, range: 0.0-1.0)\n"
              << "  --rate <samples/sec>    Data generation rate (default: 1000, min: 1)\n"
              << "  --ring-size <count>     Ring buffer capacity (default: 100000, min: 100)\n"
              << "  --series-count <count>  Number of ordinary series (default: 1)\n"
              << "  --extended-metadata     Include benchmark-specific metadata in report\n"
              << "  --quiet                 Suppress progress output (report still written)\n"
              << "  --no-text               Disable text/font rendering\n"
              << "  --finish                Wait for GPU completion after offscreen frames\n"
              << "  --width <pixels>        Framebuffer width (default: 1200)\n"
              << "  --height <pixels>       Framebuffer height (default: 720)\n"
              << "  --warmup-frames <count> Untimed frames before measurement (default: 2)\n"
              << "  --frames <count>        Exact measured frame count (default: duration-based)\n"
              << "  --scenario <name>       Stable scenario identifier retained in artifacts\n"
              << "  --pixel-checksum        Read back pixels and retain an output checksum\n"
              << "  --version               Show version information\n"
              << "  --help                  Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  " << program_name << " --duration 60 --data-type trades\n"
              << "  " << program_name << " --seed 12345 --volatility 0.05 --quiet\n"
              << "  " << program_name << " --output-dir ./reports --extended-metadata\n";
}

struct Parse_result {
    vnm::benchmark::Benchmark_config config;
    bool success = true;
    std::string error_message;
};

Parse_result parse_args(int argc, char* argv[])
{
    Parse_result result;
    auto& config = result.config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        try {
            if (arg == "--duration" && i + 1 < argc) {
                config.duration_seconds = std::stod(argv[++i]);
            }
            else
            if (arg == "--backend" && i + 1 < argc) {
                std::string backend = argv[++i];
                if (backend == "qrhi" || backend == "QRhi" || backend == "QRHI") {
                    config.backend = "qrhi";
                }
                else
                if (backend == "qrhi-offscreen" || backend == "qrhi_offscreen" ||
                    backend == "QRhiOffscreen" || backend == "QRHIOffscreen")
                {
                    config.backend = "qrhi-offscreen";
                }
                else {
                    result.success = false;
                    result.error_message =
                        "Invalid backend '" + backend + "'. Use 'qrhi' or 'qrhi-offscreen'.";
                    return result;
                }
            }
            else
            if (arg == "--graphics-backend" && i + 1 < argc) {
                config.graphics_backend = argv[++i];
                std::transform(
                    config.graphics_backend.begin(),
                    config.graphics_backend.end(),
                    config.graphics_backend.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            }
            else
            if (arg == "--session" && i + 1 < argc) {
                config.session = argv[++i];
            }
            else
            if (arg == "--stream" && i + 1 < argc) {
                config.stream = argv[++i];
            }
            else
            if (arg == "--data-type" && i + 1 < argc) {
                std::string type = argv[++i];
                if (type == "trades" || type == "Trades") {
                    config.data_type = "Trades";
                }
                else
                if (type == "bars" || type == "Bars") {
                    config.data_type = "Bars";
                }
                else {
                    result.success = false;
                    result.error_message = "Invalid data type '" + type + "'. Use 'bars' or 'trades'.";
                    return result;
                }
            }
            else
            if (arg == "--render-style" && i + 1 < argc) {
                std::string style = argv[++i];
                if (style == "dots" || style == "Dots") {
                    config.style = "Dots";
                }
                else
                if (style == "line" || style == "Line") {
                    config.style = "Line";
                }
                else
                if (style == "area" || style == "Area") {
                    config.style = "Area";
                }
                else {
                    result.success = false;
                    result.error_message = "Invalid style '" + style + "'. Use 'dots', 'line', or 'area'.";
                    return result;
                }
            }
            else
            if (arg == "--static") {
                config.static_data = true;
            }
            else
            if (arg == "--line-px" && i + 1 < argc) {
                config.line_width_px = std::stod(argv[++i]);
            }
            else
            if (arg == "--point-px" && i + 1 < argc) {
                config.point_diameter_px = std::stod(argv[++i]);
            }
            else
            if (arg == "--output-dir" && i + 1 < argc) {
                config.output_directory = argv[++i];
            }
            else
            if (arg == "--seed" && i + 1 < argc) {
                config.seed = std::stoull(argv[++i]);
            }
            else
            if (arg == "--volatility" && i + 1 < argc) {
                config.volatility = std::stod(argv[++i]);
            }
            else
            if (arg == "--rate" && i + 1 < argc) {
                config.rate = std::stod(argv[++i]);
            }
            else
            if (arg == "--ring-size" && i + 1 < argc) {
                config.ring_capacity = std::stoull(argv[++i]);
            }
            else
            if (arg == "--series-count" && i + 1 < argc) {
                config.series_count = std::stoull(argv[++i]);
            }
            else
            if (arg == "--extended-metadata") {
                config.extended_metadata = true;
            }
            else
            if (arg == "--quiet") {
                config.quiet = true;
            }
            else
            if (arg == "--no-text") {
                config.show_text = false;
            }
            else
            if (arg == "--finish") {
                config.finish = true;
            }
            else
            if (arg == "--width" && i + 1 < argc) {
                config.framebuffer_width = std::stoi(argv[++i]);
            }
            else
            if (arg == "--height" && i + 1 < argc) {
                config.framebuffer_height = std::stoi(argv[++i]);
            }
            else
            if (arg == "--warmup-frames" && i + 1 < argc) {
                config.warmup_frames = std::stoull(argv[++i]);
            }
            else
            if (arg == "--frames" && i + 1 < argc) {
                config.measured_frames = std::stoull(argv[++i]);
            }
            else
            if (arg == "--scenario" && i + 1 < argc) {
                config.scenario = argv[++i];
            }
            else
            if (arg == "--pixel-checksum") {
                config.capture_pixel_checksum = true;
            }
            else
            if (arg == "--help" || arg == "-h" || arg == "--version" || arg == "-v") {
                // Handled separately in main
            }
            else
            if (arg.starts_with("--")) {
                result.success = false;
                result.error_message = "Unknown option: " + arg;
                return result;
            }
        }
        catch (const std::invalid_argument&) {
            result.success = false;
            result.error_message = "Invalid value for " + arg + ": not a valid number";
            return result;
        }
        catch (const std::out_of_range&) {
            result.success = false;
            result.error_message = "Value out of range for " + arg;
            return result;
        }
    }

    // Default seed to current time if not specified
    if (config.seed == 0) {
        config.seed = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }

    return result;
}

std::string validate_config(const vnm::benchmark::Benchmark_config& config)
{
    if (config.duration_seconds < 1.0) {
        return "Duration must be at least 1 second (got " +
               std::to_string(config.duration_seconds) + ")";
    }
    if (config.backend != "qrhi" && config.backend != "qrhi-offscreen") {
        return "Backend must be 'qrhi' or 'qrhi-offscreen'";
    }
    if (config.capture_pixel_checksum && config.backend != "qrhi-offscreen") {
        return "Pixel checksum capture currently requires qrhi-offscreen";
    }
    const std::array<std::string, 6> graphics_backends = {
        "native", "d3d11", "metal", "vulkan", "opengl", "null"
    };
    if (std::find(
            graphics_backends.begin(),
            graphics_backends.end(),
            config.graphics_backend) == graphics_backends.end())
    {
        return "Graphics backend must be native, d3d11, metal, vulkan, opengl, or null";
    }
    if (config.duration_seconds > 3600.0) {
        return "Duration cannot exceed 3600 seconds (1 hour)";
    }
    if (config.rate < 1.0) {
        return "Rate must be at least 1 sample/sec (got " +
               std::to_string(config.rate) + ")";
    }
    if (config.rate > 1000000.0) {
        return "Rate cannot exceed 1000000 samples/sec";
    }
    if (config.ring_capacity < 100) {
        return "Ring buffer capacity must be at least 100 (got " +
               std::to_string(config.ring_capacity) + ")";
    }
    if (config.ring_capacity > 100000000) {
        return "Ring buffer capacity cannot exceed 100000000";
    }
    if (config.series_count == 0 || config.series_count > 4096) {
        return "Series count must be between 1 and 4096";
    }
    if (config.volatility < 0.0 || config.volatility > 1.0) {
        return "Volatility must be between 0.0 and 1.0 (got " +
               std::to_string(config.volatility) + ")";
    }
    if (config.session.empty()) {
        return "Session name cannot be empty";
    }
    if (config.stream.empty()) {
        return "Stream name cannot be empty";
    }
    if (config.framebuffer_width < 64 || config.framebuffer_height < 64) {
        return "Framebuffer dimensions must each be at least 64 pixels";
    }
    if (config.framebuffer_width > 16384 || config.framebuffer_height > 16384) {
        return "Framebuffer dimensions cannot exceed 16384 pixels";
    }
    if (config.measured_frames > 10'000'000 || config.warmup_frames > 1'000'000) {
        return "Frame counts exceed the supported limit";
    }
    if (config.scenario.empty()) {
        return "Scenario name cannot be empty";
    }
    return "";  // Valid
}

void print_config_summary(const vnm::benchmark::Benchmark_config& config, std::ostream& os)
{
    os << "Configuration:\n"
       << "  Duration:     " << config.duration_seconds << "s\n"
       << "  Backend:      " << config.backend << "\n"
       << "  Graphics:     " << config.graphics_backend << "\n"
       << "  Framebuffer:  " << config.framebuffer_width << "x"
       << config.framebuffer_height << "\n"
       << "  Warmup:       " << config.warmup_frames << " frames\n"
       << "  Frames:       " << (config.measured_frames == 0
            ? std::string("duration-based")
            : std::to_string(config.measured_frames)) << "\n"
       << "  Scenario:     " << config.scenario << "\n"
       << "  Data type:    " << config.data_type << "\n"
       << "  Style:        " << (config.style.empty() ? "default" : config.style) << "\n"
       << "  Rate:         " << config.rate << " samples/sec\n"
       << "  Ring size:    " << config.ring_capacity << "\n"
       << "  Series:       " << config.series_count << "\n"
       << "  Volatility:   " << config.volatility << "\n"
       << "  Seed:         " << config.seed << "\n"
       << "  Output dir:   " << config.output_directory << "\n"
       << "  Session:      " << config.session << "\n"
       << "  Stream:       " << config.stream << "\n"
       << "  Show text:    " << (config.show_text ? "yes" : "no") << "\n";
}

}  // namespace

int main(int argc, char* argv[])
{
    // Parse --help and --version early (before QApplication)
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return k_exit_success;
        }
        if (arg == "--version" || arg == "-v") {
            print_version();
            return k_exit_success;
        }
    }

    // Parse configuration
    auto parse_result = parse_args(argc, argv);
    if (!parse_result.success) {
        std::cerr << "Error: " << parse_result.error_message << "\n";
        std::cerr << "Use --help for usage information.\n";
        return k_exit_invalid_args;
    }

    auto& config = parse_result.config;

    std::ostringstream invocation_stream;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) {
            invocation_stream << ' ';
        }
        invocation_stream << argv[i];
    }
    const std::string invocation = invocation_stream.str();

    // Validate configuration
    std::string validation_error = validate_config(config);
    if (!validation_error.empty()) {
        std::cerr << "Error: " << validation_error << "\n";
        std::cerr << "Use --help for usage information.\n";
        return k_exit_invalid_args;
    }

    // Ensure output directory exists
    try {
        std::filesystem::create_directories(config.output_directory);
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error: Cannot create output directory '"
                  << config.output_directory << "': " << e.what() << "\n";
        return k_exit_invalid_args;
    }

    if (config.graphics_backend == "native") {
        qunsetenv("QSG_RHI_BACKEND");
    }
    else {
        qputenv("QSG_RHI_BACKEND", QByteArray::fromStdString(config.graphics_backend));
    }

    if (config.backend == "qrhi" && config.graphics_backend != "native") {
        const auto api = config.graphics_backend == "d3d11"
            ? QSGRendererInterface::Direct3D11
            : config.graphics_backend == "metal"
                ? QSGRendererInterface::Metal
                : config.graphics_backend == "vulkan"
                    ? QSGRendererInterface::Vulkan
                    : config.graphics_backend == "opengl"
                        ? QSGRendererInterface::OpenGL
                        : QSGRendererInterface::Null;
        QQuickWindow::setGraphicsApi(api);
    }

    // Set the requested presentation format before QApplication. Qt Quick's
    // QRhi render loop maps swapInterval(0) to QRhiSwapChain::NoVSync.
    QSurfaceFormat format;
    format.setVersion(4, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);  // MSAA
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);

    QGuiApplication app(argc, argv);

    const auto add_reproduction_metadata = [&](auto& meta, const auto& benchmark) {
        const auto graphics = benchmark.graphics_device_info();
        meta.reproduction["actual_graphics_backend"] = graphics.backend;
        meta.reproduction["build_type"] = VNM_PLOT_BENCHMARK_BUILD_TYPE;
        meta.reproduction["compiler"] = compiler_identity();
        meta.reproduction["dependency_commit"] = VNM_PLOT_BENCHMARK_DEPENDENCY_COMMIT;
        meta.reproduction["device_id"] = std::to_string(graphics.device_id);
        meta.reproduction["device_name"] = graphics.device_name;
        meta.reproduction["device_type"] = graphics.device_type;
        meta.reproduction["driver"] = graphics.backend + " via Qt QRhi";
        meta.reproduction["finish_state"] = config.finish
            ? "enabled"
            : config.capture_pixel_checksum ? "forced-by-pixel-readback" : "disabled";
        meta.reproduction["framebuffer"] = std::to_string(config.framebuffer_width) + "x" +
            std::to_string(config.framebuffer_height);
        meta.reproduction["invocation"] = invocation;
        meta.reproduction["measured_frames"] =
            std::to_string(benchmark.measured_frame_count());
        meta.reproduction["presentation_backend"] = config.backend;
        meta.reproduction["pixel_checksum"] = std::to_string(benchmark.pixel_checksum());
        meta.reproduction["qt_version"] = qVersion();
        meta.reproduction["requested_graphics_backend"] = config.graphics_backend;
        meta.reproduction["scenario"] = config.scenario;
        meta.reproduction["series_count"] = std::to_string(config.series_count);
        meta.reproduction["show_text"] = config.show_text ? "true" : "false";
        meta.reproduction["source_commit"] = VNM_PLOT_BENCHMARK_SOURCE_COMMIT;
        meta.reproduction["source_diff_sha256"] = VNM_PLOT_BENCHMARK_SOURCE_DIFF_SHA256;
        meta.reproduction["source_dirty"] = VNM_PLOT_BENCHMARK_SOURCE_DIRTY;
        meta.reproduction["source_tree"] = VNM_PLOT_BENCHMARK_SOURCE_TREE;
        meta.reproduction["vendor_id"] = std::to_string(graphics.vendor_id);
        meta.reproduction["warmup_frames"] = std::to_string(config.warmup_frames);
    };

    if (!config.quiet) {
        std::cout << "vnm_plot Benchmark v" << k_version << "\n"
                  << std::string(30, '=') << "\n\n";
        print_config_summary(config, std::cout);
        std::cout << "\nStarting benchmark...\n";
    }

    // Track exit code
    int exit_code = k_exit_success;

    const auto graphics_backend_is_valid = [&](const auto& benchmark) {
        const auto graphics = benchmark.graphics_device_info();
        return graphics.backend != "uninitialized" &&
            (config.graphics_backend == "null" || graphics.backend != "Null");
    };

    auto finish_benchmark = [&](auto& window) {
        if (!graphics_backend_is_valid(window)) {
            std::cerr << "Error: requested graphics backend '" << config.graphics_backend
                      << "' did not initialize a non-Null QRhi backend\n";
            exit_code = k_exit_runtime_error;
            app.quit();
            return;
        }
        // Build report metadata
        vnm::benchmark::Report_metadata meta;
        meta.session = config.session;
        meta.stream = config.stream;
        meta.data_type = config.data_type;
        meta.backend = config.backend;
        meta.target_duration = config.duration_seconds;
        meta.output_directory = config.output_directory;
        meta.started_at = window.started_at();
        meta.generated_at = std::chrono::system_clock::now();
        meta.include_extended = config.extended_metadata;
        meta.seed = config.seed;
        meta.volatility = config.volatility;
        meta.ring_capacity = config.ring_capacity;
        meta.samples_generated = window.samples_generated();
        add_reproduction_metadata(meta, window);

        // Generate and write report
        try {
            auto report_path = window.profiler().write_report(meta);
            const auto raw_path = window.profiler().raw_report_path(meta);

            if (!config.quiet) {
                std::cout << "\nBenchmark completed.\n"
                          << "  Samples generated: " << window.samples_generated() << "\n"
                          << "  Report written to: " << report_path.string() << "\n";
                std::cout << "  Raw artifact: " << raw_path.string() << "\n";
                std::cout << "\n" << window.profiler().generate_report(meta);
            }
            else {
                // In quiet mode, just output the report path
                std::cout << report_path.string() << "\n";
            }

            if (!config.quiet) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(report_path.string())));
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error writing report: " << e.what() << "\n";
            exit_code = k_exit_runtime_error;
        }

        app.quit();
    };

    auto write_benchmark_report = [&](auto& benchmark) {
        if (!graphics_backend_is_valid(benchmark)) {
            std::cerr << "Error: requested graphics backend '" << config.graphics_backend
                      << "' did not initialize a non-Null QRhi backend\n";
            return k_exit_runtime_error;
        }
        vnm::benchmark::Report_metadata meta;
        meta.session = config.session;
        meta.stream = config.stream;
        meta.data_type = config.data_type;
        meta.backend = config.backend;
        meta.target_duration = config.duration_seconds;
        meta.output_directory = config.output_directory;
        meta.started_at = benchmark.started_at();
        meta.generated_at = std::chrono::system_clock::now();
        meta.include_extended = config.extended_metadata;
        meta.seed = config.seed;
        meta.volatility = config.volatility;
        meta.ring_capacity = config.ring_capacity;
        meta.samples_generated = benchmark.samples_generated();
        add_reproduction_metadata(meta, benchmark);

        try {
            auto report_path = benchmark.profiler().write_report(meta);
            const auto raw_path = benchmark.profiler().raw_report_path(meta);
            if (!config.quiet) {
                std::cout << "\nBenchmark completed.\n"
                          << "  Samples generated: " << benchmark.samples_generated() << "\n"
                          << "  Report written to: " << report_path.string() << "\n";
                std::cout << "  Raw artifact: " << raw_path.string() << "\n";
                std::cout << "\n" << benchmark.profiler().generate_report(meta);
            }
            else {
                std::cout << report_path.string() << "\n";
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error writing report: " << e.what() << "\n";
            return k_exit_runtime_error;
        }

        return k_exit_success;
    };

    if (config.backend == "qrhi-offscreen") {
        vnm::benchmark::Benchmark_rhi_offscreen_runner runner(config);
        std::string error_message;
        if (!runner.run(error_message)) {
            std::cerr << "Error: " << error_message << "\n";
            return k_exit_runtime_error;
        }
        return write_benchmark_report(runner);
    }

    std::unique_ptr<vnm::benchmark::Benchmark_rhi_window> rhi_window;

    rhi_window = std::make_unique<vnm::benchmark::Benchmark_rhi_window>(config);
    rhi_window->setFormat(format);
    QObject::connect(
        rhi_window.get(),
        &vnm::benchmark::Benchmark_rhi_window::benchmark_finished,
        [&]() {
            finish_benchmark(*rhi_window);
        });
    rhi_window->show();

    int app_result = app.exec();
    return (app_result != 0) ? k_exit_runtime_error : exit_code;
}
