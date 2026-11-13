#pragma once

// The headless workloads (docs/WORKLOADS.md): each builds a Plot3DRoot in a
// Window attached to GUI.Forms' offscreen CPU raster, drives it through
// public calls and dispatched pointer and key events, captures frames, and
// writes one record. Nothing depends on wall time, so records repeat
// except for the capture intervals.

#include "options.hpp"

#include <gui_forms/application.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace implot3d_class {

enum class Workload : std::uint8_t { tour, orbit, zoom, pick, strides, interpolation, resolution, idle };

[[nodiscard]] std::optional<Workload> workload_named(std::string_view name) noexcept;
[[nodiscard]] std::string_view workload_name(Workload workload) noexcept;
// Runs `workload` and writes its record under options.output_dir. Returns
// the process exit code: 0 when every invariant held.
int run_workload(gui_forms::Application& application, const AppOptions& options, Workload workload);

} // namespace implot3d_class
