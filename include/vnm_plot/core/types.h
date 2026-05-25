#pragma once
// VNM Plot Library - Core Types
// Qt-free types used by the core renderer and data interface.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

// Forward declarations for the RHI types frame_context_t carries. These
// live behind Qt6::GuiPrivate; consumers of this public header see only
// the names so the core renderer code can route uploads and draws through
// QRhi without leaking Qt private headers.
class QRhi;
class QRhiCommandBuffer;
class QRhiRenderTarget;
class QRhiResourceUpdateBatch;

namespace vnm::plot {
struct Plot_config;
class Qrhi_series_layer;
// -----------------------------------------------------------------------------
// Size_2i - Replacement for QSize
// -----------------------------------------------------------------------------
struct Size_2i
{
    int width  = 0;
    int height = 0;

    constexpr Size_2i() = default;
    constexpr Size_2i(int w, int h) : width(w), height(h) {}

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return width > 0 && height > 0;
    }

    [[nodiscard]] constexpr bool operator==(const Size_2i& other) const noexcept
    {
        return width == other.width && height == other.height;
    }
};

// -----------------------------------------------------------------------------
// Byte Buffer - Lightweight replacement for QByteArray
// -----------------------------------------------------------------------------
// Using std::string as byte buffer: has SSO, move semantics, and works well
// with file I/O. std::vector<char> is an alternative but std::string is more
// convenient for text-based asset formats (shaders, JSON).
using Byte_buffer = std::string;
using Byte_view = std::string_view;

// -----------------------------------------------------------------------------
// data_snapshot_t: A view of sample data (optionally split into two segments)
// -----------------------------------------------------------------------------
// Represents a snapshot of data that can be safely read. The data pointer
// points to a contiguous array of samples, each `stride` bytes apart.
// If data2/count2 is set, the logical snapshot is split into two contiguous
// segments (e.g., ring buffer wrap). The total count is `count`.
// `sequence` is a monotonic counter that increments on data changes.
//
// Lifetime contract:
// - The Data_source implementation decides whether the pointers refer to a
//   copy it owns, or a direct view into its live storage.
// - Whatever guarantees the view's validity (an internal buffer, a lock, a
//   reference count) must be kept alive via `hold`; the snapshot is safe to
//   read for exactly as long as the caller keeps `hold` alive.
// - Consumers must not cache `data`/`data2` beyond the lifetime of the
//   snapshot unless they also retain `hold`.
struct data_snapshot_t
{
    const void* data     = nullptr;  ///< Pointer to first sample
    size_t      count    = 0;        ///< Number of samples
    size_t      stride   = 0;        ///< Bytes between consecutive samples
    uint64_t    sequence = 0;        ///< Change counter for cache invalidation
    const void* data2    = nullptr;  ///< Optional second segment (wrap)
    size_t      count2   = 0;        ///< Samples in second segment
    std::shared_ptr<void> hold;      ///< Optional ownership/lock guard

    explicit operator bool() const { return data != nullptr && count > 0; }

    bool is_valid() const noexcept
    {
        return data != nullptr && count > 0 && stride > 0;
    }

    const void* at(size_t index) const
    {
        if (index >= count || stride == 0) {
            return nullptr;
        }
        if (count2 > count) {
            return nullptr;
        }
        const size_t count1 = count - count2;
        if (index < count1) {
            return static_cast<const char*>(data) + index * stride;
        }
        if (!data2 || count2 == 0) {
            return nullptr;
        }
        return static_cast<const char*>(data2) + (index - count1) * stride;
    }

    size_t count1() const { return count - count2; }

    bool is_segmented() const { return data2 != nullptr && count2 > 0; }
};

struct snapshot_result_t
{
    data_snapshot_t snapshot;
    enum class Snapshot_status { READY, EMPTY, BUSY, FAILED } status = Snapshot_status::READY;

    explicit operator bool() const { return status == Snapshot_status::READY && snapshot; }
};

// -----------------------------------------------------------------------------
// Data_source: Abstract interface for data sources
// -----------------------------------------------------------------------------
class Data_source
{
public:
    virtual ~Data_source() = default;

    // Data_source decides whether snapshots are copied or direct views.
    // Callers that want buffering must implement it in their Data_source.
    virtual snapshot_result_t try_snapshot(size_t lod_level = 0) = 0;

    data_snapshot_t snapshot(size_t lod_level = 0)
    {
        auto result = try_snapshot(lod_level);
        return result.status == snapshot_result_t::Snapshot_status::READY ? result.snapshot : data_snapshot_t{};
    }

    virtual size_t lod_levels() const { return 1; }
    virtual size_t lod_scale(size_t level) const { (void)level; return 1; }
    virtual size_t sample_stride() const = 0;
    virtual const void* identity() const { return this; }

    /// Returns current sequence for the given LOD level without taking a snapshot.
    /// Useful for skip optimization when data hasn't changed.
    /// Returns 0 if not supported or unknown.
    virtual uint64_t current_sequence(size_t lod_level = 0) const { (void)lod_level; return 0; }

    // Optional value range interface for O(1) range queries.
    virtual bool has_value_range() const { return false; }
    virtual std::pair<float, float> value_range() const { return {0.0f, 0.0f}; }
    virtual bool value_range_needs_rescan() const { return false; }
    /// Query v-range for samples within [t_min_ns, t_max_ns]. Timestamps
    /// are int64_t nanoseconds (by API convention).
    /// Returns false if unsupported, no samples are in range, or a consistent snapshot
    /// cannot be obtained without blocking.
    /// Thread-safe; called from the render thread.
    virtual bool query_v_range_for_t_window(
        std::int64_t t_min_ns,
        std::int64_t t_max_ns,
        float& v_min_out,
        float& v_max_out,
        uint64_t* out_sequence = nullptr) const
    {
        (void)t_min_ns;
        (void)t_max_ns;
        (void)v_min_out;
        (void)v_max_out;
        (void)out_sequence;
        return false;
    }

};

// -----------------------------------------------------------------------------
// Data_source_ref: Explicit owning/non-owning data source handle
// -----------------------------------------------------------------------------
struct Data_source_ref
{
    std::shared_ptr<Data_source> owner;
    Data_source* ptr = nullptr;

    Data_source_ref() = default;
    explicit Data_source_ref(std::shared_ptr<Data_source> source)
    {
        set(std::move(source));
    }
    explicit Data_source_ref(Data_source& source)
    {
        set_ref(source);
    }

    void set(std::shared_ptr<Data_source> source)
    {
        owner = std::move(source);
        ptr = owner.get();
    }

    void set_ref(Data_source& source)
    {
        owner.reset();
        ptr = &source;
    }

    void reset()
    {
        owner.reset();
        ptr = nullptr;
    }

    Data_source* get() const { return ptr; }

    explicit operator bool() const { return ptr != nullptr; }

    Data_source_ref& operator=(std::shared_ptr<Data_source> source)
    {
        set(std::move(source));
        return *this;
    }
};

// -----------------------------------------------------------------------------
// Vector_data_source: Simple vector-backed data source
// -----------------------------------------------------------------------------
template<typename T>
class Vector_data_source : public Data_source
{
public:
    Vector_data_source() = default;

    explicit Vector_data_source(std::vector<T> data)
        : m_data(std::move(data))
    {}

    snapshot_result_t try_snapshot(size_t /*lod_level*/ = 0) override
    {
        return {
            data_snapshot_t{
                m_data.data(),
                m_data.size(),
                sizeof(T),
                m_sequence,
                nullptr,
                0,
                {}
            },
            m_data.empty() ? snapshot_result_t::Snapshot_status::EMPTY : snapshot_result_t::Snapshot_status::READY
        };
    }

    uint64_t current_sequence(size_t /*lod_level*/ = 0) const override { return m_sequence; }

    size_t sample_stride() const override { return sizeof(T); }

    void set_data(std::vector<T> data)
    {
        m_data = std::move(data);
        ++m_sequence;
    }

    std::vector<T>&       data()       { return m_data; }
    const std::vector<T>& data() const { return m_data; }

    /// Call after modifying m_data directly (without set_data) to update sequence.
    void notify_changed() { ++m_sequence; }
    uint64_t sequence() const { return m_sequence; }

private:
    std::vector<T> m_data;
    uint64_t       m_sequence = 0;
};

// -----------------------------------------------------------------------------
// Data_access_policy: How to extract values from samples
// -----------------------------------------------------------------------------
// Defines how the renderer extracts meaningful values from opaque sample data.
// This enables rendering of arbitrary sample types without template explosion.
struct Data_access_policy
{
    // --- Sample value extraction ---
    // Values narrow to float before upload. For large-biased signals, subtract
    // the bias inside the accessor so the remaining dynamic range survives.
    // Timestamps are int64_t nanoseconds (by API convention; the unit is the
    // accessor's contract with vnm_plot).
    std::function<std::int64_t(const void* sample)>             get_timestamp;  ///< Extract timestamp (ns)
    std::function<float(const void* sample)>                    get_value;      ///< Extract primary value
    std::function<std::pair<float, float>(const void* sample)>  get_range;      ///< Extract min/max range

    // Optional sample cloning with timestamp rewrite, used for render-only hold-forward paths.
    // Caller owns dst_sample storage; implementation writes one full sample there.
    std::function<void(void* dst_sample, const void* src_sample, std::int64_t timestamp_ns)> clone_with_timestamp;

    uint64_t layout_key = 0;  ///< Cache key distinguishing user sample types in renderer-internal caches

    bool is_valid() const
    {
        return get_timestamp && (get_value || get_range);
    }
};

// -----------------------------------------------------------------------------
// Display Styles (bit flags for combination)
// -----------------------------------------------------------------------------
enum class Display_style : int
{
    DOTS           = 0x1,
    LINE           = 0x2,
    AREA           = 0x4,
    DOTS_LINE      = DOTS | LINE,
    DOTS_AREA      = DOTS | AREA,
    LINE_AREA      = LINE | AREA,
    DOTS_LINE_AREA = DOTS | LINE | AREA
};

inline Display_style operator|(Display_style a, Display_style b)
{
    return static_cast<Display_style>(static_cast<int>(a) | static_cast<int>(b));
}

inline Display_style operator&(Display_style a, Display_style b)
{
    return static_cast<Display_style>(static_cast<int>(a) & static_cast<int>(b));
}

inline bool operator!(Display_style s)
{
    return static_cast<int>(s) == 0;
}

constexpr int k_visible_info_none        = 0x0;
constexpr int k_visible_info_value_range = 0x1;
constexpr int k_visible_info_time_range  = 0x2;
constexpr int k_visible_info_all         = k_visible_info_value_range | k_visible_info_time_range;

enum class Empty_window_behavior
{
    DRAW_NOTHING,
    HOLD_LAST_FORWARD
};

// -----------------------------------------------------------------------------
// Preview Configuration
// -----------------------------------------------------------------------------
struct preview_config_t
{
    Data_source_ref data_source;              // required when preview_config is set
    Data_access_policy access;                  // optional; if invalid, fall back to main access
    std::optional<Display_style> style;         // nullopt means use main style
};

// -----------------------------------------------------------------------------
// Data Configuration
// -----------------------------------------------------------------------------
struct data_config_t
{
    float  v_min           = -1.f;
    float  v_max           = 1.f;
    float  v_manual_min    = 0.f;
    float  v_manual_max    = 5.f;

    // Timestamps are int64_t nanoseconds (API convention). The defaults
    // describe a 10-second view starting at 0 ns; every Plot_widget user
    // is expected to call set_view (or attach a configured Plot_time_axis)
    // before the first paint, but if neither happens the widget renders a
    // sane 10-second window instead of a 5-microsecond one. The previous
    // 5000 / 10000 / 0 / 10000 literals were carried over from a pre-int64
    // era when the unit was seconds and described a 10000-second view.
    std::int64_t t_min           = 0;
    std::int64_t t_max           = std::int64_t{10} * 1'000'000'000;
    std::int64_t t_available_min = 0;
    std::int64_t t_available_max = std::int64_t{10} * 1'000'000'000;

    double vbar_width      = 150.;
};

// -----------------------------------------------------------------------------
// series_data_t: Unified series descriptor using Data_access_policy
// -----------------------------------------------------------------------------
struct series_data_t
{
    bool enabled = true;
    Display_style style = Display_style::LINE;
    Empty_window_behavior empty_window_behavior = Empty_window_behavior::DRAW_NOTHING;
    glm::vec4 color = glm::vec4(0.16f, 0.45f, 0.64f, 1.0f);
    std::string series_label;

    Data_source_ref data_source;
    Data_access_policy access;
    // Optional per-series preview configuration. When set, preview rendering can
    // use a distinct data source, access policy, and style.
    std::optional<preview_config_t> preview_config;

    std::vector<std::shared_ptr<const Qrhi_series_layer>> qrhi_layers;

    std::int64_t get_timestamp(const void* sample) const
    {
        return access.get_timestamp ? access.get_timestamp(sample) : std::int64_t{0};
    }

    float get_value(const void* sample) const
    {
        return access.get_value ? access.get_value(sample) : 0.0f;
    }

    std::pair<float, float> get_range(const void* sample) const
    {
        return access.get_range ? access.get_range(sample) : std::make_pair(0.0f, 0.0f);
    }

    void set_data_source(std::shared_ptr<Data_source> source)
    {
        data_source.set(std::move(source));
    }

    void set_data_source_ref(Data_source& source)
    {
        data_source.set_ref(source);
    }

    Data_source* main_source() const
    {
        return data_source.get();
    }

    // Returns preview source; null means preview is skipped.
    Data_source* preview_source() const
    {
        if (!preview_config) {
            return data_source.get();
        }
        return preview_config->data_source.get();
    }

    // Main access policy.
    const Data_access_policy& main_access() const
    {
        return access;
    }

    // Preview access policy (falls back to main when invalid).
    const Data_access_policy& preview_access() const
    {
        if (preview_config && preview_config->access.is_valid()) {
            return preview_config->access;
        }
        return access;
    }

    // Preview style (falls back to main when unset).
    Display_style effective_preview_style() const
    {
        if (preview_config && preview_config->style) {
            return *preview_config->style;
        }
        return style;
    }

    bool has_preview_config() const { return preview_config.has_value(); }

    bool preview_matches_main() const
    {
        if (!preview_config) {
            return true;
        }
        Data_source* prev = preview_source();
        if (!data_source || !prev || data_source.get() != prev) {
            return false;
        }
        return preview_access().layout_key == access.layout_key
            && effective_preview_style() == style;
    }

};

// -----------------------------------------------------------------------------
// Grid and Layout Types
// -----------------------------------------------------------------------------
struct grid_layer_params_t
{
    static constexpr int k_max_levels = 32;
    int   count = 0;
    float spacing_px[k_max_levels]   = {};
    float start_px[k_max_levels]     = {};
    float alpha[k_max_levels]        = {};
    float thickness_px[k_max_levels] = {};
};

/// Vertical axis label (value bar on the right side of plot)
struct v_label_t
{
    double      value;  ///< Data value this label represents
    float       y;      ///< Y position in pixels (from bottom)
    std::string text;   ///< Formatted label text
};

/// Horizontal axis label (time bar below plot)
struct h_label_t
{
    std::int64_t value;     ///< Timestamp this label represents (nanoseconds)
    glm::vec2    position;  ///< Position in pixels (x, y from bottom-left)
    std::string  text;      ///< Formatted label text
};

/// Result of layout calculation for a single frame.
/// Contains computed dimensions and pre-positioned labels.
struct frame_layout_result_t
{
    double usable_width            = 0.0;  ///< Plot area width in pixels
    double usable_height           = 0.0;  ///< Plot area height in pixels
    double v_bar_width             = 0.0;
    double h_bar_height            = 0.0;
    float  max_v_label_text_width  = 0.f;

    std::vector<h_label_t> h_labels;
    std::vector<v_label_t> v_labels;

    int    v_label_fixed_digits    = 0;
    bool   h_labels_subsecond      = false;

    int    vertical_seed_index     = -1;
    double vertical_seed_step      = 0.0;
    double vertical_finest_step    = 0.0;
    int    horizontal_seed_index   = -1;
    double horizontal_seed_step    = 0.0;
};

/// Key for layout caching. Layout is recomputed only when this key changes.
struct layout_cache_key_t
{
    float        v0                           = 0.0f;
    float        v1                           = 0.0f;
    std::int64_t t0                           = 0;  // nanoseconds
    std::int64_t t1                           = 0;  // nanoseconds
    Size_2i    viewport_size;
    double    adjusted_reserved_height     = 0.0;
    double    adjusted_preview_height      = 0.0;
    double    adjusted_font_size_in_pixels = 0.0;
    double    vbar_width_pixels            = 0.0;
    uint64_t  font_metrics_key             = 0;
    uint64_t  config_revision              = 0;
    uint64_t  format_timestamp_revision    = 0;
    uint64_t  format_value_revision        = 0;

    [[nodiscard]] bool operator==(const layout_cache_key_t& other) const noexcept
    {
        return v0 == other.v0 &&
               v1 == other.v1 &&
               t0 == other.t0 &&
               t1 == other.t1 &&
               viewport_size == other.viewport_size &&
               adjusted_reserved_height == other.adjusted_reserved_height &&
               adjusted_preview_height == other.adjusted_preview_height &&
               adjusted_font_size_in_pixels == other.adjusted_font_size_in_pixels &&
               vbar_width_pixels == other.vbar_width_pixels &&
               font_metrics_key == other.font_metrics_key &&
               config_revision == other.config_revision &&
               format_timestamp_revision == other.format_timestamp_revision &&
               format_value_revision == other.format_value_revision;
    }

};

class Layout_cache
{
public:
    using key_t = layout_cache_key_t;
    using value_type = frame_layout_result_t;

    [[nodiscard]] const value_type* try_get(const key_t& query) const noexcept
    {
        if (!m_valid || m_key != query) {
            return nullptr;
        }
        return &m_value;
    }

    [[nodiscard]] const value_type& store(const key_t& new_key, value_type&& new_value) noexcept
    {
        m_key   = new_key;
        m_value = std::move(new_value);
        m_valid = true;
        return m_value;
    }

    void invalidate() noexcept { m_valid = false; }

private:
    bool       m_valid = false;
    key_t      m_key{};
    value_type m_value{};
};

struct frame_context_t
{
    const frame_layout_result_t& layout;

    float v0 = 0.0f;
    float v1 = 1.0f;
    float preview_v0 = 0.0f;
    float preview_v1 = 0.0f;

    // Timestamps are int64_t nanoseconds (API convention).
    std::int64_t t0 = 0;
    std::int64_t t1 = 1;

    std::int64_t t_available_min = 0;
    std::int64_t t_available_max = 1;

    int win_w = 0;
    int win_h = 0;

    glm::mat4 pmv{1.0f};

    double adjusted_font_px         = 10.0;
    double base_label_height_px     = 14.0;
    double adjusted_reserved_height = 0.0;
    double adjusted_preview_height  = 0.0;

    int visible_info_flags = k_visible_info_none;
    bool dark_mode = false;

    const Plot_config* config = nullptr;

    // RHI handles for the active frame. The renderer routes uploads through
    // the RHI resource-update batch and records draws through `cb`.
    QRhi*              rhi = nullptr;
    QRhiCommandBuffer* cb  = nullptr;
    // Render target the host already opened a pass on. The renderer reads
    // the render-pass descriptor and sample count off it when building
    // graphics-pipeline state objects.
    QRhiRenderTarget*  render_target = nullptr;
    // Resource-update batch the host hands the renderer to fill. The host
    // owns its lifetime and submits it via beginPass's 4th argument; the
    // renderer must NOT call cb->resourceUpdate(batch) itself, because that
    // call is illegal once the host's render pass is open.
    QRhiResourceUpdateBatch* rhi_updates = nullptr;
};

} // namespace vnm::plot
