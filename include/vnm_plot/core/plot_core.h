#pragma once

// VNM Plot Library - Core Facade
// High-level wrapper around the core renderers.

#include "asset_loader.h"
#include "chrome_renderer.h"
#include "plot_config.h"
#include "primitive_renderer.h"
#include "series_renderer.h"
#include "text_renderer.h"
#include "types.h"

#include <memory>
#include <map>

namespace vnm::plot {

class Font_renderer;

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
        float preview_v_min = -1.0f;
        float preview_v_max = 1.0f;

        double t_min = 0.0;
        double t_max = 1.0;
        double t_available_min = 0.0;
        double t_available_max = 1.0;

        double vbar_width_px = 0.0;
        double hbar_height_px = 0.0;
        double adjusted_font_px = 10.0;
        double base_label_height_px = 14.0;
        double adjusted_reserved_height = 0.0;
        double adjusted_preview_height = 0.0;
        bool show_info = false;
    };

    Plot_core();
    ~Plot_core();

    Plot_core(const Plot_core&) = delete;
    Plot_core& operator=(const Plot_core&) = delete;

    bool initialize();
    bool initialize(const init_params_t& params);
    void cleanup_gl_resources();

    Asset_loader& asset_loader() { return m_asset_loader; }
    Primitive_renderer& primitives() { return m_primitives; }
    Series_renderer& series_renderer() { return m_series; }
    Chrome_renderer& chrome_renderer() { return m_chrome; }
    Text_renderer* text_renderer() { return m_text.get(); }

    void render(
        const render_params_t& params,
        const std::map<int, std::shared_ptr<const series_data_t>>& series,
        const Plot_config* config);

private:
    Asset_loader m_asset_loader;
    Primitive_renderer m_primitives;
    Series_renderer m_series;
    Chrome_renderer m_chrome;
    std::unique_ptr<Font_renderer> m_fonts;
    std::unique_ptr<Text_renderer> m_text;
    int m_last_font_px = 0;
    bool m_initialized = false;
};

} // namespace vnm::plot
