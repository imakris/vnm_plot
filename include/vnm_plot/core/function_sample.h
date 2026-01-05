#pragma once

// VNM Plot Library - Function Sample Type
// A simple sample type for plotting mathematical functions y = f(x).

#include <vnm_plot/core/types.h>

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
    function_sample_t(double x_, float y_) : x(x_), y(y_), y_min(y_), y_max(y_) {}

    function_sample_t(double x_, float y_, float y_min_, float y_max_)
    :
        x(x_),
        y(y_),
        y_min(y_min_),
        y_max(y_max_)
    {}

    double timestamp() const { return x; }
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

    // Generate with range (for band/envelope display)
    void generate_with_range(
        Function fn_value,
        Function fn_min,
        Function fn_max,
        double x_min,
        double x_max,
        size_t num_samples)
    {
        if (num_samples < 2) {
            num_samples = 2;
        }

        std::vector<function_sample_t> samples;
        samples.reserve(num_samples);

        const double step = (x_max - x_min) / static_cast<double>(num_samples - 1);

        for (size_t i = 0; i < num_samples; ++i) {
            double x = x_min + i * step;
            float y = fn_value(x);
            float y_lo = fn_min(x);
            float y_hi = fn_max(x);

            // Handle NaN/Inf
            if (!std::isfinite(y))    { y    = 0.0f; }
            if (!std::isfinite(y_lo)) { y_lo = y;    }
            if (!std::isfinite(y_hi)) { y_hi = y;    }

            samples.emplace_back(x, y, y_lo, y_hi);
        }

        set_data(std::move(samples));
    }
};

// -----------------------------------------------------------------------------
// Create Data_access_policy for function_sample_t
// -----------------------------------------------------------------------------
inline Data_access_policy make_function_sample_policy()
{
    Data_access_policy policy;

    policy.get_timestamp = [](const void* p) {
        return static_cast<const function_sample_t*>(p)->x;
    };

    policy.get_value = [](const void* p) {
        return static_cast<const function_sample_t*>(p)->y;
    };

    policy.get_range = [](const void* p) {
        auto* s = static_cast<const function_sample_t*>(p);
        return std::make_pair(s->y_min, s->y_max);
    };

    policy.sample_stride = sizeof(function_sample_t);

    // Vertex attributes will be set up by the renderer based on sample layout
    // For now, assume simple position-based layout
    policy.layout_key = 0x1001;  // Unique key for function_sample_t layout

    return policy;
}

} // namespace vnm::plot
