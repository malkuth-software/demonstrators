#pragma once

// Measurement records: one JSON record per workload with its inputs, every
// counter the app keeps, the window's public metrics (frames, damage,
// paints), GUI.Forms' counted allocation paths, per-frame deltas, the
// build and the outcome of the workload's invariants. Counts first; the
// only durations are frame intervals. Nothing here judges a figure.

#include "json_writer.hpp"
#include "live_feed.hpp"
#include "plot_frame.hpp"
#include "plot_root.hpp"

#include <gui_forms/allocation/statistics/allocation_statistics.hpp>
#include <gui_forms/metrics/types/metrics_types.hpp>
#include <gui_forms/plots/series_ring_channel/series_ring_channel.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace gui_forms {
class Window;
}

namespace implot_class {

// Everything the app counts, read at one moment.
struct CounterSnapshot final {
    PlotRootCounters root{};
    PlotFrameStatistics heatmap{};
    PlotFrameStatistics histogram{};
    PlotFrameStatistics drag{};
    LiveFeedCounters feed{};
    gui_forms::plots::SeriesChannelStatistics channel{};
    // The live plot's ambient frames and copies for its sub-surface.
    std::uint64_t live_frames_run{};
    std::uint64_t live_column_copies{};
    std::uint64_t subplot_propagations{};
    std::uint64_t subplot_echoes{};
    std::uint64_t histogram_rebins{};
    gui_forms::MetricsSnapshot metrics{};
    // GUI.Forms' counted allocation paths, process-wide
    // (allocation_statistics): never the process total.
    gui_forms::AllocationStatistics allocations{};
};

[[nodiscard]] CounterSnapshot read_counters(gui_forms::Window& window, PlotRoot& root);
void write_counters(JsonWriter& json, const CounterSnapshot& snapshot);
void write_frame_statistics(JsonWriter& json, const PlotFrameStatistics& statistics);
// The build: GUI.Forms version, build type, compiler, assertions.
void write_build(JsonWriter& json);

// dev/measurements/<host kind>/<workload>.json under `root`.
[[nodiscard]] std::filesystem::path record_path(const std::filesystem::path& root, std::string_view host_kind,
                                                std::string_view workload);
bool write_record_file(const std::filesystem::path& path, const std::string& text);
[[nodiscard]] std::string_view host_kind() noexcept;

} // namespace implot_class
