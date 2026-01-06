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
    Benchmark_profiler() {
        root_.name = "[root]";
        current_ = &root_;
    }

    ~Benchmark_profiler() override = default;

    /// Begin a named scope. Nested calls create child scopes.
    void begin_scope(const char* name) override {
        start_times_.push(std::chrono::steady_clock::now());

        // Find or create child with this name (aggregation!)
        auto& children = current_->children;
        auto it = children.find(name);
        if (it == children.end()) {
            auto child = std::make_unique<Scope_stats>();
            child->name = name;
            child->parent = current_;
            it = children.emplace(name, std::move(child)).first;
        }
        current_ = it->second.get();
    }

    /// End the current scope and record timing.
    void end_scope() override {
        if (start_times_.empty()) return;

        auto end_time = std::chrono::steady_clock::now();
        auto start_time = start_times_.top();
        start_times_.pop();

        double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        // Aggregate into existing stats
        current_->call_count++;
        current_->total_ms += elapsed_ms;
        current_->min_ms = std::min(current_->min_ms, elapsed_ms);
        current_->max_ms = std::max(current_->max_ms, elapsed_ms);

        if (current_->parent) {
            current_ = current_->parent;
        }
    }

    /// Generate report string in Lumis format
    std::string generate_report(const Report_metadata& meta) const {
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
        for (const auto& [name, child] : root_.children) {
            total_root_ms += child->total_ms;
        }

        // Write tree
        for (auto it = root_.children.begin(); it != root_.children.end(); ++it) {
            bool is_last = (std::next(it) == root_.children.end());
            write_scope_tree(oss, *it->second, "", is_last, total_root_ms);
        }

        oss << "\n";
        return oss.str();
    }

    /// Write report to file in output directory
    std::filesystem::path write_report(const Report_metadata& meta) const {
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
    void reset() {
        root_.children.clear();
        root_.call_count = 0;
        root_.total_ms = 0.0;
        root_.min_ms = std::numeric_limits<double>::max();
        root_.max_ms = 0.0;
        current_ = &root_;
        while (!start_times_.empty()) start_times_.pop();
    }

    /// Get root scope for inspection
    const auto& root() const { return root_; }

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

    Scope_stats root_;
    Scope_stats* current_ = nullptr;
    std::stack<std::chrono::steady_clock::time_point> start_times_;

    // Format UTC timestamp as ISO 8601
    static std::string format_utc_time(std::chrono::system_clock::time_point tp) {
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
    static std::string format_filename_time(std::chrono::system_clock::time_point tp) {
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
    static std::string get_compiler_string() {
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
    static void write_metadata(std::ostream& os, const Report_metadata& meta) {
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
    static std::string generate_filename(const Report_metadata& meta) {
        std::ostringstream oss;
        oss << "inspector_benchmark_" << format_filename_time(meta.started_at)
            << "_" << meta.symbol << "_" << meta.data_type << ".txt";
        return oss.str();
    }

    // Write scope tree recursively with proper indentation and glyphs
    void write_scope_tree(std::ostream& os, const Scope_stats& scope,
                          const std::string& prefix, bool is_last,
                          double total_ms) const {
        // Glyph for this node
        std::string glyph = is_last ? "\xe2\x94\x94\xe2\x94\x80 " : "\xe2\x94\x9c\xe2\x94\x80 ";  // └─ or ├─

        // Build full section name with prefix
        std::string section = prefix + glyph + scope.name;

        // Pad/truncate to 58 chars
        if (section.length() > 58) {
            section = section.substr(0, 55) + "...";
        }

        // Calculate stats
        double avg_ms = scope.call_count > 0 ? scope.total_ms / scope.call_count : 0.0;
        double percent = total_ms > 0 ? (scope.total_ms / total_ms) * 100.0 : 0.0;

        // Write row
        os << std::left << std::setw(58) << section << " "
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

        // Write children
        auto it = scope.children.begin();
        while (it != scope.children.end()) {
            bool child_is_last = (std::next(it) == scope.children.end()) && (children_total >= scope.total_ms - 0.001);
            write_scope_tree(os, *it->second, child_prefix, child_is_last, total_ms);
            ++it;
        }

        // Write unattributed row if there's unaccounted time
        double unattributed = scope.total_ms - children_total;
        if (unattributed > 0.001 && !scope.children.empty()) {
            std::string unattr_glyph = "\xe2\x94\x94\xe2\x94\x80 ";  // └─
            std::string unattr_section = child_prefix + unattr_glyph + "[unattributed]";

            if (unattr_section.length() > 58) {
                unattr_section = unattr_section.substr(0, 55) + "...";
            }

            double unattr_avg = scope.call_count > 0 ? unattributed / scope.call_count : 0.0;
            double unattr_percent = total_ms > 0 ? (unattributed / total_ms) * 100.0 : 0.0;

            os << std::left << std::setw(58) << unattr_section << " "
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
        : profiler_(profiler) {
        profiler_.begin_scope(name);
    }

    ~Profile_scope() {
        profiler_.end_scope();
    }

    Profile_scope(const Profile_scope&) = delete;
    Profile_scope& operator=(const Profile_scope&) = delete;

private:
    Benchmark_profiler& profiler_;
};

// Helper macros for unique variable names
#define BENCHMARK_CONCAT_IMPL(x, y) x##y
#define BENCHMARK_CONCAT(x, y) BENCHMARK_CONCAT_IMPL(x, y)

#define BENCHMARK_SCOPE(profiler, name) \
    vnm::benchmark::Profile_scope BENCHMARK_CONCAT(_profile_scope_, __COUNTER__)(profiler, name)

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_PROFILER_H
