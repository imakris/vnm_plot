// vnm_plot Synthetic Benchmark
// Stage 7: CLI polish with validation and improved UX

#include "benchmark_window.h"
#include "benchmark_profiler.h"

#include <QApplication>
#include <QDesktopServices>
#include <QSurfaceFormat>
#include <QUrl>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr const char* k_version = "1.0.0";

// Exit codes
constexpr int k_exit_success     = 0;
constexpr int k_exit_invalid_args = 1;
constexpr int k_exit_runtime_error = 2;

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
              << "  --output-dir <path>     Output directory for reports (default: current dir)\n"
              << "  --seed <number>         RNG seed for reproducibility (default: time-based)\n"
              << "  --volatility <value>    Brownian volatility (default: 0.02, range: 0.0-1.0)\n"
              << "  --rate <samples/sec>    Data generation rate (default: 1000, min: 1)\n"
              << "  --ring-size <count>     Ring buffer capacity (default: 100000, min: 100)\n"
              << "  --extended-metadata     Include benchmark-specific metadata in report\n"
              << "  --quiet                 Suppress progress output (report still written)\n"
              << "  --no-text               Disable text/font rendering\n"
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
    return "";  // Valid
}

void print_config_summary(const vnm::benchmark::Benchmark_config& config, std::ostream& os)
{
    os << "Configuration:\n"
       << "  Duration:     " << config.duration_seconds << "s\n"
       << "  Data type:    " << config.data_type << "\n"
       << "  Rate:         " << config.rate << " samples/sec\n"
       << "  Ring size:    " << config.ring_capacity << "\n"
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

    // Set OpenGL version before QApplication
    QSurfaceFormat format;
    format.setVersion(4, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);  // MSAA
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    if (!config.quiet) {
        std::cout << "vnm_plot Benchmark v" << k_version << "\n"
                  << std::string(30, '=') << "\n\n";
        print_config_summary(config, std::cout);
        std::cout << "\nStarting benchmark...\n";
    }

    // Create benchmark window
    vnm::benchmark::Benchmark_window window(config);

    // Track exit code
    int exit_code = k_exit_success;

    // Connect finish signal to generate report and quit
    QObject::connect(&window, &vnm::benchmark::Benchmark_window::benchmark_finished, [&]() {
        // Build report metadata
        vnm::benchmark::Report_metadata meta;
        meta.session = config.session;
        meta.stream = config.stream;
        meta.data_type = config.data_type;
        meta.target_duration = config.duration_seconds;
        meta.output_directory = config.output_directory;
        meta.started_at = window.started_at();
        meta.generated_at = std::chrono::system_clock::now();
        meta.include_extended = config.extended_metadata;
        meta.seed = config.seed;
        meta.volatility = config.volatility;
        meta.ring_capacity = config.ring_capacity;
        meta.samples_generated = window.samples_generated();

        // Generate and write report
        try {
            auto report_path = window.profiler().write_report(meta);

            if (!config.quiet) {
                std::cout << "\nBenchmark completed.\n"
                          << "  Samples generated: " << window.samples_generated() << "\n"
                          << "  Report written to: " << report_path.string() << "\n";
                std::cout << "\n" << window.profiler().generate_report(meta);
            }
            else {
                // In quiet mode, just output the report path
                std::cout << report_path.string() << "\n";
            }

            // Open the report file in default text editor
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(report_path.string())));
        }
        catch (const std::exception& e) {
            std::cerr << "Error writing report: " << e.what() << "\n";
            exit_code = k_exit_runtime_error;
        }

        app.quit();
    });

    window.show();

    int app_result = app.exec();
    return (app_result != 0) ? k_exit_runtime_error : exit_code;
}
