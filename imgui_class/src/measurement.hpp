#pragma once

// Measurement records: one JSON record per workload with its inputs, every
// counter the app keeps, the window's public metrics (frames, damage,
// paints), GUI.Forms' counted allocation paths, per-frame deltas, the
// build and the outcome of the workload's invariants. Counts first; the
// only durations are frame intervals. Nothing here judges a figure.

#include "color_field.hpp"
#include "demo_root.hpp"
#include "json_writer.hpp"

#include <gui_forms/allocation/statistics/allocation_statistics.hpp>
#include <gui_forms/metrics/types/metrics_types.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace gui_forms {
class Window;
}

namespace imgui_class {

// Everything the app counts, read at one moment.
struct CounterSnapshot final {
    DemoRootCounters root{};
    ColorFieldStatistics color_field{};
    std::uint64_t model_advances{};
    std::uint64_t vector_changes{};
    std::size_t visible_rows{};
    gui_forms::MetricsSnapshot metrics{};
    // GUI.Forms' counted allocation paths, process-wide
    // (allocation_statistics): never the process total.
    gui_forms::AllocationStatistics allocations{};
};

[[nodiscard]] CounterSnapshot read_counters(gui_forms::Window& window, DemoRoot& root);
void write_counters(JsonWriter& json, const CounterSnapshot& snapshot);
// The build: GUI.Forms version, build type, compiler, assertions.
void write_build(JsonWriter& json);

// dev/measurements/imgui_class/<host kind>/<workload>.json under `root`.
[[nodiscard]] std::filesystem::path record_path(const std::filesystem::path& root, std::string_view host_kind,
                                                std::string_view workload);
bool write_record_file(const std::filesystem::path& path, const std::string& text);
[[nodiscard]] std::string_view host_kind() noexcept;

} // namespace imgui_class
