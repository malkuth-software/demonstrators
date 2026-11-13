#pragma once

// The headless workloads (docs/WORKLOADS.md): each builds a DemoRoot in a
// Window attached to GUI.Forms' offscreen CPU raster, drives it through
// public calls and dispatched pointer input, captures frames, and writes
// one record. Nothing runs on a clock, so the record repeats for a seed.

#include "options.hpp"

#include <gui_forms/application.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace imgui_class {

enum class Workload : std::uint8_t { tour, widgets, table, scrub, colour, style, idle };

[[nodiscard]] std::optional<Workload> workload_named(std::string_view name) noexcept;
[[nodiscard]] std::string_view workload_name(Workload workload) noexcept;
// Runs `workload` and writes its record under options.output_dir. Returns
// the process exit code: 0 when every invariant held.
int run_workload(gui_forms::Application& application, const AppOptions& options, Workload workload);

} // namespace imgui_class
