#pragma once

// Measurement records: one JSON record per workload with its inputs, every
// counter the app keeps (the model's, the loader's, each view's, the
// table's and VirtualRowsView's), the window's public metrics (frames,
// damage, paints), GUI.Forms' counted allocation paths, per-frame deltas,
// the build and the outcome of the workload's invariants. Counts first; the
// only durations are capture intervals. Nothing here judges a figure.

#include "json_writer.hpp"
#include "profiler_root.hpp"

#include <gui_forms/allocation/statistics/allocation_statistics.hpp>
#include <gui_forms/controls/virtual_rows_view.hpp>
#include <gui_forms/metrics/types/metrics_types.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace gui_forms {
class Window;
}

namespace flame_graph {

// Everything the app counts, read at one moment.
struct CounterSnapshot final {
    ProfilerCounters root{};
    TraceStatistics model{};
    StringTableStatistics names{};
    std::size_t name_count{};
    std::size_t name_bytes{};
    LoadReport load{};
    SearchSummary search{};
    FlameViewStatistics flame{};
    TimelineStatistics timeline{};
    DetailsTableStatistics table{};
    gui_forms::VirtualRowsStatistics rows{};
    std::size_t table_rows{};
    gui_forms::MetricsSnapshot metrics{};
    // GUI.Forms' counted allocation paths, process-wide
    // (allocation_statistics): never the process total.
    gui_forms::AllocationStatistics allocations{};
};

[[nodiscard]] CounterSnapshot read_counters(gui_forms::Window& window, ProfilerRoot& root);
void write_counters(JsonWriter& json, const CounterSnapshot& snapshot);
void write_load_report(JsonWriter& json, const LoadReport& report);
// The build: GUI.Forms version, build type, compiler, assertions.
void write_build(JsonWriter& json);

// dev/measurements/flame_graph/<host kind>/<workload>.json under `root`.
[[nodiscard]] std::filesystem::path record_path(const std::filesystem::path& root, std::string_view host_kind,
                                                std::string_view workload);
bool write_record_file(const std::filesystem::path& path, const std::string& text);
[[nodiscard]] std::string_view host_kind() noexcept;

} // namespace flame_graph
