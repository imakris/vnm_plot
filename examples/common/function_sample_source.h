#pragma once

#include <vnm_plot/core/access_policy.h>
#include <vnm_plot/core/types.h>

#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace vnm::plot::examples {

enum class Nonfinite_sample_policy
{
    SKIP,
    REPLACE_WITH_ZERO,
};

struct function_sample_t
{
    double x = 0.0;
    float  y = 0.0f;
    float  y_min = 0.0f;
    float  y_max = 0.0f;
};

inline std::optional<float> normalize_function_value(
    float value,
    Nonfinite_sample_policy nonfinite_policy)
{
    if (std::isfinite(value)) {
        return value;
    }

    if (nonfinite_policy == Nonfinite_sample_policy::REPLACE_WITH_ZERO) {
        return 0.0f;
    }

    return std::nullopt;
}

inline std::optional<function_sample_t> make_function_sample(
    double x,
    float y,
    Nonfinite_sample_policy nonfinite_policy = Nonfinite_sample_policy::SKIP)
{
    const auto normalized_y = normalize_function_value(y, nonfinite_policy);
    if (!normalized_y) {
        return std::nullopt;
    }

    return function_sample_t{x, *normalized_y, *normalized_y, *normalized_y};
}

class Function_data_source : public vnm::plot::Vector_data_source<function_sample_t>
{
public:
    using value_function = std::function<float(double)>;

    Function_data_source() = default;

    void generate(
        const value_function&   fn,
        double                  x_min,
        double                  x_max,
        std::size_t             num_samples,
        Nonfinite_sample_policy nonfinite_policy = Nonfinite_sample_policy::SKIP)
    {
        if (num_samples < 2) {
            num_samples = 2;
        }

        std::vector<function_sample_t> samples;
        samples.reserve(num_samples);

        const double step = (x_max - x_min) / static_cast<double>(num_samples - 1);

        for (std::size_t i = 0; i < num_samples; ++i) {
            const double x = x_min + static_cast<double>(i) * step;
            const auto sample = make_function_sample(x, fn(x), nonfinite_policy);
            if (sample) {
                samples.push_back(*sample);
            }
        }

        set_data(std::move(samples));
    }
};

inline vnm::plot::Data_access_policy_typed<function_sample_t> make_function_sample_policy_typed()
{
    return vnm::plot::make_access_policy<function_sample_t>(
        &function_sample_t::x,
        &function_sample_t::y,
        &function_sample_t::y_min,
        &function_sample_t::y_max);
}

} // namespace vnm::plot::examples
