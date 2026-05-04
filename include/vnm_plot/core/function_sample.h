#pragma once

// VNM Plot Library - Function Sample Type
// A simple sample type for plotting mathematical functions y = f(x).

#include <vnm_plot/core/access_policy.h>
#include <vnm_plot/core/types.h>
#include <vnm_plot/core/vertex_layout.h>

#include <cmath>
#include <functional>
#include <vector>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// function_sample_t: Simple (x, y) pair for function plotting
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
struct function_sample_t
{
    double x;      // Independent variable (e.g., time or x-axis value)
    float  y;      // Dependent variable (function value)
    float  y_min;  // Optional: range minimum (for envelope/band display)
    float  y_max;  // Optional: range maximum

    function_sample_t() : x(0), y(0), y_min(0), y_max(0) {}
    function_sample_t(double xv, float yv) : x(xv), y(yv), y_min(yv), y_max(yv) {}

    function_sample_t(double xv, float yv, float ymin, float ymax)
    :
        x(xv),
        y(yv),
        y_min(ymin),
        y_max(ymax)
    {}
};
#pragma pack(pop)

// -----------------------------------------------------------------------------
// Function_data_source: Generate samples from a function
// -----------------------------------------------------------------------------
class Function_data_source : public Vector_data_source<function_sample_t>
{
public:
    using Function = std::function<float(double)>;

    Function_data_source() = default;

    // Generate samples by evaluating function over [x_min, x_max]
    void generate(Function fn, double x_min, double x_max, size_t num_samples)
    {
        if (num_samples < 2) {
            num_samples = 2;
        }

        std::vector<function_sample_t> samples;
        samples.reserve(num_samples);

        const double step = (x_max - x_min) / static_cast<double>(num_samples - 1);

        for (size_t i = 0; i < num_samples; ++i) {
            double x = x_min + i * step;
            float y = fn(x);

            // Handle NaN/Inf
            if (!std::isfinite(y)) {
                y = 0.0f;
            }

            samples.emplace_back(x, y);
        }

        set_data(std::move(samples));
    }

};

// -----------------------------------------------------------------------------
// Create Data_access_policy for function_sample_t
// -----------------------------------------------------------------------------
inline const Vertex_layout& function_sample_layout()
{
    static const Vertex_layout layout =
        make_standard_layout<function_sample_t>(
            &function_sample_t::x,
            &function_sample_t::y,
            &function_sample_t::y_min,
            &function_sample_t::y_max);
    return layout;
}

inline uint64_t function_sample_layout_key()
{
    static const uint64_t key = layout_key_for(function_sample_layout());
    return key;
}

inline Data_access_policy_typed<function_sample_t> make_function_sample_policy_typed()
{
    auto policy = make_access_policy<function_sample_t>(
        &function_sample_t::x,
        &function_sample_t::y,
        &function_sample_t::y_min,
        &function_sample_t::y_max);
    // function_sample_t::x is a synthetic-data convenience expressed in
    // seconds. The vnm_plot API works in int64 nanoseconds, so convert at
    // the access-policy boundary.
    constexpr double k_ns_per_second = 1.0e9;
    policy.get_timestamp = [](const function_sample_t& sample) -> std::int64_t {
        return static_cast<std::int64_t>(sample.x * k_ns_per_second);
    };
    policy.clone_with_timestamp = [](function_sample_t& dst, const function_sample_t& src,
                                     std::int64_t timestamp_ns) {
        dst = src;
        dst.x = static_cast<double>(timestamp_ns) / k_ns_per_second;
    };
    return policy;
}

} // namespace vnm::plot
