#include "measurement.hpp"

#include <gui_forms/allocation.hpp>
#include <gui_forms/window/window.hpp>

#include <fstream>
#include <system_error>

namespace flame_graph {

namespace gf = gui_forms;

namespace {

void field(JsonWriter& json, std::string_view name, std::uint64_t value) {
    json.key(name);
    json.value(value);
}

void size_field(JsonWriter& json, std::string_view name, std::size_t value) {
    json.key(name);
    json.value(static_cast<std::uint64_t>(value));
}

} // namespace

CounterSnapshot read_counters(gf::Window& window, ProfilerRoot& root) {
    CounterSnapshot snapshot{};
    snapshot.root = root.counters();
    snapshot.model = root.model().statistics();
    snapshot.names = root.model().names().statistics();
    snapshot.name_count = root.model().names().size();
    snapshot.name_bytes = root.model().names().arena_bytes();
    snapshot.load = root.last_report();
    snapshot.search = root.search().summary;
    snapshot.flame = root.flame().statistics();
    snapshot.timeline = root.timeline().statistics();
    snapshot.table = root.table().statistics();
    snapshot.rows = root.table().rows().statistics();
    snapshot.table_rows = root.table().row_count();
    snapshot.metrics = window.metrics_snapshot();
    snapshot.allocations = gf::allocation_statistics();
    return snapshot;
}

void write_load_report(JsonWriter& json, const LoadReport& report) {
    json.begin_object();
    json.key("loaded");
    json.value(report.loaded);
    json.key("format");
    json.value(format_name(report.format));
    json.key("message");
    json.value(report.message);
    size_field(json, "bytes", report.bytes);
    size_field(json, "events", report.events);
    size_field(json, "complete_events", report.complete_events);
    size_field(json, "begin_events", report.begin_events);
    size_field(json, "end_events", report.end_events);
    size_field(json, "unmatched_ends", report.unmatched_ends);
    size_field(json, "unclosed_begins", report.unclosed_begins);
    size_field(json, "metadata_events", report.metadata_events);
    size_field(json, "other_events", report.other_events);
    size_field(json, "malformed_events", report.malformed_events);
    size_field(json, "lines", report.lines);
    size_field(json, "malformed_lines", report.malformed_lines);
    size_field(json, "samples", report.samples);
    size_field(json, "spans_refused", report.spans_refused);
    size_field(json, "names_refused", report.names_refused);
    size_field(json, "threads_refused", report.threads_refused);
    json.end_object();
}

void write_counters(JsonWriter& json, const CounterSnapshot& snapshot) {
    json.begin_object();
    const ProfilerCounters& root = snapshot.root;
    json.key("root");
    json.begin_object();
    field(json, "loads", root.loads);
    field(json, "load_failures", root.load_failures);
    field(json, "searches", root.searches);
    field(json, "flame_picks", root.flame_picks);
    field(json, "timeline_picks", root.timeline_picks);
    field(json, "table_picks", root.table_picks);
    field(json, "table_activations", root.table_activations);
    field(json, "orientation_switches", root.orientation_switches);
    field(json, "zoom_resets", root.zoom_resets);
    field(json, "status_updates", root.status_updates);
    json.end_object();
    const TraceStatistics& model = snapshot.model;
    json.key("model");
    json.begin_object();
    size_field(json, "spans", model.spans);
    size_field(json, "spans_dropped_depth", model.spans_dropped_depth);
    size_field(json, "overlaps_clipped", model.overlaps_clipped);
    size_field(json, "nodes", model.nodes);
    size_field(json, "child_probes", model.child_probes);
    size_field(json, "functions", model.functions);
    field(json, "max_depth", model.max_depth);
    size_field(json, "names", snapshot.name_count);
    size_field(json, "name_bytes", snapshot.name_bytes);
    field(json, "name_lookups", snapshot.names.lookups);
    field(json, "name_probes", snapshot.names.probes);
    field(json, "name_table_growths", snapshot.names.growths);
    json.end_object();
    json.key("load");
    write_load_report(json, snapshot.load);
    json.key("search");
    json.begin_object();
    size_field(json, "names_matched", snapshot.search.names_matched);
    size_field(json, "nodes_matched", snapshot.search.nodes_matched);
    json.key("matched_weight");
    json.value(snapshot.search.matched_weight);
    json.end_object();
    const FlameViewStatistics& flame = snapshot.flame;
    json.key("flame");
    json.begin_object();
    field(json, "frames_painted", flame.frames_painted);
    field(json, "cells_visited", flame.cells_visited);
    field(json, "cells_drawn", flame.cells_drawn);
    field(json, "cells_pruned_narrow", flame.cells_pruned_narrow);
    field(json, "cells_culled", flame.cells_culled);
    field(json, "cells_over_cap", flame.cells_over_cap);
    field(json, "labels_drawn", flame.labels_drawn);
    field(json, "labels_cut", flame.labels_cut);
    field(json, "label_measures", flame.label_measures);
    field(json, "picks", flame.picks);
    field(json, "zooms", flame.zooms);
    field(json, "resets", flame.resets);
    field(json, "view_changes", flame.view_changes);
    field(json, "row_scrolls", flame.row_scrolls);
    json.end_object();
    const TimelineStatistics& timeline = snapshot.timeline;
    json.key("timeline");
    json.begin_object();
    field(json, "frames_painted", timeline.frames_painted);
    field(json, "spans_visited", timeline.spans_visited);
    field(json, "bars_drawn", timeline.bars_drawn);
    field(json, "spans_merged", timeline.spans_merged);
    field(json, "jumps", timeline.jumps);
    field(json, "spans_jumped", timeline.spans_jumped);
    field(json, "bars_over_cap", timeline.bars_over_cap);
    field(json, "labels_drawn", timeline.labels_drawn);
    field(json, "label_measures", timeline.label_measures);
    field(json, "tick_generations", timeline.tick_generations);
    field(json, "picks", timeline.picks);
    field(json, "view_changes", timeline.view_changes);
    field(json, "row_scrolls", timeline.row_scrolls);
    json.end_object();
    const DetailsTableStatistics& table = snapshot.table;
    json.key("table");
    json.begin_object();
    field(json, "rebuilds", table.rebuilds);
    field(json, "sorts", table.sorts);
    field(json, "header_clicks", table.header_clicks);
    field(json, "rows_painted", table.rows_painted);
    field(json, "picks", table.picks);
    field(json, "activations", table.activations);
    size_field(json, "rows", snapshot.table_rows);
    json.end_object();
    const gf::VirtualRowsStatistics& rows = snapshot.rows;
    json.key("virtual_rows");
    json.begin_object();
    field(json, "rows_measured", rows.rows_measured);
    field(json, "rows_painted", rows.rows_painted);
    field(json, "rows_described", rows.rows_described);
    field(json, "scroll_copies", rows.scroll_copies);
    field(json, "scroll_repaints", rows.scroll_repaints);
    size_field(json, "last_paint_rows", rows.last_paint_rows);
    size_field(json, "index_nodes", rows.index_nodes);
    size_field(json, "index_bytes", rows.index_bytes);
    field(json, "partial_repaints", rows.partial_repaints);
    json.end_object();
    // GUI.Forms' own counted allocation paths, process-wide since start;
    // its "scope" member says so. Not the process's allocations.
    json.key("gui_forms_allocations");
    json.raw(snapshot.allocations.to_json());
    json.key("window_metrics");
    json.raw(snapshot.metrics.to_json());
    json.end_object();
}

void write_build(JsonWriter& json) {
    json.begin_object();
    json.key("gui_forms_version");
    json.value(FLAME_GRAPH_GUI_FORMS_VERSION);
    json.key("build_type");
    json.value(FLAME_GRAPH_BUILD_TYPE);
    json.key("compiler");
#if defined(__clang__)
    json.value(__clang_version__);
#elif defined(_MSC_VER)
    json.value("msvc");
#else
    json.value("other");
#endif
    json.key("assertions");
#if defined(NDEBUG)
    json.value(false);
#else
    json.value(true);
#endif
    json.end_object();
}

std::string_view host_kind() noexcept {
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#elif defined(__linux__)
    return "linux";
#else
    return "other";
#endif
}

std::filesystem::path record_path(const std::filesystem::path& root, std::string_view host, std::string_view workload) {
    std::string name(workload);
    name.append(".json");
    const std::filesystem::path path = root / "dev" / "measurements" / "flame_graph" / std::string(host) / name;
    return path;
}

bool write_record_file(const std::filesystem::path& path, const std::string& text) {
    std::error_code error{};
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.put('\n');
    const bool written = static_cast<bool>(file);
    return written;
}

} // namespace flame_graph
