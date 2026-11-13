#pragma once

// Measurement records: one JSON record per workload with its inputs, every
// counter the app keeps, the plot's scene statistics (pictures rendered,
// meshes built, triangles drawn), the window's public metrics (frames,
// damage, paints), GUI.Forms' counted allocation paths, per-frame deltas,
// the build and the outcome of the workload's invariants. Counts first;
// the only durations are capture intervals. Nothing here judges a figure.

#include "json_writer.hpp"
#include "plot3d_root.hpp"

#include <gui_forms/allocation/statistics/allocation_statistics.hpp>
#include <gui_forms/metrics/types/metrics_types.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace gui_forms {
class Window;
}

namespace implot3d_class {

// The plot's own figures, read at one moment.
struct SceneSnapshot final {
    std::uint64_t picture_renders{};
    std::uint64_t mesh_builds{};
    std::uint64_t triangles_drawn{};
    std::uint64_t items{};
    std::uint64_t labels{};
    gui_forms::plots::Plot3DCamera camera{};
};

// Everything the app counts, read at one moment.
struct CounterSnapshot final {
    Plot3DRootCounters root{};
    SceneSnapshot scene{};
    gui_forms::MetricsSnapshot metrics{};
    // GUI.Forms' counted allocation paths, process-wide
    // (allocation_statistics): never the process total.
    gui_forms::AllocationStatistics allocations{};
};

[[nodiscard]] SceneSnapshot read_scene(const gui_forms::Plot3DView& plot);
[[nodiscard]] CounterSnapshot read_counters(gui_forms::Window& window, const Plot3DRoot& root);
void write_scene(JsonWriter& json, const SceneSnapshot& scene);
void write_counters(JsonWriter& json, const CounterSnapshot& snapshot);
// The build: GUI.Forms version, build type, compiler, assertions.
void write_build(JsonWriter& json);

// dev/measurements/implot3d_class/<host kind>/<workload>.json under `root`.
[[nodiscard]] std::filesystem::path record_path(const std::filesystem::path& root, std::string_view host_kind,
                                                std::string_view workload);
bool write_record_file(const std::filesystem::path& path, const std::string& text);
[[nodiscard]] std::string_view host_kind() noexcept;

} // namespace implot3d_class
