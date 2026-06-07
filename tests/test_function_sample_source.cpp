// vnm_plot example function sample source tests

#include "test_macros.h"

#include "function_sample_source.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace plot_examples = vnm::plot::examples;
namespace plot = vnm::plot;

namespace {

bool test_make_function_sample_skips_nonfinite_by_default()
{
    const auto finite_sample = plot_examples::make_function_sample(1.0, 2.5f);
    TEST_ASSERT(finite_sample,
        "finite function value should create a sample");
    TEST_ASSERT(finite_sample->x == 1.0 && finite_sample->y == 2.5f,
        "finite function value should be preserved");

    const auto nan_sample = plot_examples::make_function_sample(
        1.0,
        std::numeric_limits<float>::quiet_NaN());
    TEST_ASSERT(!nan_sample,
        "NaN function value should be skipped by default");

    const auto inf_sample = plot_examples::make_function_sample(
        1.0,
        std::numeric_limits<float>::infinity());
    TEST_ASSERT(!inf_sample,
        "infinite function value should be skipped by default");

    return true;
}

bool test_make_function_sample_can_replace_nonfinite_with_zero()
{
    const auto sample = plot_examples::make_function_sample(
        1.0,
        std::numeric_limits<float>::infinity(),
        plot_examples::Nonfinite_sample_policy::REPLACE_WITH_ZERO);

    TEST_ASSERT(sample,
        "replace-with-zero policy should keep a non-finite generated sample");
    TEST_ASSERT(sample->y == 0.0f && sample->y_min == 0.0f && sample->y_max == 0.0f,
        "replace-with-zero policy should write an explicit zero sample");

    return true;
}

bool test_function_data_source_skips_nonfinite_generated_values()
{
    plot_examples::Function_data_source source;
    source.generate(
        [](double x) {
            return x < 1.5
                ? static_cast<float>(x)
                : std::numeric_limits<float>::quiet_NaN();
        },
        0.0,
        3.0,
        4);

    const auto snapshot = source.try_snapshot(0);
    TEST_ASSERT(snapshot.status == plot::snapshot_result_t::Snapshot_status::READY,
        "default generated function source should publish a ready snapshot");
    TEST_ASSERT(snapshot.snapshot.count == 2,
        "default generated function source should skip non-finite samples");
    TEST_ASSERT(snapshot.snapshot.hold,
        "function source snapshot should retain the generated vector payload");

    const auto* first = reinterpret_cast<const plot_examples::function_sample_t*>(
        snapshot.snapshot.at(0));
    const auto* second = reinterpret_cast<const plot_examples::function_sample_t*>(
        snapshot.snapshot.at(1));
    TEST_ASSERT(first != nullptr && second != nullptr,
        "generated function samples should be readable from the snapshot");
    TEST_ASSERT(first->x == 0.0 && first->y == 0.0f,
        "first finite generated sample should be preserved");
    TEST_ASSERT(second->x == 1.0 && second->y == 1.0f,
        "second finite generated sample should be preserved");

    return true;
}

} // namespace

int main()
{
    std::cout << "Function sample source tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_make_function_sample_skips_nonfinite_by_default);
    RUN_TEST(test_make_function_sample_can_replace_nonfinite_with_zero);
    RUN_TEST(test_function_data_source_skips_nonfinite_generated_values);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
