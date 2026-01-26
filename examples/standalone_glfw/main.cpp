#include <vnm_plot/vnm_plot.h>

#include <glatter/glatter.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#ifdef Status
#undef Status
#endif

struct Sample
{
    double x;
    float y;
    float y_min;
    float y_max;
};

class Vector_source final : public vnm::plot::Data_source
{
public:
    explicit Vector_source(std::vector<Sample> samples)
        : m_samples(std::move(samples))
    {
    }

    vnm::plot::snapshot_result_t try_snapshot(std::size_t /*lod_level*/ = 0) override
    {
        vnm::plot::snapshot_result_t res;
        res.snapshot.data = m_samples.data();
        res.snapshot.count = m_samples.size();
        res.snapshot.stride = sizeof(Sample);
        res.snapshot.sequence = m_sequence;
        res.status = m_samples.empty()
            ? vnm::plot::snapshot_result_t::Status::EMPTY
            : vnm::plot::snapshot_result_t::Status::READY;
        return res;
    }

    std::size_t sample_stride() const override
    {
        return sizeof(Sample);
    }

    const void* identity() const override
    {
        return this;
    }

private:
    std::vector<Sample> m_samples;
    std::uint64_t m_sequence = 1;
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

static void setup_vertex_attributes()
{
    glVertexAttribLPointer(0, 1, GL_DOUBLE, sizeof(Sample),
        reinterpret_cast<void*>(offsetof(Sample, x)));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Sample),
        reinterpret_cast<void*>(offsetof(Sample, y)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Sample),
        reinterpret_cast<void*>(offsetof(Sample, y_min)));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Sample),
        reinterpret_cast<void*>(offsetof(Sample, y_max)));
    glEnableVertexAttribArray(3);
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

    vnm::plot::Asset_loader asset_loader;
    asset_loader.set_log_callback([](const std::string& msg) {
        std::cerr << "asset_loader: " << msg << "\n";
    });
    vnm::plot::init_embedded_assets(asset_loader);

    vnm::plot::Primitive_renderer primitives;
    vnm::plot::Series_renderer series_renderer;
    vnm::plot::Chrome_renderer chrome_renderer;

    if (!primitives.initialize(asset_loader)) {
        std::cerr << "Failed to initialize primitives\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    series_renderer.initialize(asset_loader);

    const double t_min = -10.0;
    const double t_max = 10.0;
    const float v_min = -1.3f;
    const float v_max = 1.3f;

    auto samples = build_samples(t_min, t_max, 2000);
    auto source = std::make_shared<Vector_source>(std::move(samples));

    auto series = std::make_shared<vnm::plot::series_data_t>();
    series->id = 1;
    series->enabled = true;
    series->style = vnm::plot::Display_style::LINE;
    series->color = glm::vec4(0.2f, 0.7f, 0.9f, 1.0f);
    series->data_source = source;
    series->shader_set = {
        "shaders/function_sample.vert",
        "shaders/plot_line_adjacency.geom",
        "shaders/plot_line.frag"
    };

    series->access.get_timestamp = [](const void* sample) -> double {
        return static_cast<const Sample*>(sample)->x;
    };
    series->access.get_value = [](const void* sample) -> float {
        return static_cast<const Sample*>(sample)->y;
    };
    series->access.get_range = [](const void* sample) -> std::pair<float, float> {
        const auto* s = static_cast<const Sample*>(sample);
        return {s->y_min, s->y_max};
    };
    series->access.sample_stride = sizeof(Sample);
    series->access.layout_key = 0x1001;
    series->access.setup_vertex_attributes = setup_vertex_attributes;

    std::map<int, std::shared_ptr<vnm::plot::series_data_t>> series_map;
    series_map[series->id] = series;

    vnm::plot::Render_config config;
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

        vnm::plot::frame_layout_result_t layout;
        layout.usable_width = fb_w;
        layout.usable_height = fb_h;
        layout.v_bar_width = 0.0;
        layout.h_bar_height = 0.0;

        vnm::plot::frame_context_t ctx{
            layout,
            v_min,
            v_max,
            v_min,
            v_max,
            t_min,
            t_max,
            t_min,
            t_max,
            fb_w,
            fb_h,
            glm::ortho(0.f, float(fb_w), float(fb_h), 0.f, -1.f, 1.f),
            12.0,
            0.0,
            0.0,
            0.0,
            false,
            &config
        };

        chrome_renderer.render_grid_and_backgrounds(ctx, primitives);
        series_renderer.render(ctx, series_map);
        chrome_renderer.render_preview_overlay(ctx, primitives);
        primitives.flush_rects(ctx.pmv);

        glfwSwapBuffers(window);
    }

    glfwMakeContextCurrent(window);
    series_renderer.cleanup_gl_resources();
    primitives.cleanup_gl_resources();

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
