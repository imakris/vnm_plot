#include "function_plotter.h"
#include <QDebug>

#include <QAudio>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QMediaDevices>

#include <glatter/glatter.h>
#include <glm/vec4.hpp>

#include <mexce.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace {

// Color palette for multiple functions
const QColor k_function_colors[] = {
    QColor(102, 178, 255),  // Light blue
    QColor(255, 128, 128),  // Light red
    QColor(128, 255, 128),  // Light green
    QColor(255, 200, 100),  // Orange
    QColor(200, 150, 255),  // Purple
    QColor(100, 255, 220),  // Cyan
    QColor(255, 180, 200),  // Pink
    QColor(200, 200, 100),  // Yellow-green
};
constexpr int k_num_colors = sizeof(k_function_colors) / sizeof(k_function_colors[0]);

std::string format_axis_scientific(double v, int digits)
{
    const int precision = std::max(0, digits);
    const double scale = std::pow(10.0, double(precision));
    double r = std::round(v * scale) / scale;
    const double eps = (precision > 0) ? (0.5 / scale) : 0.5;

    if (std::abs(r) < eps) {
        r = 0.0;
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*e", precision, r);

    std::string s = buf;
    if (!s.empty() && s[0] == '-' && r == 0.0) {
        s.erase(0, 1);
    }

    return s;
}

bool use_scientific_for_range(double range, int digits)
{
    if (!(range > 0.0)) {
        return false;
    }

    const double a = 0.0;
    const double b = range;
    const double c = -range;

    const std::string fixed_a = vnm::plot::format_axis_fixed_or_int(a, digits);
    const std::string fixed_b = vnm::plot::format_axis_fixed_or_int(b, digits);
    const std::string fixed_c = vnm::plot::format_axis_fixed_or_int(c, digits);

    const std::string sci_a = format_axis_scientific(a, digits);
    const std::string sci_b = format_axis_scientific(b, digits);
    const std::string sci_c = format_axis_scientific(c, digits);

    const std::size_t fixed_len = std::max({fixed_a.size(), fixed_b.size(), fixed_c.size()});
    const std::size_t sci_len = std::max({sci_a.size(), sci_b.size(), sci_c.size()});

    return sci_len < fixed_len;
}

constexpr int k_audio_sample_rate = 44100;

} // namespace

// ============================================================================
// Function_entry implementation
// ============================================================================

Function_entry::Function_entry(int id, const QColor& color, const QString& initial_expression, Function_plotter* plotter)
    : QObject(plotter)
    , m_plotter(plotter)
    , m_evaluator(std::make_unique<mexce::evaluator>())
    , m_series_id(id)
    , m_expression(initial_expression)
    , m_color(color)
{
    // Bind the x variable to mexce
    m_evaluator->bind(m_x, "x");

    // Create the series data
    setup_series();

    // Initialize with default expression
    compile_expression();
}

Function_entry::~Function_entry()
{
    // Remove series from plot widget if connected
    if (m_plotter && m_plotter->plot_widget()) {
        m_plotter->plot_widget()->remove_series(m_series_id);
    }
}

int Function_entry::index() const
{
    if (!m_plotter) {
        return -1;
    }

    for (int i = 0; i < m_plotter->function_count(); ++i) {
        if (m_plotter->get_function(i) == this) {
            return i;
        }
    }
    return -1;
}

void Function_entry::set_expression(const QString& expr)
{
    if (m_expression != expr) {
        m_expression = expr;
        mark_audio_stale();

        if (compile_expression()) {
            if (m_plotter) {
                generate_samples(m_plotter->x_min(), m_plotter->x_max());
            }
        }

        emit expression_changed();
    }
}

void Function_entry::set_color(const QColor& color)
{
    if (m_color != color) {
        m_color = color;
        update_series_color();
        if (m_plotter) {
            if (auto* widget = m_plotter->plot_widget()) {
                widget->update();
            }
        }
        emit color_changed();
    }
}

bool Function_entry::compile_expression()
{
    try {
        std::string expr = m_expression.toStdString();
        m_evaluator->set_expression(expr);
        m_expression_valid = true;
        clear_error();
        return true;
    }
    catch (const std::exception& e) {
        m_expression_valid = false;
        set_error(QString::fromStdString(e.what()));
        return false;
    }
    catch (...) {
        m_expression_valid = false;
        set_error("Unknown error compiling expression");
        return false;
    }
}

void Function_entry::set_num_samples(int n)
{
    n = std::max(10, std::min(1000000, n));
    if (m_num_samples != n) {
        m_num_samples = n;
        emit num_samples_changed();

        if (m_expression_valid && m_plotter) {
            generate_samples(m_plotter->x_min(), m_plotter->x_max());
        }
    }
}

bool Function_entry::show_dots() const
{
    if (!m_series) {
        return false;
    }
    return static_cast<int>(m_series->style) & static_cast<int>(vnm::plot::Display_style::DOTS);
}

bool Function_entry::show_line() const
{
    if (!m_series) {
        return true;  // Default
    }
    return static_cast<int>(m_series->style) & static_cast<int>(vnm::plot::Display_style::LINE);
}

bool Function_entry::show_area() const
{
    if (!m_series) {
        return false;
    }
    return static_cast<int>(m_series->style) & static_cast<int>(vnm::plot::Display_style::AREA);
}

void Function_entry::set_style_flag(vnm::plot::Display_style flag, bool enabled)
{
    if (!m_series) {
        return;
    }

    const int flag_int = static_cast<int>(flag);
    const bool currently_set = static_cast<int>(m_series->style) & flag_int;
    if (currently_set == enabled) {
        return;
    }

    int style = static_cast<int>(m_series->style);
    if (enabled) {
        style |= flag_int;
    }
    else {
        style &= ~flag_int;
    }
    // Ensure at least one style is active
    if (style == 0) {
        style = static_cast<int>(vnm::plot::Display_style::LINE);
    }
    m_series->style = static_cast<vnm::plot::Display_style>(style);

    if (m_plotter) {
        if (auto* widget = m_plotter->plot_widget()) {
            widget->update();
        }
    }
    emit display_style_changed();
}

void Function_entry::set_show_dots(bool v)
{
    set_style_flag(vnm::plot::Display_style::DOTS, v);
}

void Function_entry::set_show_line(bool v)
{
    set_style_flag(vnm::plot::Display_style::LINE, v);
}

void Function_entry::set_show_area(bool v)
{
    set_style_flag(vnm::plot::Display_style::AREA, v);
}

void Function_entry::generate_samples(double x_min, double x_max)
{
    if (!m_expression_valid) {
        return;
    }

    const int num_samples = m_num_samples;
    std::vector<vnm::plot::function_sample_t> samples;
    samples.reserve(static_cast<size_t>(num_samples));

    const double range = x_max - x_min;
    const double step = range / static_cast<double>(num_samples - 1);

    for (int i = 0; i < num_samples; ++i) {
        m_x = x_min + i * step;

        float y = 0.0f;
        try {
            double result = m_evaluator->evaluate();

            if (std::isfinite(result)) {
                y = static_cast<float>(result);
            }
            else {
                y = 0.0f;
            }
        }
        catch (...) {
            y = 0.0f;
        }

        samples.emplace_back(m_x, y);
    }

    // Update the data source
    m_data_source.set_data(std::move(samples));

    if (m_plotter) {
        if (auto* widget = m_plotter->plot_widget()) {
            widget->update();
        }
    }

    emit data_updated();
}

void Function_entry::mark_audio_stale()
{
    m_audio_stale = true;
    if (m_audio_sink) {
        m_audio_sink->stop();
        m_audio_sink.reset();
        set_is_playing(false);
    }
    if (m_audio_buffer) {
        m_audio_buffer->close();
        m_audio_buffer.reset();
    }
}

void Function_entry::play_sound()
{
    if (m_audio_sink) {
        const auto state = m_audio_sink->state();
        if (state == QAudio::ActiveState) {
            m_audio_sink->suspend();
            set_is_playing(false);
            return;
        }
        else if (state == QAudio::SuspendedState && !m_audio_stale) {
            m_audio_sink->resume();
            set_is_playing(true);
            return;
        }
    }

    if (!m_expression_valid && !compile_expression()) {
        return;
    }

    const double x_min = m_plotter->x_min();
    const double x_max = m_plotter->x_max();
    const double duration = x_max - x_min;
    if (!(duration > 0.0)) {
        return;
    }

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    QAudioFormat format;
    format.setSampleRate(k_audio_sample_rate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    if (!device.isFormatSupported(format)) {
        format = device.preferredFormat();
    }

    const int sample_rate = format.sampleRate();
    const int channels = format.channelCount();
    if (sample_rate <= 0 || channels <= 0) {
        return;
    }

    const std::size_t sample_count = static_cast<std::size_t>(
        std::ceil(duration * static_cast<double>(sample_rate)));
    if (sample_count == 0) {
        return;
    }

    std::vector<float> samples;
    samples.reserve(sample_count);

    float max_abs = 0.0f;
    const double step = duration / static_cast<double>(sample_count);
    double x = x_min;

    for (std::size_t i = 0; i < sample_count; ++i) {
        m_x = x;
        x += step;

        float y = 0.0f;
        try {
            const double result = m_evaluator->evaluate();
            if (std::isfinite(result)) {
                y = static_cast<float>(result);
            }
        }
        catch (...) {
            y = 0.0f;
        }

        samples.push_back(y);
        max_abs = std::max(max_abs, std::abs(y));
    }

    const float norm = (max_abs > 0.0f) ? (1.0f / max_abs) : 0.0f;

    QByteArray buffer;
    const auto sample_format = format.sampleFormat();
    const std::size_t frame_count = sample_count * static_cast<std::size_t>(channels);

    if (sample_format == QAudioFormat::Int16) {
        if (frame_count > (std::numeric_limits<int>::max() / static_cast<int>(sizeof(std::int16_t)))) {
            return;
        }
        buffer.resize(static_cast<int>(frame_count * sizeof(std::int16_t)));
        auto* out = reinterpret_cast<std::int16_t*>(buffer.data());
        for (float s : samples) {
            const float v = std::clamp(s * norm, -1.0f, 1.0f);
            const auto sv = static_cast<std::int16_t>(std::lround(v * 32767.0f));
            for (int ch = 0; ch < channels; ++ch) {
                *out++ = sv;
            }
        }
    }
    else if (sample_format == QAudioFormat::Int32) {
        if (frame_count > (std::numeric_limits<int>::max() / static_cast<int>(sizeof(std::int32_t)))) {
            return;
        }
        buffer.resize(static_cast<int>(frame_count * sizeof(std::int32_t)));
        auto* out = reinterpret_cast<std::int32_t*>(buffer.data());
        for (float s : samples) {
            const float v = std::clamp(s * norm, -1.0f, 1.0f);
            const auto sv = static_cast<std::int32_t>(std::lround(v * 2147483647.0f));
            for (int ch = 0; ch < channels; ++ch) {
                *out++ = sv;
            }
        }
    }
    else if (sample_format == QAudioFormat::Float) {
        if (frame_count > (std::numeric_limits<int>::max() / static_cast<int>(sizeof(float)))) {
            return;
        }
        buffer.resize(static_cast<int>(frame_count * sizeof(float)));
        auto* out = reinterpret_cast<float*>(buffer.data());
        for (float s : samples) {
            const float v = std::clamp(s * norm, -1.0f, 1.0f);
            for (int ch = 0; ch < channels; ++ch) {
                *out++ = v;
            }
        }
    }
    else if (sample_format == QAudioFormat::UInt8) {
        if (frame_count > (std::numeric_limits<int>::max() / static_cast<int>(sizeof(std::uint8_t)))) {
            return;
        }
        buffer.resize(static_cast<int>(frame_count * sizeof(std::uint8_t)));
        auto* out = reinterpret_cast<std::uint8_t*>(buffer.data());
        for (float s : samples) {
            const float v = std::clamp(s * norm, -1.0f, 1.0f);
            const auto sv = static_cast<std::uint8_t>(std::lround((v + 1.0f) * 127.5f));
            for (int ch = 0; ch < channels; ++ch) {
                *out++ = sv;
            }
        }
    }
    else {
        return;
    }

    if (m_audio_sink) {
        m_audio_sink->stop();
        m_audio_sink.reset();
        set_is_playing(false);
    }
    if (m_audio_buffer) {
        m_audio_buffer->close();
        m_audio_buffer.reset();
    }

    m_audio_buffer = std::make_unique<QBuffer>();
    m_audio_buffer->setData(std::move(buffer));
    m_audio_buffer->open(QIODevice::ReadOnly);

    m_audio_sink = std::make_unique<QAudioSink>(device, format);
    QObject::connect(
        m_audio_sink.get(),
        &QAudioSink::stateChanged,
        this,
        [this](QAudio::State state) {
            set_is_playing(state == QAudio::ActiveState);
        });
    m_audio_sink->start(m_audio_buffer.get());
    set_is_playing(true);
    m_audio_stale = false;
}

void Function_entry::set_error(const QString& msg)
{
    if (m_error_message != msg) {
        m_error_message = msg;
        emit error_message_changed();
    }
}

void Function_entry::clear_error()
{
    if (!m_error_message.isEmpty()) {
        m_error_message.clear();
        emit error_message_changed();
    }
}

void Function_entry::set_is_playing(bool playing)
{
    if (m_is_playing != playing) {
        m_is_playing = playing;
        emit playback_state_changed();
    }
}

void Function_entry::setup_series()
{
    m_series = std::make_shared<vnm::plot::series_data_t>();
    m_series->id = m_series_id;
    m_series->enabled = true;
    m_series->style = vnm::plot::Display_style::LINE;

    update_series_color();

    // Set up access policy for sample extraction
    m_series->access.get_timestamp = [](const void* sample) -> double {
        return static_cast<const vnm::plot::function_sample_t*>(sample)->x;
    };
    m_series->access.get_value = [](const void* sample) -> float {
        return static_cast<const vnm::plot::function_sample_t*>(sample)->y;
    };
    m_series->access.get_range = [](const void* sample) -> std::pair<float, float> {
        const auto* s = static_cast<const vnm::plot::function_sample_t*>(sample);
        return {s->y_min, s->y_max};
    };

    // Configure shaders for each display style.
    // The renderer uses shaders map directly for multi-pass rendering.
    const char* vert = ":/vnm_plot/shaders/function_sample.vert";
    m_series->shaders[vnm::plot::Display_style::DOTS] = {
        vert,
        ":/vnm_plot/shaders/plot_dot_quad.geom",
        ":/vnm_plot/shaders/plot_dot_quad.frag"
    };
    m_series->shaders[vnm::plot::Display_style::AREA] = {
        vert,
        ":/vnm_plot/shaders/plot_area.geom",
        ":/vnm_plot/shaders/plot_line.frag"
    };
    m_series->shaders[vnm::plot::Display_style::LINE] = {
        vert,
        ":/vnm_plot/shaders/plot_line_adjacency.geom",
        ":/vnm_plot/shaders/plot_line.frag"
    };

    // Layout key for function_sample_t (must be unique for this vertex layout)
    m_series->access.layout_key = 0x1001;

    // Set up vertex attributes for function_sample_t:
    // struct { double x; float y; float y_min; float y_max; }
    m_series->access.setup_vertex_attributes = []() {
        // Attribute 0: double x (uses 2 slots for dvec1)
        glVertexAttribLPointer(0, 1, GL_DOUBLE, sizeof(vnm::plot::function_sample_t),
            reinterpret_cast<void*>(offsetof(vnm::plot::function_sample_t, x)));
        glEnableVertexAttribArray(0);

        // Attribute 1: float y
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(vnm::plot::function_sample_t),
            reinterpret_cast<void*>(offsetof(vnm::plot::function_sample_t, y)));
        glEnableVertexAttribArray(1);

        // Attribute 2: float y_min
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(vnm::plot::function_sample_t),
            reinterpret_cast<void*>(offsetof(vnm::plot::function_sample_t, y_min)));
        glEnableVertexAttribArray(2);

        // Attribute 3: float y_max
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(vnm::plot::function_sample_t),
            reinterpret_cast<void*>(offsetof(vnm::plot::function_sample_t, y_max)));
        glEnableVertexAttribArray(3);
    };

    // Use m_data_source directly with no-op deleter
    m_series->data_source = std::shared_ptr<vnm::plot::Data_source>(
        &m_data_source,
        [](vnm::plot::Data_source*) {}  // No-op deleter since we don't own it
    );
}

void Function_entry::update_series_color()
{
    if (m_series) {
        m_series->color = glm::vec4(
            m_color.redF(),
            m_color.greenF(),
            m_color.blueF(),
            m_color.alphaF()
        );
    }
}

// ============================================================================
// Function_plotter implementation
// ============================================================================

Function_plotter::Function_plotter(QObject* parent)
    : QAbstractListModel(parent)
{
    // Add one default function
    add_function();
}

Function_plotter::~Function_plotter() = default;

int Function_plotter::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_entries.size());
}

QVariant Function_plotter::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_entries.size())) {
        return QVariant();
    }

    const auto& entry = m_entries[static_cast<size_t>(index.row())];

    switch (role) {
        case ExpressionRole:
            return entry->expression();
        case ColorRole:
            return entry->color();
        case HasErrorRole:
            return entry->has_error();
        case ErrorMessageRole:
            return entry->error_message();
        case IsPlayingRole:
            return entry->is_playing();
        case NumSamplesRole:
            return entry->num_samples();
        case ShowDotsRole:
            return entry->show_dots();
        case ShowLineRole:
            return entry->show_line();
        case ShowAreaRole:
            return entry->show_area();
        case EntryRole:
            return QVariant::fromValue(entry.get());
        default:
            return QVariant();
    }
}

bool Function_plotter::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_entries.size())) {
        return false;
    }

    auto& entry = m_entries[static_cast<size_t>(index.row())];

    switch (role) {
        case ExpressionRole:
            entry->set_expression(value.toString());
            emit dataChanged(index, index, {ExpressionRole, HasErrorRole, ErrorMessageRole});
            return true;
        case ColorRole:
            entry->set_color(value.value<QColor>());
            emit dataChanged(index, index, {ColorRole});
            return true;
        default:
            return false;
    }
}

QHash<int, QByteArray> Function_plotter::roleNames() const
{
    return {
        {ExpressionRole, "expression"},
        {ColorRole, "color"},
        {HasErrorRole, "hasError"},
        {ErrorMessageRole, "errorMessage"},
        {IsPlayingRole, "isPlaying"},
        {NumSamplesRole, "numSamples"},
        {ShowDotsRole, "showDots"},
        {ShowLineRole, "showLine"},
        {ShowAreaRole, "showArea"},
        {EntryRole, "entry"}
    };
}

void Function_plotter::set_x_min(double v)
{
    if (m_x_min != v) {
        m_x_min = v;
        emit range_changed();
        for (const auto& entry : m_entries) {
            entry->mark_audio_stale();
        }
        regenerate_all_samples();
        update_plot_widget();
    }
}

void Function_plotter::set_x_max(double v)
{
    if (m_x_max != v) {
        m_x_max = v;
        emit range_changed();
        for (const auto& entry : m_entries) {
            entry->mark_audio_stale();
        }
        regenerate_all_samples();
        update_plot_widget();
    }
}


void Function_plotter::set_plot_widget(vnm::plot::Plot_widget* widget)
{
    if (m_plot_widget != widget) {
        // Remove all series from old widget
        if (m_plot_widget) {
            for (const auto& entry : m_entries) {
                m_plot_widget->remove_series(entry->series_id());
            }
        }

        m_plot_widget = widget;

        // Add series to new widget
        if (m_plot_widget) {
            configure_plot_widget();

            for (const auto& entry : m_entries) {
                m_plot_widget->add_series(entry->series_id(), entry->series());
            }

            update_plot_widget();
            regenerate_all_samples();
        }

        emit plot_widget_changed();
    }
}

void Function_plotter::add_function()
{
    const int new_id = m_next_series_id++;
    const QColor color = get_next_color();
    const QString expression = get_unique_expression();

    beginInsertRows(QModelIndex(), static_cast<int>(m_entries.size()), static_cast<int>(m_entries.size()));

    auto entry = std::make_unique<Function_entry>(new_id, color, expression, this);

    // Connect signals for model updates
    connect(entry.get(), &Function_entry::expression_changed, this, [this, ptr = entry.get()]() {
        if (int idx = index_of_entry(ptr); idx >= 0) {
            QModelIndex modelIndex = index(idx);
            emit dataChanged(modelIndex, modelIndex, {ExpressionRole, HasErrorRole, ErrorMessageRole});
        }
    });

    connect(entry.get(), &Function_entry::error_message_changed, this, [this, ptr = entry.get()]() {
        if (int idx = index_of_entry(ptr); idx >= 0) {
            emit dataChanged(index(idx), index(idx), {HasErrorRole, ErrorMessageRole});
        }
    });

    connect(entry.get(), &Function_entry::color_changed, this, [this, ptr = entry.get()]() {
        if (int idx = index_of_entry(ptr); idx >= 0) {
            emit dataChanged(index(idx), index(idx), {ColorRole});
        }
    });

    connect(entry.get(), &Function_entry::playback_state_changed, this, [this, ptr = entry.get()]() {
        if (int idx = index_of_entry(ptr); idx >= 0) {
            emit dataChanged(index(idx), index(idx), {IsPlayingRole});
        }
    });

    connect(entry.get(), &Function_entry::num_samples_changed, this, [this, ptr = entry.get()]() {
        if (int idx = index_of_entry(ptr); idx >= 0) {
            emit dataChanged(index(idx), index(idx), {NumSamplesRole});
        }
    });

    connect(entry.get(), &Function_entry::display_style_changed, this, [this, ptr = entry.get()]() {
        if (int idx = index_of_entry(ptr); idx >= 0) {
            emit dataChanged(index(idx), index(idx), {ShowDotsRole, ShowLineRole, ShowAreaRole});
        }
    });

    // Add series to plot widget if connected
    if (m_plot_widget) {
        m_plot_widget->add_series(entry->series_id(), entry->series());
    }

    // Generate initial samples
    entry->generate_samples(m_x_min, m_x_max);

    m_entries.push_back(std::move(entry));

    endInsertRows();
    emit function_count_changed();
}

void Function_plotter::remove_function(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(m_entries.size())) {
        return;
    }

    // Don't allow removing the last function
    if (m_entries.size() <= 1) {
        return;
    }

    beginRemoveRows(QModelIndex(), idx, idx);

    m_entries.erase(m_entries.begin() + idx);

    endRemoveRows();
    emit function_count_changed();
}

Function_entry* Function_plotter::get_function(int idx) const
{
    if (idx < 0 || idx >= static_cast<int>(m_entries.size())) {
        return nullptr;
    }
    return m_entries[static_cast<size_t>(idx)].get();
}

void Function_plotter::load_preset(int idx)
{
    if (idx < 0 || idx >= k_num_presets) {
        return;
    }

    const auto& preset = k_function_presets[idx];
    const QString preset_expression = QString::fromLatin1(preset.expression);
    const bool limits_changed = (m_x_min != preset.x_min || m_x_max != preset.x_max);

    if (limits_changed) {
        m_x_min = preset.x_min;
        m_x_max = preset.x_max;
        emit range_changed();
        for (const auto& entry : m_entries) {
            entry->mark_audio_stale();
        }
    }

    for (auto& entry : m_entries) {
        if (entry->expression() != preset_expression) {
            entry->set_expression(preset_expression);
        }
        else if (limits_changed && entry->expression_valid()) {
            entry->generate_samples(m_x_min, m_x_max);
        }
    }

    update_plot_widget();
}

void Function_plotter::recompute_all()
{
    for (auto& entry : m_entries) {
        if (entry->compile_expression()) {
            entry->generate_samples(m_x_min, m_x_max);
        }
    }
}

void Function_plotter::regenerate_all_samples()
{
    for (auto& entry : m_entries) {
        if (entry->expression_valid()) {
            entry->generate_samples(m_x_min, m_x_max);
        }
    }
}

void Function_plotter::update_plot_widget()
{
    if (!m_plot_widget) {
        return;
    }

    // Update the time (x) range
    m_plot_widget->set_t_range(m_x_min, m_x_max);
    m_plot_widget->set_available_t_range(m_x_min, m_x_max);
}

void Function_plotter::configure_plot_widget()
{
    if (!m_plot_widget) {
        return;
    }

    vnm::plot::Plot_config config;
    config.dark_mode = true;
    config.show_text = true;
    config.font_size_px = 11.0;
    config.preview_height_px = 0;  // Use automatic preview height
    config.clear_to_transparent = true;
    config.snap_lines_to_pixels = false;
    config.line_width_px = 1.25;
    config.auto_v_range_extra_scale = 0.5;

    // Custom x-axis formatter (just show the value, not time)
    config.format_timestamp = [](double x, double range) -> std::string {
        int digits = 0;
        if (range > 0.0) {
            const double step = range / 5.0;
            digits = std::max(0, static_cast<int>(std::ceil(-std::log10(step))) + 1);
            digits = std::min(digits, 6);
        }

        const bool use_scientific = use_scientific_for_range(range, digits);
        if (use_scientific) {
            return format_axis_scientific(x, digits);
        }

        return vnm::plot::format_axis_fixed_or_int(x, digits);
    };

    m_plot_widget->set_config(config);
}

QColor Function_plotter::get_next_color()
{
    QColor color = k_function_colors[m_color_index % k_num_colors];
    m_color_index++;
    return color;
}

QString Function_plotter::get_unique_expression() const
{
    // Collect all currently used expressions
    std::vector<QString> used_expressions;
    used_expressions.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        used_expressions.push_back(entry->expression());
        qDebug() << "Existing expression:" << entry->expression();
    }

    // Try each preset expression and return the first unused one
    for (int i = 0; i < k_num_presets; ++i) {
        const QString expr = QString::fromLatin1(k_function_presets[i].expression);
        bool is_used = false;
        for (const auto& used : used_expressions) {
            if (used == expr) {
                is_used = true;
                break;
            }
        }
        if (!is_used) {
            qDebug() << "Returning unique expression:" << expr;
            return expr;
        }
    }

    // All presets used, fall back to the first preset
    qDebug() << "All presets used, falling back to sin(x)";
    return QString::fromLatin1(k_function_presets[0].expression);
}

int Function_plotter::index_of_entry(Function_entry* entry) const
{
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].get() == entry) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
