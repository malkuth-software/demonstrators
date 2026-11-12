#pragma once

// The measured workloads (NODE_GRAPH_SPEC section 6, docs/WORKLOADS.md):
// each builds a seeded stress graph, drives the canvas headless through a
// window attached to GUI.Forms' offscreen raster (the hosts' raster, with
// the bundled fonts), checks its invariants and writes one record. Needs
// GUIForms::Application (the raster).

#include "options.hpp"

#include <gui_forms/application.hpp>

#include <optional>
#include <string_view>

namespace node_graph {

enum class Workload : std::uint8_t { load, pan, zoom, drag, magnet, marquee_knife, edit_host, roundtrip, idle };

[[nodiscard]] std::optional<Workload> workload_named(std::string_view name) noexcept;
[[nodiscard]] std::string_view workload_name(Workload workload) noexcept;

// Runs `workload` and writes its record under options.output_dir. Returns
// 0 when every invariant held and the record was written, 1 otherwise.
[[nodiscard]] int run_workload(gui_forms::Application& application, const AppOptions& options, Workload workload);

} // namespace node_graph
