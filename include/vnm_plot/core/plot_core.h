#pragma once

// VNM Plot Library - Core Facade
// High-level wrapper around the core renderers.

#include "types.h"

#include <map>
#include <memory>
#include <optional>

namespace vnm::plot {

class Asset_loader;
class Primitive_renderer;
class Series_renderer;
class Chrome_renderer;
class Text_renderer;
struct Plot_config;

class Plot_core
{
public:
    struct init_params_t
    {
        bool enable_text = true;
        bool init_embedded_assets = true;
    };

    struct render_params_t
    {
        int width = 0;
        int height = 0;

        float v_min = -1.0f;
        float v_max = 1.0f;

        double t_min = 0.0;
        double t_max = 1.0;

        std::optional<float> preview_v_min;
        std::optional<float> preview_v_max;

        std::optional<double> t_available_min;
        std::optional<double> t_available_max;

        bool show_info = false;
    };

    Plot_core();
    ~Plot_core();

    Plot_core(const Plot_core&) = delete;
    Plot_core& operator=(const Plot_core&) = delete;

    bool initialize();
    bool initialize(const init_params_t& params);
    void cleanup_gl_resources();

    Asset_loader& asset_loader();
    Primitive_renderer& primitives();
    Series_renderer& series_renderer();
    Chrome_renderer& chrome_renderer();
    Text_renderer* text_renderer();

    void render(
        const render_params_t& params,
        const std::map<int, std::shared_ptr<const series_data_t>>& series,
        const Plot_config* config);

private:
    struct impl_t;
    std::unique_ptr<impl_t> m_impl;
};

} // namespace vnm::plot
