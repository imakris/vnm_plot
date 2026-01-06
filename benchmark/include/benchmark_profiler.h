// vnm_plot Benchmark - Profiler
// Hierarchical scope timing with Lumis-compatible report generation

#ifndef VNM_PLOT_BENCHMARK_PROFILER_H
#define VNM_PLOT_BENCHMARK_PROFILER_H

#include <vnm_plot/core/plot_config.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stack>
#include <string>

namespace vnm::benchmark {

/// Report metadata for output file
struct Report_metadata {
    std::string session = "benchmark_run";
    std::string symbol = "SIM";
    std::string data_type = "Bars";      // "Bars" or "Trades"
    double target_duration = 30.0;
    std::filesystem::path output_directory;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point generated_at;

    // Extended metadata (only included with --extended-metadata flag)
    bool include_extended = false;
    uint64_t seed = 0;
    double volatility = 0.0;
    std::size_t ring_capacity = 0;
    std::size_t samples_generated = 0;
};

/// Profiler that builds hierarchical timing tree with scope aggregation.
/// Implements vnm::plot::Profiler interface to capture vnm_plot internal scopes.
class Benchmark_profiler : public vnm::plot::Profiler {
public:
    Benchmark_profiler()
    {
        m_root.name = "[root]";
        m_current = &m_root;
    }

    ~Benchmark_profiler() override = default;

    /// Begin a named scope. Nested calls create child scopes.
    void begin_scope(const char* name) override
    {
        m_start_times.push(std::chrono::steady_clock::now());

        // Find or create child with this name (aggregation!)
        auto& children = m_current->children;
        auto it = children.find(name);
        if (it == children.end()) {
            auto child = std::make_unique<Scope_stats>();
            child->name = name;
            child->parent = m_current;
            it = children.emplace(name, std::move(child)).first;
        }
        m_current = it->second.get();
    }

    /// End the current scope and record timing.
    void end_scope() override
    {
        if (m_start_times.empty()) {
            return;
        }

        auto end_time = std::chrono::steady_clock::now();
        auto start_time = m_start_times.top();
        m_start_times.pop();

        double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        // Aggregate into existing stats
        m_current->call_count++;
        m_current->total_ms += elapsed_ms;
        m_current->min_ms = std::min(m_current->min_ms, elapsed_ms);
        m_current->max_ms = std::max(m_current->max_ms, elapsed_ms);

        if (m_current->parent) {
            m_current = m_current->parent;
        }
    }

    /// Generate report string in Lumis format
    std::string generate_report(const Report_metadata& meta) const
    {
        std::ostringstream oss;

        // Header
        oss << "Lumis profiling report\n";
        oss << "Session: " << meta.session << "\n";
        oss << "Started at (UTC): " << format_utc_time(meta.started_at) << "\n";
        oss << "Target duration (s): " << std::fixed << std::setprecision(3) << meta.target_duration << "\n";
        oss << "Report generated at (UTC): " << format_utc_time(meta.generated_at) << "\n";
        oss << "Built with " << get_compiler_string() << "\n";
        oss << "\n";

        // Metadata section
        oss << "Metadata:\n";
        write_metadata(oss, meta);
        oss << "\n";

        // Table header (108 characters total)
        // Column widths: Section=58, Calls=5, Total=9, Avg=10, Min=6, Max=6, Percent=8 (+6 spaces)
        oss << std::left << std::setw(58) << "Section" << " "
            << std::right << std::setw(5) << "Calls" << " "
            << std::setw(9) << "Total ms" << " "
            << std::setw(10) << "Average ms" << " "
            << std::setw(6) << "Min ms" << " "
            << std::setw(6) << "Max ms" << " "
            << std::setw(8) << "Percent" << "\n";
        oss << std::string(108, '-') << "\n";

        // Calculate total time for percentage
        double total_root_ms = 0.0;
        for (const auto& [name, child] : m_root.children) {
            total_root_ms += child->total_ms;
        }

        // Write tree (disambiguate duplicate short names at the root level)
        std::map<std::string, std::size_t> root_name_counts;
        for (const auto& [name, child] : m_root.children) {
            root_name_counts[short_name(child->name)]++;
        }
        std::map<std::string, std::size_t> root_name_seen;

        for (auto it = m_root.children.begin(); it != m_root.children.end(); ++it) {
            bool is_last = (std::next(it) == m_root.children.end());
            std::string display_name = short_name(it->second->name);
            if (root_name_counts[display_name] > 1) {
                std::size_t idx = ++root_name_seen[display_name];
                display_name += "_" + std::to_string(idx);
            }
            write_scope_tree(oss, *it->second, "", is_last, total_root_ms, display_name);
        }

        oss << "\n";
        return oss.str();
    }

    /// Write report to file in output directory
    std::filesystem::path write_report(const Report_metadata& meta) const
    {
        // Generate filename: inspector_benchmark_YYYYMMDD_HHMMSS_<SYMBOL>_<DataType>.txt
        std::string filename = generate_filename(meta);
        std::filesystem::path output_path = meta.output_directory / filename;

        // Ensure directory exists
        std::filesystem::create_directories(meta.output_directory);

        // Write report
        std::ofstream ofs(output_path);
        if (ofs) {
            ofs << generate_report(meta);
        }

        return output_path;
    }

    /// Reset profiler for next run
    void reset()
    {
        m_root.children.clear();
        m_root.call_count = 0;
        m_root.total_ms = 0.0;
        m_root.min_ms = std::numeric_limits<double>::max();
        m_root.max_ms = 0.0;
        m_current = &m_root;
        while (!m_start_times.empty()) {
            m_start_times.pop();
        }
    }

    /// Get root scope for inspection
    const auto& root() const { return m_root; }

private:
    struct Scope_stats {
        std::string name;
        uint64_t call_count = 0;
        double total_ms = 0.0;
        double min_ms = std::numeric_limits<double>::max();
        double max_ms = 0.0;
        std::map<std::string, std::unique_ptr<Scope_stats>> children;
        Scope_stats* parent = nullptr;
    };

    Scope_stats m_root;
    Scope_stats* m_current = nullptr;
    std::stack<std::chrono::steady_clock::time_point> m_start_times;

    // Format UTC timestamp as ISO 8601
    static std::string format_utc_time(std::chrono::system_clock::time_point tp)
    {
        auto time_t_val = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_val{};
#ifdef _WIN32
        gmtime_s(&tm_val, &time_t_val);
#else
        gmtime_r(&time_t_val, &tm_val);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_val, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    // Format timestamp for filename
    static std::string format_filename_time(std::chrono::system_clock::time_point tp)
    {
        auto time_t_val = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_val{};
#ifdef _WIN32
        gmtime_s(&tm_val, &time_t_val);
#else
        gmtime_r(&time_t_val, &tm_val);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_val, "%Y%m%d_%H%M%S");
        return oss.str();
    }

    // Get compiler identification string
    static std::string get_compiler_string()
    {
#if defined(__GNUC__) && !defined(__clang__)
        std::ostringstream oss;
        oss << "GNU v" << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
        return oss.str();
#elif defined(__clang__)
        std::ostringstream oss;
        oss << "Clang v" << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__;
        return oss.str();
#elif defined(_MSC_VER)
        std::ostringstream oss;
        oss << "MSVC v" << (_MSC_VER / 100) << "." << (_MSC_VER % 100);
        return oss.str();
#else
        return "Unknown compiler";
#endif
    }

    // Write metadata section (alphabetically sorted)
    static void write_metadata(std::ostream& os, const Report_metadata& meta)
    {
        // Standard metadata (Lumis-compatible)
        os << "  - data_type: " << meta.data_type << "\n";
        os << "  - duration_seconds: " << static_cast<int>(meta.target_duration) << "\n";
        os << "  - output_directory: " << meta.output_directory.generic_string() << "\n";
        os << "  - report_filename: " << generate_filename(meta) << "\n";

        // Extended metadata (if enabled)
        if (meta.include_extended) {
            os << "  - ring_capacity: " << meta.ring_capacity << "\n";
            os << "  - samples_generated: " << meta.samples_generated << "\n";
            os << "  - seed: " << meta.seed << "\n";
        }

        os << "  - started_at_utc: " << format_utc_time(meta.started_at) << "\n";
        os << "  - symbol: " << meta.symbol << "\n";

        if (meta.include_extended) {
            os << "  - volatility: " << std::fixed << std::setprecision(4) << meta.volatility << "\n";
        }
    }

    // Generate filename
    static std::string generate_filename(const Report_metadata& meta)
    {
        std::ostringstream oss;
        oss << "inspector_benchmark_" << format_filename_time(meta.started_at)
            << "_" << meta.symbol << "_" << meta.data_type << ".txt";
        return oss.str();
    }

    // Extract short name from fully qualified scope name (e.g., "renderer.frame.foo" -> "foo")
    static std::string short_name(const std::string& full_name)
    {
        auto pos = full_name.rfind('.');
        if (pos != std::string::npos && pos + 1 < full_name.length()) {
            return full_name.substr(pos + 1);
        }
        return full_name;
    }

    // Count display width (ASCII chars = 1, UTF-8 sequences = 1 per character)
    static std::size_t display_width(const std::string& s)
    {
        std::size_t width = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if ((c & 0xC0) != 0x80) {  // Not a continuation byte
                ++width;
            }
        }
        return width;
    }

    // Pad string to target display width
    static std::string pad_to_width(const std::string& s, std::size_t target_width)
    {
        std::size_t current_width = display_width(s);
        if (current_width >= target_width) {
            return s;
        }
        return s + std::string(target_width - current_width, ' ');
    }

    // Write scope tree recursively with proper indentation and glyphs
    void write_scope_tree(
        std::ostream& os,
        const Scope_stats& scope,
        const std::string& prefix,
        bool is_last,
        double total_ms,
        const std::string& display_override = "") const
    {
        // Glyph for this node
        std::string glyph = is_last ? "\xe2\x94\x94\xe2\x94\x80 " : "\xe2\x94\x9c\xe2\x94\x80 ";  // └─ or ├─

        // Use short name (strip parent prefix since tree shows hierarchy)
        std::string display_name = display_override.empty()
            ? short_name(scope.name)
            : display_override;

        // Build section with tree prefix
        std::string section = prefix + glyph + display_name;

        // Truncate if too long (by display width)
        std::size_t section_width = display_width(section);
        if (section_width > 58) {
            // Truncate display_name to fit
            std::size_t prefix_width = display_width(prefix + glyph);
            std::size_t max_name_width = 55 - prefix_width;
            if (display_name.length() > max_name_width) {
                display_name = display_name.substr(0, max_name_width - 3) + "...";
            }
            section = prefix + glyph + display_name;
        }

        // Pad to 58 display characters
        section = pad_to_width(section, 58);

        // Calculate stats
        double avg_ms = scope.call_count > 0 ? scope.total_ms / scope.call_count : 0.0;
        double percent = total_ms > 0 ? (scope.total_ms / total_ms) * 100.0 : 0.0;

        // Write row
        os << section << " "
           << std::right << std::setw(5) << scope.call_count << " "
           << std::fixed << std::setprecision(3)
           << std::setw(9) << scope.total_ms << " "
           << std::setw(10) << avg_ms << " "
           << std::setw(6) << scope.min_ms << " "
           << std::setw(6) << scope.max_ms << " "
           << std::setw(7) << percent << "%\n";

        // Calculate children prefix
        std::string child_prefix = prefix + (is_last ? "   " : "\xe2\x94\x82  ");  // │ for continuation

        // Calculate unattributed time
        double children_total = 0.0;
        for (const auto& [name, child] : scope.children) {
            children_total += child->total_ms;
        }

        // Prepare child display names (disambiguate duplicate short names)
        std::map<std::string, std::size_t> short_name_counts;
        for (const auto& [child_name, child] : scope.children) {
            short_name_counts[short_name(child->name)]++;
        }
        std::map<std::string, std::size_t> short_name_seen;

        // Write children
        auto it = scope.children.begin();
        while (it != scope.children.end()) {
            bool child_is_last = (std::next(it) == scope.children.end()) && (children_total >= scope.total_ms - 0.001);
            std::string child_display = short_name(it->second->name);
            if (short_name_counts[child_display] > 1) {
                std::size_t idx = ++short_name_seen[child_display];
                child_display += "_" + std::to_string(idx);
            }
            write_scope_tree(os, *it->second, child_prefix, child_is_last, total_ms, child_display);
            ++it;
        }

        // Write unattributed row if there's unaccounted time
        double unattributed = scope.total_ms - children_total;
        if (unattributed > 0.001 && !scope.children.empty()) {
            std::string unattr_glyph = "\xe2\x94\x94\xe2\x94\x80 ";  // └─
            std::string unattr_section = child_prefix + unattr_glyph + "[unattributed]";
            unattr_section = pad_to_width(unattr_section, 58);

            double unattr_avg = scope.call_count > 0 ? unattributed / scope.call_count : 0.0;
            double unattr_percent = total_ms > 0 ? (unattributed / total_ms) * 100.0 : 0.0;

            os << unattr_section << " "
               << std::right << std::setw(5) << scope.call_count << " "
               << std::fixed << std::setprecision(3)
               << std::setw(9) << unattributed << " "
               << std::setw(10) << unattr_avg << " "
               << std::setw(6) << "-" << " "
               << std::setw(6) << "-" << " "
               << std::setw(7) << unattr_percent << "%\n";
        }
    }
};

/// RAII scope guard for automatic begin/end
class Profile_scope {
public:
    Profile_scope(Benchmark_profiler& profiler, const char* name)

    :
        m_profiler(profiler)
    {
        m_profiler.begin_scope(name);
    }

    ~Profile_scope() { m_profiler.end_scope(); }

    Profile_scope(const Profile_scope&) = delete;
    Profile_scope& operator=(const Profile_scope&) = delete;

private:
    Benchmark_profiler& m_profiler;
};

// Helper macros for unique variable names
#define BENCHMARK_CONCAT_IMPL(x, y) x##y
#define BENCHMARK_CONCAT(x, y) BENCHMARK_CONCAT_IMPL(x, y)

#define BENCHMARK_SCOPE(profiler, name) \
    vnm::benchmark::Profile_scope BENCHMARK_CONCAT(profile_scope, __COUNTER__)(profiler, name)

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_PROFILER_H
