#pragma once

// Measurement records (NODE_GRAPH_SPEC section 6): one JSON record per
// workload with its inputs, every counter the app keeps, per-frame counts,
// the build and the outcome of the workload's invariants. Counts come
// first; the only durations are frame intervals, which a person perceives.
// Nothing here judges a figure.

#include "evaluator.hpp"
#include "generator.hpp"
#include "graph_document.hpp"
#include "graph_root.hpp"
#include "json_writer.hpp"
#include "graph_canvas.hpp"

#include <gui_forms/allocation/statistics/allocation_statistics.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace node_graph {

// Everything the app counts, read at one moment.
struct CounterSnapshot final {
    // NodeCanvas's own statistics, its DragTracker's and its two indexes'.
    gui_forms::NodeCanvasStatistics canvas{};
    gui_forms::DragTrackerStatistics drag{};
    gui_forms::SpatialIndexStatistics node_index{};
    gui_forms::SpatialIndexStatistics link_index{};
    // What the app's subclass counts (raisers, bodies, evaluation, flow).
    GraphCanvasCounters graph_canvas{};
    DocumentCounters document{};
    EvaluatorCounters evaluator{};
    GraphRootCounters root{};
    std::size_t live_nodes{};
    std::size_t live_links{};
    std::size_t node_slots{};
    std::size_t link_slots{};
    std::size_t undo_steps{};
    std::size_t undo_bytes{};
    std::uint64_t revision{};
    std::size_t semantic_children{};
    // GUI.Forms' counted allocation paths, process-wide
    // (allocation_statistics): never the process total.
    gui_forms::AllocationStatistics allocations{};
};

[[nodiscard]] CounterSnapshot read_counters(GraphRoot& root);
void write_counters(JsonWriter& json, const CounterSnapshot& snapshot);
void write_generator(JsonWriter& json, const GeneratorReport& report);
void write_edits(JsonWriter& json, const EditReport& report);
// The build: GUI.Forms version, build type, compiler.
void write_build(JsonWriter& json);

// dev/measurements/<host kind>/<workload>-<seed>.json under `root`.
[[nodiscard]] std::filesystem::path record_path(const std::filesystem::path& root, std::string_view host_kind,
                                                std::string_view workload, std::uint64_t seed);
// Writes `text` to `path`, creating its directories. False on failure.
bool write_record_file(const std::filesystem::path& path, const std::string& text);
// The host kind recorded in paths: macos, windows, linux or other.
[[nodiscard]] std::string_view host_kind() noexcept;

} // namespace node_graph
