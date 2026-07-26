// vnm_plot QQuickRhiItem renderer ownership tests

#include "test_macros.h"
#include "plot_renderer.h"

#include <vnm_plot/qt/plot_widget.h>

#include <QGuiApplication>
#include <QQuickRhiItem>

#include <iostream>
#include <memory>
#include <type_traits>

namespace plot = vnm::plot;

namespace {

static_assert(std::is_default_constructible_v<plot::Plot_renderer>);
static_assert(!std::is_constructible_v<plot::Plot_renderer, const plot::Plot_widget*>);

class test_renderer_t final : public plot::Plot_renderer
{
public:
    using plot::Plot_renderer::synchronize;
};

class test_widget_t final : public plot::Plot_widget
{
public:
    using plot::Plot_widget::createRenderer;
};

bool test_renderer_outlives_synchronized_widget()
{
    auto renderer = std::make_unique<test_renderer_t>();

    {
        auto widget = std::make_unique<plot::Plot_widget>();
        renderer->synchronize(widget.get());
    }

    renderer.reset();
    return true;
}

bool test_created_renderer_has_independent_lifetime()
{
    std::unique_ptr<QQuickRhiItemRenderer> renderer;

    {
        test_widget_t widget;
        renderer.reset(widget.createRenderer());
        TEST_ASSERT(renderer, "Plot_widget should create a renderer");
    }

    renderer.reset();
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    std::cout << "Plot renderer lifecycle tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_renderer_outlives_synchronized_widget);
    RUN_TEST(test_created_renderer_has_independent_lifetime);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
