// vnm_plot QQuickRhiItem renderer ownership tests

#include "test_macros.h"
#include "plot_render_feedback.h"
#include "plot_renderer.h"

#include <vnm_plot/qt/plot_widget.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQuickRhiItem>
#include <QQuickWindow>
#include <QSemaphore>
#include <QSGRendererInterface>
#include <QThread>

#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <type_traits>

namespace plot = vnm::plot;

namespace {

static_assert(std::is_default_constructible_v<plot::Plot_renderer>);
static_assert(!std::is_constructible_v<plot::Plot_renderer, const plot::Plot_widget*>);

constexpr int k_scene_graph_timeout_ms = 10000;

struct destruction_state_t
{
    QSemaphore        second_render_entered;
    QSemaphore        continue_second_render;
    std::atomic<bool> destroyed{false};
    std::atomic<bool> feedback_visible_at_destruction{false};
    std::atomic<bool> second_render_completed{false};
    std::atomic<bool> second_render_timed_out{false};
    std::atomic<bool> deletion_timed_out{false};
    std::atomic<int>  render_callbacks{0};
    std::atomic<int>  frames_completed{0};
};

class test_renderer_t final : public plot::Plot_renderer
{
public:
    using plot::Plot_renderer::synchronize;
};

class test_widget_t : public plot::Plot_widget
{
public:
    test_widget_t() = default;

    explicit test_widget_t(std::shared_ptr<destruction_state_t> destruction_state)
        : m_destruction_state(std::move(destruction_state))
    {}

    ~test_widget_t() override
    {
        if (!m_destruction_state) {
            return;
        }

        float  v_min = 0.0f;
        float  v_max = 0.0f;
        qint64 t_min = 0;
        qint64 t_max = 0;
        m_destruction_state->feedback_visible_at_destruction.store(
            rendered_v_range(v_min, v_max) || rendered_t_range(t_min, t_max),
            std::memory_order_release);
        m_destruction_state->destroyed.store(true, std::memory_order_release);
    }

    using plot::Plot_widget::createRenderer;

    bool rendered_feedback(
        float&  v_min,
        float&  v_max,
        qint64& t_min,
        qint64& t_max) const
    {
        return
            rendered_v_range(v_min, v_max) &&
            rendered_t_range(t_min, t_max);
    }

private:
    std::shared_ptr<destruction_state_t> m_destruction_state;
};

class gated_renderer_t final : public plot::Plot_renderer
{
public:
    explicit gated_renderer_t(std::shared_ptr<destruction_state_t> state)
        : m_state(std::move(state))
    {}

protected:
    void render(QRhiCommandBuffer* cb) override
    {
        const int callback =
            m_state->render_callbacks.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (callback == 2) {
            m_state->second_render_entered.release();
            if (!m_state->continue_second_render.tryAcquire(
                    1,
                    k_scene_graph_timeout_ms))
            {
                m_state->second_render_timed_out.store(true, std::memory_order_release);
            }
        }

        plot::Plot_renderer::render(cb);

        if (callback == 1) {
            update();
        }
        else if (callback == 2) {
            m_state->second_render_completed.store(true, std::memory_order_release);
        }
    }

private:
    std::shared_ptr<destruction_state_t> m_state;
};

class gated_widget_t final : public test_widget_t
{
public:
    explicit gated_widget_t(std::shared_ptr<destruction_state_t> state)
        :
        test_widget_t(state),
        m_state(std::move(state))
    {}

    QQuickRhiItemRenderer* createRenderer() override
    {
        return new gated_renderer_t(m_state);
    }

private:
    std::shared_ptr<destruction_state_t> m_state;
};

bool wait_until(const std::function<bool()>& predicate, int timeout_ms)
{
    QElapsedTimer elapsed;
    elapsed.start();

    while (!predicate() && elapsed.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

void configure_static_widget(test_widget_t& widget, QQuickWindow& window)
{
    widget.setParentItem(window.contentItem());
    widget.setWidth(window.width());
    widget.setHeight(window.height());
    widget.set_v_auto(false);
    widget.set_v_range(-4.0f, 9.0f);
    widget.set_available_t_range(1000, 9000);
    widget.set_t_range(2000, 7000);
}

bool test_feedback_channel_respects_completed_frame_boundary()
{
    auto channel = std::make_shared<plot::detail::plot_render_feedback_channel_t>();

    plot::detail::plot_render_feedback_t first;
    first.v_min = 11.0f;
    channel->publish(std::move(first));
    channel->mark_latest_completed();

    plot::detail::plot_render_feedback_t second;
    second.v_min = 22.0f;
    channel->publish(std::move(second));

    auto completed = plot::detail::take_completed_feedback(channel, channel, 0);
    TEST_ASSERT(completed && completed->generation == 1 && completed->v_min == 11.0f,
        "frame N delivery must consume frame N's completed generation");

    completed = plot::detail::take_completed_feedback(channel, channel, 1);
    TEST_ASSERT(!completed,
        "published frame N+1 feedback must wait for its own completion marker");

    channel->mark_latest_completed();
    completed = plot::detail::take_completed_feedback(channel, channel, 1);
    TEST_ASSERT(completed && completed->generation == 2 && completed->v_min == 22.0f,
        "frame N+1 feedback should become consumable after its frame boundary");
    return true;
}

bool test_replaced_feedback_channel_ignores_old_callback()
{
    auto old_channel =
        std::make_shared<plot::detail::plot_render_feedback_channel_t>();
    auto current_channel =
        std::make_shared<plot::detail::plot_render_feedback_channel_t>();

    plot::detail::plot_render_feedback_t old_feedback;
    old_feedback.v_min = 17.0f;
    old_channel->publish(std::move(old_feedback));
    old_channel->mark_latest_completed();

    auto completed = plot::detail::take_completed_feedback(
        current_channel,
        old_channel,
        0);
    TEST_ASSERT(!completed,
        "an old renderer callback must not consume a replacement channel");

    completed = plot::detail::take_completed_feedback(old_channel, old_channel, 0);
    TEST_ASSERT(completed && completed->generation == 1 && completed->v_min == 17.0f,
        "rejecting an old callback must leave its channel generation untouched");
    return true;
}

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

bool test_static_first_frame_delivers_feedback()
{
    QQuickWindow window;
    window.resize(480, 320);

    auto widget = std::make_unique<test_widget_t>();
    configure_static_widget(*widget, window);
    window.show();

    float  v_min = 0.0f;
    float  v_max = 0.0f;
    qint64 t_min = 0;
    qint64 t_max = 0;
    const bool delivered = wait_until(
        [&] {
            return widget->rendered_feedback(v_min, v_max, t_min, t_max);
        },
        k_scene_graph_timeout_ms);

    TEST_ASSERT(delivered,
        "a static item's first frame should deliver render feedback");
    TEST_ASSERT(v_min == -4.0f && v_max == 9.0f,
        "first-frame feedback should preserve the rendered manual value range");
    TEST_ASSERT(t_min == 2000 && t_max == 7000,
        "first-frame feedback should preserve the rendered time range");

    widget.reset();
    window.close();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return true;
}

bool test_destroy_before_renderer_only_frame_cancels_feedback_delivery()
{
    QQuickWindow window;
    window.resize(480, 320);

    QObject gui_context;
    auto state  = std::make_shared<destruction_state_t>();
    auto widget = new gated_widget_t(state);
    configure_static_widget(*widget, window);

    QObject::connect(
        &window,
        &QQuickWindow::afterRendering,
        &window,
        [state, widget, &gui_context] {
            const int frame =
                state->frames_completed.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (frame != 1) {
                return;
            }

            QMetaObject::invokeMethod(
                &gui_context,
                [state, widget] {
                    if (!state->second_render_entered.tryAcquire(
                            1,
                            k_scene_graph_timeout_ms))
                    {
                        state->deletion_timed_out.store(true, std::memory_order_release);
                        state->continue_second_render.release();
                        return;
                    }

                    delete widget;
                    state->continue_second_render.release();
                },
                Qt::QueuedConnection);
        },
        Qt::DirectConnection);

    window.show();
    const bool lifecycle_completed = wait_until(
        [&] {
            return
                state->destroyed.load(std::memory_order_acquire) &&
                state->second_render_completed.load(std::memory_order_acquire);
        },
        k_scene_graph_timeout_ms);

    TEST_ASSERT(lifecycle_completed,
        "renderer-only frame should finish after GUI-thread item destruction");
    TEST_ASSERT(!state->deletion_timed_out.load(std::memory_order_acquire),
        "GUI thread should delete the item after the renderer-only frame enters");
    TEST_ASSERT(!state->second_render_timed_out.load(std::memory_order_acquire),
        "renderer-only frame should resume after item destruction");
    TEST_ASSERT(state->render_callbacks.load(std::memory_order_acquire) >= 2,
        "lifecycle regression must execute render after item destruction");
    TEST_ASSERT(!state->feedback_visible_at_destruction.load(std::memory_order_acquire),
        "feedback must not run before the queued owner-thread delivery point");

    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    window.close();
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QSG_RENDER_LOOP", QByteArrayLiteral("threaded"));
    QGuiApplication app(argc, argv);
#if defined(Q_OS_WIN)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif

    std::cout << "Plot renderer lifecycle tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_feedback_channel_respects_completed_frame_boundary);
    RUN_TEST(test_replaced_feedback_channel_ignores_old_callback);
    RUN_TEST(test_renderer_outlives_synchronized_widget);
    RUN_TEST(test_created_renderer_has_independent_lifetime);
    RUN_TEST(test_static_first_frame_delivers_feedback);
    RUN_TEST(test_destroy_before_renderer_only_frame_cancels_feedback_delivery);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
