// vnm_plot Headless GLFW Benchmark
// CI-compatible benchmark using GLFW with invisible window + FBO rendering
//
// Usage:
//   ./vnm_plot_benchmark_headless [options]
//
// For CI environments without display, run with:
//   xvfb-run ./vnm_plot_benchmark_headless
// or use Mesa software rendering:
//   LIBGL_ALWAYS_SOFTWARE=1 ./vnm_plot_benchmark_headless

#include <vnm_plot/vnm_plot.h>

#include <glatter/glatter.h>

// Undef X11 Status macro that conflicts with snapshot_result_t::Status
// This must be done immediately after glatter.h includes X11 headers
#ifdef Status
#undef Status
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <GLFW/glfw3.h>

// Reuse benchmark components from the Qt benchmark
#include "benchmark_constants.h"
#include "ring_buffer.h"
#include "sample_types.h"
#include "brownian_generator.h"
#include "benchmark_profiler.h"
#include "benchmark_data_source.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>

// Import shared benchmark constants
using vnm::benchmark::k_adjusted_font_px;
using vnm::benchmark::k_base_label_height_px;
using vnm::benchmark::k_adjusted_preview_height;
using vnm::benchmark::k_vbar_width_pixels;

namespace {

constexpr const char* k_version = "1.0.0";
constexpr int k_exit_success = 0;
constexpr int k_exit_invalid_args = 1;
constexpr int k_exit_gl_error = 2;
constexpr int k_exit_runtime_error = 3;

// Default framebuffer dimensions for offscreen rendering
constexpr int k_default_width = 1200;
constexpr int k_default_height = 720;

/// Configuration for the headless benchmark
struct Headless_config {
    double duration_seconds = 30.0;
    std::string session = "headless_benchmark";
    std::string symbol = "SIM";
    std::string data_type = "Bars";  // "Bars" or "Trades"
    std::filesystem::path output_directory = ".";
    uint64_t seed = 0;  // 0 = time-based
    double volatility = 0.02;
    double rate = 1000.0;  // samples per second
    std::size_t ring_capacity = 100000;
    bool extended_metadata = false;
    bool quiet = false;
    bool show_text = true;  // Text rendering enabled by default (like Qt benchmark)
    bool no_gl = false;     // Skip all GL calls to measure pure CPU time
    int width = k_default_width;
    int height = k_default_height;
    int target_fps = 60;  // Target frames per second
};

void print_version()
{
    std::cout << "vnm_plot_benchmark_headless version " << k_version << "\n";
}

void print_usage(const char* program_name)
{
    std::cout << "Usage: " << program_name << " [options]\n"
              << "\n"
              << "Headless GLFW benchmark for vnm_plot rendering performance.\n"
              << "Generates Brownian motion data and measures rendering throughput.\n"
              << "Designed for CI environments - no display required.\n"
              << "\n"
              << "Options:\n"
              << "  --duration <seconds>    Benchmark duration (default: 30, min: 1)\n"
              << "  --session <name>        Session name for report (default: headless_benchmark)\n"
              << "  --symbol <name>         Symbol name for report (default: SIM)\n"
              << "  --data-type <type>      bars|trades (default: bars)\n"
              << "  --output-dir <path>     Output directory for reports (default: current dir)\n"
              << "  --seed <number>         RNG seed for reproducibility (default: time-based)\n"
              << "  --volatility <value>    Brownian volatility (default: 0.02, range: 0.0-1.0)\n"
              << "  --rate <samples/sec>    Data generation rate (default: 1000, min: 1)\n"
              << "  --ring-size <count>     Ring buffer capacity (default: 100000, min: 100)\n"
              << "  --width <pixels>        Framebuffer width (default: 1200)\n"
              << "  --height <pixels>       Framebuffer height (default: 720)\n"
              << "  --fps <target>          Target frames per second (default: 60)\n"
              << "  --extended-metadata     Include benchmark-specific metadata in report\n"
              << "  --quiet                 Suppress progress output (report still written)\n"
              << "  --no-text               Disable text/font rendering\n"
              << "  --no-gl                 Skip all GL calls to measure pure CPU time\n"
              << "  --version               Show version information\n"
              << "  --help                  Show this help message\n"
              << "\n"
              << "CI Usage:\n"
              << "  # With X virtual framebuffer:\n"
              << "  xvfb-run " << program_name << " --duration 10\n"
              << "\n"
              << "  # With Mesa software rendering:\n"
              << "  LIBGL_ALWAYS_SOFTWARE=1 " << program_name << " --duration 10\n"
              << "\n"
              << "Examples:\n"
              << "  " << program_name << " --duration 60 --data-type trades\n"
              << "  " << program_name << " --seed 12345 --volatility 0.05 --quiet\n"
              << "  " << program_name << " --output-dir ./reports --extended-metadata\n";
}

struct Parse_result {
    Headless_config config;
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
            else if (arg == "--session" && i + 1 < argc) {
                config.session = argv[++i];
            }
            else if (arg == "--symbol" && i + 1 < argc) {
                config.symbol = argv[++i];
            }
            else if (arg == "--data-type" && i + 1 < argc) {
                std::string type = argv[++i];
                if (type == "trades" || type == "Trades") {
                    config.data_type = "Trades";
                }
                else if (type == "bars" || type == "Bars") {
                    config.data_type = "Bars";
                }
                else {
                    result.success = false;
                    result.error_message = "Invalid data type '" + type + "'. Use 'bars' or 'trades'.";
                    return result;
                }
            }
            else if (arg == "--output-dir" && i + 1 < argc) {
                config.output_directory = argv[++i];
            }
            else if (arg == "--seed" && i + 1 < argc) {
                config.seed = std::stoull(argv[++i]);
            }
            else if (arg == "--volatility" && i + 1 < argc) {
                config.volatility = std::stod(argv[++i]);
            }
            else if (arg == "--rate" && i + 1 < argc) {
                config.rate = std::stod(argv[++i]);
            }
            else if (arg == "--ring-size" && i + 1 < argc) {
                config.ring_capacity = std::stoull(argv[++i]);
            }
            else if (arg == "--width" && i + 1 < argc) {
                config.width = std::stoi(argv[++i]);
            }
            else if (arg == "--height" && i + 1 < argc) {
                config.height = std::stoi(argv[++i]);
            }
            else if (arg == "--fps" && i + 1 < argc) {
                config.target_fps = std::stoi(argv[++i]);
            }
            else if (arg == "--extended-metadata") {
                config.extended_metadata = true;
            }
            else if (arg == "--quiet") {
                config.quiet = true;
            }
            else if (arg == "--no-text") {
                config.show_text = false;
            }
            else if (arg == "--no-gl") {
                config.no_gl = true;
            }
            else if (arg == "--help" || arg == "-h" || arg == "--version" || arg == "-v") {
                // Handled separately in main
            }
            else if (arg.rfind("--", 0) == 0) {
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

std::string validate_config(const Headless_config& config)
{
    if (config.duration_seconds < 1.0) {
        return "Duration must be at least 1 second";
    }
    if (config.duration_seconds > 3600.0) {
        return "Duration cannot exceed 3600 seconds (1 hour)";
    }
    if (config.rate < 1.0) {
        return "Rate must be at least 1 sample/sec";
    }
    if (config.rate > 1000000.0) {
        return "Rate cannot exceed 1000000 samples/sec";
    }
    if (config.ring_capacity < 100) {
        return "Ring buffer capacity must be at least 100";
    }
    if (config.ring_capacity > 100000000) {
        return "Ring buffer capacity cannot exceed 100000000";
    }
    if (config.volatility < 0.0 || config.volatility > 1.0) {
        return "Volatility must be between 0.0 and 1.0";
    }
    if (config.width < 100 || config.width > 8192) {
        return "Width must be between 100 and 8192";
    }
    if (config.height < 100 || config.height > 8192) {
        return "Height must be between 100 and 8192";
    }
    if (config.target_fps < 1 || config.target_fps > 1000) {
        return "FPS must be between 1 and 1000";
    }
    return "";
}

void print_config_summary(const Headless_config& config, std::ostream& os)
{
    os << "Configuration:\n"
       << "  Duration:     " << config.duration_seconds << "s\n"
       << "  Data type:    " << config.data_type << "\n"
       << "  Rate:         " << config.rate << " samples/sec\n"
       << "  Ring size:    " << config.ring_capacity << "\n"
       << "  Volatility:   " << config.volatility << "\n"
       << "  Seed:         " << config.seed << "\n"
       << "  Resolution:   " << config.width << "x" << config.height << "\n"
       << "  Target FPS:   " << config.target_fps << "\n"
       << "  Output dir:   " << config.output_directory.string() << "\n"
       << "  Session:      " << config.session << "\n"
       << "  Symbol:       " << config.symbol << "\n"
       << "  Show text:    " << (config.show_text ? "yes" : "no") << "\n"
       << "  No GL:        " << (config.no_gl ? "yes" : "no") << "\n";
}

std::string format_benchmark_timestamp(double ts, double /*range*/)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", ts);
    return buf;
}

void glfw_error_callback(int code, const char* desc)
{
    std::cerr << "GLFW error " << code << ": " << (desc ? desc : "(null)") << "\n";
}

/// Framebuffer Object wrapper for offscreen rendering
class Offscreen_fbo {
public:
    Offscreen_fbo(int width, int height)
        : m_width(width), m_height(height)
    {
    }

    ~Offscreen_fbo()
    {
        // Note: cleanup() should be called explicitly before context destruction
        // The destructor is a safety net but may not work if context is gone
    }

    bool initialize()
    {
        // Try multisampled FBO first, fall back to non-multisampled if it fails
        // (software renderers like llvmpipe may not support MSAA renderbuffers)
        if (try_initialize_multisampled()) {
            return true;
        }

        // Fallback to non-multisampled
        cleanup();
        return try_initialize_simple();
    }

    void bind()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glViewport(0, 0, m_width, m_height);
    }

    void unbind()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    /// Explicit cleanup - must be called before GL context destruction
    void cleanup()
    {
        if (m_fbo) {
            glDeleteFramebuffers(1, &m_fbo);
            m_fbo = 0;
        }
        if (m_color_rbo) {
            glDeleteRenderbuffers(1, &m_color_rbo);
            m_color_rbo = 0;
        }
        if (m_depth_rbo) {
            glDeleteRenderbuffers(1, &m_depth_rbo);
            m_depth_rbo = 0;
        }
    }

    int width() const { return m_width; }
    int height() const { return m_height; }
    bool is_multisampled() const { return m_multisampled; }

private:
    bool try_initialize_multisampled()
    {
        // Check if MSAA is supported
        GLint max_samples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
        if (max_samples < 2) {
            return false;
        }

        int samples = std::min(static_cast<int>(vnm::plot::k_msaa_samples), max_samples);

        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glGenRenderbuffers(1, &m_color_rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_color_rbo);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, m_width, m_height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_color_rbo);

        glGenRenderbuffers(1, &m_depth_rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depth_rbo);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, m_width, m_height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth_rbo);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (status != GL_FRAMEBUFFER_COMPLETE) {
            return false;
        }

        m_multisampled = true;
        return true;
    }

    bool try_initialize_simple()
    {
        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        // Create simple color renderbuffer (no multisampling)
        glGenRenderbuffers(1, &m_color_rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_color_rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, m_width, m_height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_color_rbo);

        // Create simple depth renderbuffer
        glGenRenderbuffers(1, &m_depth_rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depth_rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_width, m_height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth_rbo);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Framebuffer incomplete: 0x" << std::hex << status << std::dec << "\n";
            return false;
        }

        m_multisampled = false;
        return true;
    }

    int m_width;
    int m_height;
    GLuint m_fbo = 0;
    GLuint m_color_rbo = 0;
    GLuint m_depth_rbo = 0;
    bool m_multisampled = false;
};

}  // namespace

int main(int argc, char* argv[])
{
    // Parse --help and --version early
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
        return k_exit_invalid_args;
    }

    // Ensure output directory exists
    try {
        std::filesystem::create_directories(config.output_directory);
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error: Cannot create output directory: " << e.what() << "\n";
        return k_exit_invalid_args;
    }

    if (!config.quiet) {
        std::cout << "vnm_plot Headless GLFW Benchmark v" << k_version << "\n"
                  << std::string(45, '=') << "\n\n";
        print_config_summary(config, std::cout);
        std::cout << "\n";
    }

    // Initialize GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::cerr << "Error: Failed to initialize GLFW\n";
        return k_exit_gl_error;
    }

    // Create invisible window for OpenGL context
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);  // Headless - no visible window
    glfwWindowHint(GLFW_SAMPLES, vnm::plot::k_msaa_samples);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(config.width, config.height,
                                          "vnm_plot_benchmark_headless", nullptr, nullptr);
    if (!window) {
        std::cerr << "Error: Failed to create GLFW window\n"
                  << "Hint: You may need xvfb-run or LIBGL_ALWAYS_SOFTWARE=1\n";
        glfwTerminate();
        return k_exit_gl_error;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);  // No vsync for benchmarking

    // Initialize OpenGL loader
    if (!vnm::plot::init_gl()) {
        std::cerr << "Error: Failed to initialize OpenGL loader (glatter)\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return k_exit_gl_error;
    }

    // Validate OpenGL version
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (!config.quiet) {
        std::cout << "OpenGL " << major << "." << minor << " initialized\n";
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        std::cout << "Renderer: " << (renderer ? renderer : "unknown") << "\n\n";
    }

    if (major < 4 || (major == 4 && minor < 3)) {
        std::cerr << "Error: OpenGL 4.3+ required, got " << major << "." << minor << "\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return k_exit_gl_error;
    }

    // Create offscreen FBO
    Offscreen_fbo fbo(config.width, config.height);
    if (!fbo.initialize()) {
        std::cerr << "Error: Failed to create offscreen framebuffer\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return k_exit_gl_error;
    }

    // Initialize asset loader and renderers
    vnm::plot::Asset_loader asset_loader;
    asset_loader.set_log_callback([&config](const std::string& msg) {
        if (!config.quiet) {
            std::cerr << "asset_loader: " << msg << "\n";
        }
    });
    vnm::plot::init_embedded_assets(asset_loader);

    vnm::plot::Primitive_renderer primitives;
    vnm::plot::Series_renderer series_renderer;
    vnm::plot::Chrome_renderer chrome_renderer;

    if (!primitives.initialize(asset_loader)) {
        std::cerr << "Error: Failed to initialize primitive renderer\n";
        fbo.cleanup();
        glfwDestroyWindow(window);
        glfwTerminate();
        return k_exit_gl_error;
    }
    series_renderer.initialize(asset_loader);

    // Initialize text rendering components (when available and enabled)
#if defined(VNM_PLOT_ENABLE_TEXT)
    vnm::plot::Font_renderer font_renderer;
    std::unique_ptr<vnm::plot::Text_renderer> text_renderer;
    bool text_enabled = config.show_text;

    if (text_enabled) {
        const int font_px = static_cast<int>(std::round(k_adjusted_font_px));
        font_renderer.initialize(asset_loader, font_px);
        text_renderer = std::make_unique<vnm::plot::Text_renderer>(&font_renderer);
    }
#else
    const bool text_enabled = false;
    if (config.show_text && !config.quiet) {
        std::cout << "Note: Text rendering disabled at build time (VNM_PLOT_ENABLE_TEXT=OFF)\n";
    }
#endif

    // Create profiler
    vnm::benchmark::Benchmark_profiler profiler;

    // Layout calculator and cache
    vnm::plot::Layout_calculator layout_calc;
    vnm::plot::Layout_cache layout_cache;

    // Configure rendering (matching Qt benchmark defaults)
    vnm::plot::Render_config render_config;
    render_config.dark_mode = true;
    render_config.show_text = text_enabled;
    render_config.snap_lines_to_pixels = false;
    render_config.line_width_px = 1.5;
    render_config.skip_gl_calls = config.no_gl;
    render_config.format_timestamp = format_benchmark_timestamp;
    render_config.profiler = &profiler;

    // Create Brownian generator
    vnm::benchmark::Brownian_generator::Config gen_config;
    gen_config.seed = config.seed;
    gen_config.volatility = config.volatility;
    gen_config.initial_price = 100.0;
    gen_config.time_step = 1.0 / config.rate;
    vnm::benchmark::Brownian_generator generator(gen_config);

    // Create ring buffers and data sources
    std::unique_ptr<vnm::benchmark::Ring_buffer<vnm::benchmark::Bar_sample>> bar_buffer;
    std::unique_ptr<vnm::benchmark::Ring_buffer<vnm::benchmark::Trade_sample>> trade_buffer;
    std::unique_ptr<vnm::benchmark::Benchmark_data_source<vnm::benchmark::Bar_sample>> bar_source;
    std::unique_ptr<vnm::benchmark::Benchmark_data_source<vnm::benchmark::Trade_sample>> trade_source;

    if (config.data_type == "Trades") {
        trade_buffer = std::make_unique<vnm::benchmark::Ring_buffer<vnm::benchmark::Trade_sample>>(config.ring_capacity);
        trade_source = std::make_unique<vnm::benchmark::Benchmark_data_source<vnm::benchmark::Trade_sample>>(*trade_buffer);
    }
    else {
        bar_buffer = std::make_unique<vnm::benchmark::Ring_buffer<vnm::benchmark::Bar_sample>>(config.ring_capacity);
        bar_source = std::make_unique<vnm::benchmark::Benchmark_data_source<vnm::benchmark::Bar_sample>>(*bar_buffer);
    }

    // Set up series
    auto series = std::make_shared<vnm::plot::series_data_t>();
    series->id = 1;
    series->enabled = true;
    series->color = glm::vec4(0.2f, 0.7f, 0.9f, 1.0f);

    if (config.data_type == "Trades") {
        series->style = vnm::plot::Display_style::DOTS;
        series->shader_set = {
            "shaders/function_sample.vert",
            "shaders/plot_dot.geom",
            "shaders/plot_dot.frag"
        };
        series->data_source = std::shared_ptr<vnm::plot::Data_source>(
            trade_source.get(), [](vnm::plot::Data_source*) {});
        series->access = vnm::benchmark::make_trade_access_policy();
    }
    else {
        series->style = vnm::plot::Display_style::AREA;
        series->shader_set = {
            "shaders/function_sample.vert",
            "shaders/plot_area.geom",
            "shaders/plot_area.frag"
        };
        series->data_source = std::shared_ptr<vnm::plot::Data_source>(
            bar_source.get(), [](vnm::plot::Data_source*) {});
        series->access = vnm::benchmark::make_bar_access_policy();
    }

    std::map<int, std::shared_ptr<vnm::plot::series_data_t>> series_map;
    series_map[series->id] = series;

    // View range state
    double t_min = 0.0;
    double t_max = 10.0;
    double t_available_min = 0.0;
    float v_min = 90.0f;
    float v_max = 110.0f;

    // Timing
    auto started_at = std::chrono::system_clock::now();
    auto benchmark_start = std::chrono::steady_clock::now();
    auto last_frame_time = benchmark_start;
    const double frame_interval_ns = 1e9 / config.target_fps;

    std::atomic<std::size_t> samples_generated{0};
    std::atomic<bool> stop_generator{false};

    // Generator thread
    std::thread generator_thread([&]() {
        const double ns_per_sample = 1e9 / config.rate;
        auto gen_start = std::chrono::steady_clock::now();

        while (!stop_generator.load()) {
            auto now = std::chrono::steady_clock::now();
            double elapsed_ns = std::chrono::duration<double, std::nano>(now - gen_start).count();
            double target_samples = elapsed_ns / ns_per_sample;

            std::size_t current_count = samples_generated.load();
            while (current_count < static_cast<std::size_t>(target_samples) && !stop_generator.load()) {
                if (config.data_type == "Trades") {
                    trade_buffer->push(generator.next_trade());
                }
                else {
                    bar_buffer->push(generator.next_bar());
                }
                ++samples_generated;
                ++current_count;
            }

            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    if (!config.quiet) {
        std::cout << "Starting benchmark (" << config.duration_seconds << "s)...\n";
    }

    std::size_t frame_count = 0;
    const int fb_w = fbo.width();
    const int fb_h = fbo.height();

    // Main render loop
    while (!glfwWindowShouldClose(window)) {
        auto now = std::chrono::steady_clock::now();
        double elapsed_seconds = std::chrono::duration<double>(now - benchmark_start).count();

        // Check if benchmark duration completed
        if (elapsed_seconds >= config.duration_seconds) {
            break;
        }

        // Frame pacing
        double frame_elapsed_ns = std::chrono::duration<double, std::nano>(now - last_frame_time).count();
        if (frame_elapsed_ns < frame_interval_ns) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(
                static_cast<int64_t>(frame_interval_ns - frame_elapsed_ns)));
            now = std::chrono::steady_clock::now();
        }
        last_frame_time = now;

        glfwPollEvents();

        // Update view range from data
        vnm::plot::Data_source* source = (config.data_type == "Trades")
            ? static_cast<vnm::plot::Data_source*>(trade_source.get())
            : static_cast<vnm::plot::Data_source*>(bar_source.get());

        auto snapshot_result = source->try_snapshot();
        if (snapshot_result.status == vnm::plot::snapshot_result_t::Status::OK &&
            snapshot_result.snapshot.count > 0)
        {
            const auto& snapshot = snapshot_result.snapshot;
            const auto* first_bytes = static_cast<const char*>(snapshot.data);
            const auto* last_bytes = first_bytes + (snapshot.count - 1) * snapshot.stride;

            double t_first = 0.0;
            double t_last = 0.0;

            if (config.data_type == "Trades") {
                t_first = reinterpret_cast<const vnm::benchmark::Trade_sample*>(first_bytes)->timestamp;
                t_last = reinterpret_cast<const vnm::benchmark::Trade_sample*>(last_bytes)->timestamp;
            }
            else {
                t_first = reinterpret_cast<const vnm::benchmark::Bar_sample*>(first_bytes)->timestamp;
                t_last = reinterpret_cast<const vnm::benchmark::Bar_sample*>(last_bytes)->timestamp;
            }

            t_available_min = t_first;
            t_max = t_last;
            t_min = std::max(t_first, t_last - 10.0);  // 10 second window

            if (source->has_value_range()) {
                auto [lo, hi] = source->value_range();
                float padding = (hi - lo) * 0.1f;
                if (padding < 0.01f) padding = 1.0f;
                v_min = lo - padding;
                v_max = hi + padding;
            }
        }

        // Render frame
        {
            VNM_PLOT_PROFILE_SCOPE(&profiler, "renderer");
            VNM_PLOT_PROFILE_SCOPE(&profiler, "renderer.frame");

            // GL setup (skip in no-GL mode)
            if (!config.no_gl) {
                fbo.bind();
                glEnable(GL_MULTISAMPLE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                const auto palette = vnm::plot::Color_palette::for_theme(render_config.dark_mode);
                glClearColor(palette.background.r, palette.background.g,
                             palette.background.b, palette.background.a);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            }

            // Calculate layout dimensions (matching Qt benchmark)
            double adjusted_reserved_height;
            double usable_width;
            double usable_height;
            {
                VNM_PLOT_PROFILE_SCOPE(&profiler, "renderer.frame.dimension_calc");
                adjusted_reserved_height = k_base_label_height_px + k_adjusted_preview_height;
                usable_width = fb_w - k_vbar_width_pixels;
                usable_height = fb_h - adjusted_reserved_height;
            }

            // Use Layout_calculator for proper label positions
            vnm::plot::Layout_calculator::parameters_t layout_params;
            {
                VNM_PLOT_PROFILE_SCOPE(&profiler, "renderer.frame.layout_params_setup");
                layout_params.v_min = v_min;
                layout_params.v_max = v_max;
                layout_params.t_min = t_min;
                layout_params.t_max = t_max;
                layout_params.usable_width = usable_width;
                layout_params.usable_height = usable_height;
                layout_params.vbar_width = k_vbar_width_pixels;
                layout_params.label_visible_height = usable_height + k_adjusted_preview_height;
                layout_params.adjusted_font_size_in_pixels = k_adjusted_font_px;

#if defined(VNM_PLOT_ENABLE_TEXT)
                if (text_enabled) {
                    layout_params.monospace_char_advance_px = font_renderer.monospace_advance_px();
                    layout_params.monospace_advance_is_reliable = font_renderer.monospace_advance_is_reliable();
                    layout_params.measure_text_cache_key = font_renderer.text_measure_cache_key();
                    layout_params.measure_text_func = [&font_renderer](const char* text) {
                        return font_renderer.measure_text_px(text);
                    };
                }
                else
#endif
                {
                    layout_params.monospace_char_advance_px = 0.0f;
                    layout_params.monospace_advance_is_reliable = false;
                    layout_params.measure_text_cache_key = 0;
                    layout_params.measure_text_func = [](const char* text) {
                        return static_cast<float>(std::strlen(text));
                    };
                }
                layout_params.h_label_vertical_nudge_factor = vnm::plot::detail::k_h_label_vertical_nudge_px;
                layout_params.format_timestamp_func = format_benchmark_timestamp;
                layout_params.get_required_fixed_digits_func = [](double) { return 2; };
                layout_params.profiler = &profiler;
            }

            vnm::plot::layout_cache_key_t cache_key;
            {
                VNM_PLOT_PROFILE_SCOPE(&profiler, "renderer.frame.cache_key_setup");
                cache_key.v0 = v_min;
                cache_key.v1 = v_max;
                cache_key.t0 = t_min;
                cache_key.t1 = t_max;
                cache_key.viewport_size = vnm::plot::Size2i{fb_w, fb_h};
                cache_key.adjusted_reserved_height = adjusted_reserved_height;
                cache_key.adjusted_preview_height = k_adjusted_preview_height;
                cache_key.adjusted_font_size_in_pixels = k_adjusted_font_px;
                cache_key.vbar_width_pixels = k_vbar_width_pixels;
#if defined(VNM_PLOT_ENABLE_TEXT)
                cache_key.font_metrics_key = text_enabled ? font_renderer.text_measure_cache_key() : 0;
#else
                cache_key.font_metrics_key = 0;
#endif
            }

            const vnm::plot::frame_layout_result_t* layout_ptr;
            {
                VNM_PLOT_PROFILE_SCOPE(&profiler, "renderer.frame.layout_cache_lookup");
                layout_ptr = layout_cache.try_get(cache_key);
            }
            if (!layout_ptr) {
                VNM_PLOT_PROFILE_SCOPE(&profiler, "renderer.frame.layout_cache_miss");
                auto layout_result = layout_calc.calculate(layout_params);

                vnm::plot::frame_layout_result_t layout;
                {
                    VNM_PLOT_PROFILE_SCOPE(&profiler, "renderer.frame.layout_cache_miss_store");
                    layout.usable_width = usable_width;
                    layout.usable_height = usable_height;
                    layout.v_bar_width = k_vbar_width_pixels;
                    layout.h_bar_height = k_base_label_height_px + 1.0;
                    layout.max_v_label_text_width = layout_result.max_v_label_text_width;
                    layout.v_labels = std::move(layout_result.v_labels);
                    layout.h_labels = std::move(layout_result.h_labels);
                    layout.v_label_fixed_digits = layout_result.v_label_fixed_digits;
                    layout.h_labels_subsecond = layout_result.h_labels_subsecond;
                    layout_ptr = &layout_cache.store(cache_key, std::move(layout));
                }
            }

            // Build frame context
            vnm::plot::frame_context_t ctx = [&]() {
                VNM_PLOT_PROFILE_SCOPE(&profiler, "renderer.frame.context_build");
                return vnm::plot::frame_context_t{
                    *layout_ptr,
                    v_min,
                    v_max,
                    v_min,  // preview_v0
                    v_max,  // preview_v1
                    t_min,
                    t_max,
                    t_available_min,
                    t_max,
                    fb_w,
                    fb_h,
                    glm::ortho(0.f, float(fb_w), float(fb_h), 0.f, -1.f, 1.f),
                    k_adjusted_font_px,
                    k_base_label_height_px,
                    adjusted_reserved_height,
                    k_adjusted_preview_height,
                    false,  // show_info
                    &render_config
                };
            }();

            // Always render (renderers handle skip_gl internally for CPU profiling)
            chrome_renderer.render_grid_and_backgrounds(ctx, primitives);
            series_renderer.render(ctx, series_map);
            chrome_renderer.render_preview_overlay(ctx, primitives);
            if (!config.no_gl) {
                primitives.flush_rects(ctx.pmv);
            }

            // Render text labels (when enabled)
#if defined(VNM_PLOT_ENABLE_TEXT)
            if (text_renderer && render_config.show_text) {
                text_renderer->render(ctx, false, false);
            }
#endif

            if (!config.no_gl) {
                fbo.unbind();
            }
        }

        // Swap buffers (skip in no-GL mode)
        if (!config.no_gl) {
            glfwSwapBuffers(window);
        }
        ++frame_count;

        // Progress output
        if (!config.quiet && frame_count % 60 == 0) {
            std::cout << "\rProgress: " << std::fixed << std::setprecision(1)
                      << (elapsed_seconds / config.duration_seconds * 100.0) << "%"
                      << " | Frames: " << frame_count
                      << " | Samples: " << samples_generated.load()
                      << std::flush;
        }
    }

    // Stop generator thread
    stop_generator.store(true);
    generator_thread.join();

    if (!config.quiet) {
        std::cout << "\n\n";
    }

    // Generate report
    vnm::benchmark::Report_metadata meta;
    meta.session = config.session;
    meta.symbol = config.symbol;
    meta.data_type = config.data_type;
    meta.target_duration = config.duration_seconds;
    meta.output_directory = config.output_directory;
    meta.started_at = started_at;
    meta.generated_at = std::chrono::system_clock::now();
    meta.include_extended = config.extended_metadata;
    meta.seed = config.seed;
    meta.volatility = config.volatility;
    meta.ring_capacity = config.ring_capacity;
    meta.samples_generated = samples_generated.load();

    int exit_code = k_exit_success;

    try {
        auto report_path = profiler.write_report(meta);

        double actual_duration = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - benchmark_start).count();
        double avg_fps = frame_count / actual_duration;

        if (!config.quiet) {
            std::cout << "Benchmark completed.\n"
                      << "  Frames rendered: " << frame_count << "\n"
                      << "  Average FPS: " << std::fixed << std::setprecision(1) << avg_fps << "\n"
                      << "  Samples generated: " << samples_generated.load() << "\n"
                      << "  Report written to: " << report_path.string() << "\n\n";
            std::cout << profiler.generate_report(meta);
        }
        else {
            // In quiet mode, output report path for CI parsing
            std::cout << report_path.string() << "\n";
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error writing report: " << e.what() << "\n";
        exit_code = k_exit_runtime_error;
    }

    // Cleanup GL resources BEFORE destroying context
    primitives.cleanup_gl_resources();
    series_renderer.cleanup_gl_resources();
#if defined(VNM_PLOT_ENABLE_TEXT)
    if (text_enabled) {
        vnm::plot::Font_renderer::cleanup_thread_resources();
    }
#endif
    fbo.cleanup();

    glfwDestroyWindow(window);
    glfwTerminate();

    return exit_code;
}
