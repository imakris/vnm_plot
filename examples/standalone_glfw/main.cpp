#include <vnm_plot/vnm_plot.h>

#include <glatter/glatter.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

struct Sample
{
    double x;
    float y;
    float y_min;
    float y_max;
};

static std::vector<Sample> build_samples(double x_min, double x_max, std::size_t count)
{
    std::vector<Sample> samples;
    samples.reserve(count);

    const double span = x_max - x_min;
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(count - 1);
        const double x = x_min + t * span;
        const float y = static_cast<float>(std::sin(x));
        samples.push_back({x, y, y, y});
    }

    return samples;
}

static void glfw_error_callback(int code, const char* desc)
{
    std::cerr << "GLFW error " << code << ": " << (desc ? desc : "(null)") << "\n";
}

int main()
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, vnm::plot::k_msaa_samples);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1200, 720, "vnm_plot_core (GLFW)", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!vnm::plot::init_gl()) {
        std::cerr << "Failed to initialize glatter\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    vnm::plot::Plot_core plot_core;
    plot_core.asset_loader().set_log_callback([](const std::string& msg) {
        std::cerr << "asset_loader: " << msg << "\n";
    });
    vnm::plot::Plot_core::init_params_t init_params;
    init_params.enable_text = false;
    if (!plot_core.initialize(init_params)) {
        std::cerr << "Failed to initialize plot core\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    const double t_min = -10.0;
    const double t_max = 10.0;
    const float v_min = -1.3f;
    const float v_max = 1.3f;

    auto samples = build_samples(t_min, t_max, 2000);
    auto source = std::make_shared<vnm::plot::Vector_data_source<Sample>>(std::move(samples));

    auto policy = vnm::plot::make_access_policy<Sample>(
        &Sample::x,
        &Sample::y,
        &Sample::y_min,
        &Sample::y_max);

    auto series = vnm::plot::Series_builder()
        .enabled(true)
        .style(vnm::plot::Display_style::LINE)
        .color(vnm::plot::rgba_u8(51, 179, 230))
        .data_source(source)
        .access(policy)
        .build_shared();

    std::map<int, std::shared_ptr<const vnm::plot::series_data_t>> series_map;
    series_map[1] = series;

    vnm::plot::Plot_config config;
    config.dark_mode = true;
    config.show_text = false;
    config.snap_lines_to_pixels = false;
    config.line_width_px = 1.5;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int fb_w = 0;
        int fb_h = 0;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        if (fb_w <= 0 || fb_h <= 0) {
            continue;
        }

        glViewport(0, 0, fb_w, fb_h);

        glEnable(GL_MULTISAMPLE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        const auto palette = vnm::plot::Color_palette::for_theme(config.dark_mode);
        glClearColor(palette.background.r, palette.background.g, palette.background.b, palette.background.a);
        glClear(GL_COLOR_BUFFER_BIT);

        vnm::plot::Plot_core::render_params_t params;
        params.width = fb_w;
        params.height = fb_h;
        params.v_min = v_min;
        params.v_max = v_max;
        params.preview_v_min = v_min;
        params.preview_v_max = v_max;
        params.t_min = t_min;
        params.t_max = t_max;
        params.t_available_min = t_min;
        params.t_available_max = t_max;
        params.adjusted_font_px = 12.0;
        params.base_label_height_px = 0.0;
        params.adjusted_reserved_height = 0.0;
        params.adjusted_preview_height = 0.0;

        plot_core.render(params, series_map, &config);

        glfwSwapBuffers(window);
    }

    glfwMakeContextCurrent(window);
    plot_core.cleanup_gl_resources();

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
