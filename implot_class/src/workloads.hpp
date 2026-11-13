#pragma once

// The headless workloads (docs/WORKLOADS.md): each builds a PlotRoot in a
// Window attached to GUI.Forms' offscreen CPU raster, drives it through
// public calls and dispatched input, captures frames, and writes one
// record. The live feed's thread is not started: the Live workload writes
// blocks itself and runs the plot's ambient frames at fixed 60 Hz times,
// so the record repeats.

#include "options.hpp"

#include <gui_forms/application.hpp>

#include <optional>
#include <string_view>

namespace implot_class {

enum class Workload : std::uint8_t { tour, large_pan, heatmap, drag, live, link, idle };

[[nodiscard]] std::optional<Workload> workload_named(std::string_view name) noexcept;
[[nodiscard]] std::string_view workload_name(Workload workload) noexcept;
// Runs `workload` and writes its record under options.output_dir. Returns
// the process exit code: 0 when every invariant held.
int run_workload(gui_forms::Application& application, const AppOptions& options, Workload workload);

} // namespace implot_class
