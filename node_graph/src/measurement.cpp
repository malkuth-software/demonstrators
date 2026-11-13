#include "measurement.hpp"

#include <cstdio>
#include <fstream>
#include <system_error>

#ifndef NODE_GRAPH_GUI_FORMS_VERSION
#define NODE_GRAPH_GUI_FORMS_VERSION "unknown"
#endif
#ifndef NODE_GRAPH_BUILD_TYPE
#define NODE_GRAPH_BUILD_TYPE "unknown"
#endif

namespace node_graph {

namespace gf = gui_forms;

namespace {

void field(JsonWriter& json, std::string_view name, std::uint64_t number) {
    json.key(name);
    json.value(number);
}

void field(JsonWriter& json, std::string_view name, double number) {
    json.key(name);
    json.value(number);
}

void write_canvas(JsonWriter& json, const gf::NodeCanvasStatistics& canvas) {
    json.begin_object();
    field(json, "frames_painted", canvas.frames_painted);
    field(json, "nodes_painted", canvas.nodes_painted);
    field(json, "links_painted", canvas.links_painted);
    field(json, "link_points_flattened", canvas.link_points_flattened);
    field(json, "captions_drawn", canvas.captions_drawn);
    field(json, "text_skipped_small", canvas.text_skipped_small);
    field(json, "ports_painted", canvas.ports_painted);
    field(json, "lamps_painted", canvas.lamps_painted);
    field(json, "hit_tests", canvas.hit_tests);
    field(json, "node_index_builds", canvas.node_index_builds);
    field(json, "link_index_builds", canvas.link_index_builds);
    field(json, "index_moves_in_place", canvas.index_moves_in_place);
    field(json, "index_moves_refused", canvas.index_moves_refused);
    field(json, "router_runs", canvas.router_runs);
    field(json, "lamp_repaints", canvas.lamp_repaints);
    field(json, "gestures_started", canvas.gestures_started);
    field(json, "gestures_committed", canvas.gestures_committed);
    field(json, "gestures_cancelled", canvas.gestures_cancelled);
    field(json, "link_drags", canvas.link_drags);
    field(json, "magnet_candidates", canvas.magnet_candidates);
    field(json, "magnet_captures", canvas.magnet_captures);
    field(json, "connections_made", canvas.connections_made);
    field(json, "connections_refused", canvas.connections_refused);
    field(json, "links_detached", canvas.links_detached);
    field(json, "marquee_selections", canvas.marquee_selections);
    field(json, "knife_cuts", canvas.knife_cuts);
    field(json, "palette_requests", canvas.palette_requests);
    field(json, "edit_requests", canvas.edit_requests);
    field(json, "edits_committed", canvas.edits_committed);
    field(json, "edits_cancelled", canvas.edits_cancelled);
    field(json, "clicks_suppressed", canvas.clicks_suppressed);
    field(json, "key_presses_suppressed", canvas.key_presses_suppressed);
    field(json, "level_transitions", canvas.level_transitions);
    field(json, "focus_rings_painted", canvas.focus_rings_painted);
    field(json, "item_looks_resolved", canvas.item_looks_resolved);
    field(json, "flow_frames", canvas.flow_frames);
    field(json, "flow_links_painted", canvas.flow_links_painted);
    field(json, "last_frame_nodes", static_cast<std::uint64_t>(canvas.last_frame_nodes));
    field(json, "last_frame_links", static_cast<std::uint64_t>(canvas.last_frame_links));
    json.key("level");
    json.value(gf::scene_detail_level_name(canvas.level));
    json.end_object();
}

void write_graph_canvas(JsonWriter& json, const GraphCanvasCounters& canvas) {
    json.begin_object();
    field(json, "selection_raised", canvas.selection_raised);
    field(json, "link_connected_raised", canvas.link_connected_raised);
    field(json, "graph_edited_raised", canvas.graph_edited_raised);
    field(json, "view_raised", canvas.view_raised);
    field(json, "palette_raised", canvas.palette_raised);
    field(json, "edit_ended_raised", canvas.edit_ended_raised);
    field(json, "link_checks", canvas.link_checks);
    field(json, "link_checks_refused", canvas.link_checks_refused);
    field(json, "bodies_painted", canvas.bodies_painted);
    field(json, "value_bars_painted", canvas.value_bars_painted);
    field(json, "previews_painted", canvas.previews_painted);
    field(json, "body_text_skipped", canvas.body_text_skipped);
    field(json, "evaluations", canvas.evaluations);
    field(json, "flow_marks", canvas.flow_marks);
    field(json, "ambient_frames", canvas.ambient_frames);
    field(json, "ambient_enlistments", canvas.ambient_enlistments);
    field(json, "background_changes", canvas.background_changes);
    field(json, "save_requests", canvas.save_requests);
    field(json, "last_frame_live_sinks", static_cast<std::uint64_t>(canvas.last_frame_live_sinks));
    field(json, "flowing_links", static_cast<std::uint64_t>(canvas.flowing_links));
    json.end_object();
}

void write_drag(JsonWriter& json, const gf::DragTrackerStatistics& drag) {
    json.begin_object();
    field(json, "sessions", drag.sessions);
    field(json, "refused_starts", drag.refused_starts);
    field(json, "plans", drag.plans);
    field(json, "refused_plans", drag.refused_plans);
    field(json, "replans", drag.replans);
    field(json, "previews", drag.previews);
    field(json, "commits", drag.commits);
    field(json, "restores", drag.restores);
    field(json, "withdrawn", drag.withdrawn);
    field(json, "deferred_replans", drag.deferred_replans);
    field(json, "scroll_ticks", drag.scroll_ticks);
    field(json, "scroll_steps", drag.scroll_steps);
    field(json, "scroll_stops", drag.scroll_stops);
    json.end_object();
}

void write_index(JsonWriter& json, const gf::SpatialIndexStatistics& index) {
    json.begin_object();
    field(json, "builds", index.builds);
    field(json, "queries", index.queries);
    field(json, "cells_visited", index.cells_visited);
    field(json, "entries_visited", index.entries_visited);
    field(json, "moved_in_place", index.moved_in_place);
    field(json, "moved_out_of_cells", index.moved_out_of_cells);
    field(json, "moves_refused", index.moves_refused);
    field(json, "columns", static_cast<std::uint64_t>(index.columns));
    field(json, "rows", static_cast<std::uint64_t>(index.rows));
    field(json, "cell_size", index.cell_size);
    field(json, "cell_entries", static_cast<std::uint64_t>(index.cell_entries));
    field(json, "oversize", static_cast<std::uint64_t>(index.oversize));
    field(json, "displaced", static_cast<std::uint64_t>(index.displaced));
    json.end_object();
}

void write_document(JsonWriter& json, const DocumentCounters& document) {
    json.begin_object();
    field(json, "nodes_added", document.nodes_added);
    field(json, "nodes_removed", document.nodes_removed);
    field(json, "moves", document.moves);
    field(json, "connects", document.connects);
    field(json, "connects_refused_type", document.connects_refused_type);
    field(json, "connects_refused_cycle", document.connects_refused_cycle);
    field(json, "connects_refused_graph", document.connects_refused_graph);
    field(json, "disconnects", document.disconnects);
    field(json, "property_edits", document.property_edits);
    field(json, "title_edits", document.title_edits);
    field(json, "undos", document.undos);
    field(json, "redos", document.redos);
    field(json, "gestures", document.gestures);
    field(json, "loads", document.loads);
    field(json, "saves", document.saves);
    field(json, "autosaves", document.autosaves);
    field(json, "cycle_checks", document.cycle_checks);
    field(json, "cycle_check_visits", document.cycle_check_visits);
    field(json, "topology_builds", document.topology_builds);
    field(json, "lamp_writes", document.lamp_writes);
    field(json, "canvas_moves", document.canvas_moves);
    field(json, "canvas_connects", document.canvas_connects);
    field(json, "canvas_disconnects", document.canvas_disconnects);
    field(json, "canvas_removals", document.canvas_removals);
    field(json, "canvas_undos", document.canvas_undos);
    field(json, "canvas_redos", document.canvas_redos);
    field(json, "canvas_edits", document.canvas_edits);
    field(json, "gestures_cancelled", document.gestures_cancelled);
    json.end_object();
}

void write_evaluator(JsonWriter& json, const EvaluatorCounters& evaluator) {
    json.begin_object();
    field(json, "full_passes", evaluator.full_passes);
    field(json, "time_passes", evaluator.time_passes);
    field(json, "nodes_evaluated", evaluator.nodes_evaluated);
    field(json, "failures", evaluator.failures);
    field(json, "cycle_nodes", evaluator.cycle_nodes);
    field(json, "lamp_writes", evaluator.lamp_writes);
    json.end_object();
}

void write_root(JsonWriter& json, const GraphRootCounters& root) {
    json.begin_object();
    field(json, "edit_hosts_created", root.edit_hosts_created);
    field(json, "edits_opened", root.edits_opened);
    field(json, "edits_declined", root.edits_declined);
    field(json, "edits_ended_by_canvas", root.edits_ended_by_canvas);
    field(json, "edits_committed", root.edits_committed);
    field(json, "edits_cancelled", root.edits_cancelled);
    field(json, "edits_unchanged", root.edits_unchanged);
    field(json, "palette_opened", root.palette_opened);
    field(json, "palette_insertions", root.palette_insertions);
    field(json, "palette_links", root.palette_links);
    field(json, "saves", root.saves);
    field(json, "save_failures", root.save_failures);
    field(json, "status_updates", root.status_updates);
    json.end_object();
}

} // namespace

CounterSnapshot read_counters(GraphRoot& root) {
    CounterSnapshot snapshot{};
    GraphCanvas& canvas = root.canvas();
    const gf::NodeGraph& graph = root.document().graph();
    snapshot.canvas = canvas.statistics();
    snapshot.drag = canvas.drag_statistics();
    snapshot.node_index = canvas.node_index_statistics();
    snapshot.link_index = canvas.link_index_statistics();
    snapshot.graph_canvas = canvas.counters();
    snapshot.document = root.document().counters();
    snapshot.evaluator = root.evaluator().counters();
    snapshot.root = root.counters();
    for (const gf::NodeRecord& node : graph.node_slots()) {
        if (node.live) ++snapshot.live_nodes;
    }
    for (const gf::LinkRecord& link : graph.link_slots()) {
        if (link.live) ++snapshot.live_links;
    }
    snapshot.node_slots = graph.node_slots().size();
    snapshot.link_slots = graph.link_slots().size();
    snapshot.undo_steps = graph.undo_steps();
    snapshot.undo_bytes = graph.undo_bytes();
    snapshot.revision = graph.revision();
    snapshot.semantic_children = canvas.semantic_virtual_children().size();
    snapshot.allocations = gf::allocation_statistics();
    return snapshot;
}

void write_counters(JsonWriter& json, const CounterSnapshot& snapshot) {
    json.begin_object();
    json.key("canvas");
    write_canvas(json, snapshot.canvas);
    json.key("drag");
    write_drag(json, snapshot.drag);
    json.key("node_index");
    write_index(json, snapshot.node_index);
    json.key("link_index");
    write_index(json, snapshot.link_index);
    json.key("graph_canvas");
    write_graph_canvas(json, snapshot.graph_canvas);
    json.key("document");
    write_document(json, snapshot.document);
    json.key("evaluator");
    write_evaluator(json, snapshot.evaluator);
    json.key("root");
    write_root(json, snapshot.root);
    field(json, "live_nodes", static_cast<std::uint64_t>(snapshot.live_nodes));
    field(json, "live_links", static_cast<std::uint64_t>(snapshot.live_links));
    field(json, "node_slots", static_cast<std::uint64_t>(snapshot.node_slots));
    field(json, "link_slots", static_cast<std::uint64_t>(snapshot.link_slots));
    field(json, "undo_steps", static_cast<std::uint64_t>(snapshot.undo_steps));
    field(json, "undo_bytes", static_cast<std::uint64_t>(snapshot.undo_bytes));
    field(json, "revision", snapshot.revision);
    field(json, "semantic_children", static_cast<std::uint64_t>(snapshot.semantic_children));
    json.key("allocations");
    json.raw(snapshot.allocations.to_json());
    json.end_object();
}

void write_generator(JsonWriter& json, const GeneratorReport& report) {
    json.begin_object();
    field(json, "nodes", static_cast<std::uint64_t>(report.nodes));
    field(json, "links", static_cast<std::uint64_t>(report.links));
    field(json, "link_attempts", static_cast<std::uint64_t>(report.link_attempts));
    field(json, "local_links", static_cast<std::uint64_t>(report.local_links));
    field(json, "long_links", static_cast<std::uint64_t>(report.long_links));
    field(json, "skipped_no_port", static_cast<std::uint64_t>(report.skipped_no_port));
    field(json, "reroutes", static_cast<std::uint64_t>(report.reroutes));
    field(json, "grid_columns", static_cast<std::uint64_t>(report.grid_columns));
    field(json, "cell_width", report.cell_width);
    field(json, "cell_height", report.cell_height);
    json.end_object();
}

void write_edits(JsonWriter& json, const EditReport& report) {
    json.begin_object();
    field(json, "applied", static_cast<std::uint64_t>(report.applied));
    field(json, "refused", static_cast<std::uint64_t>(report.refused));
    field(json, "moves", static_cast<std::uint64_t>(report.moves));
    field(json, "property_edits", static_cast<std::uint64_t>(report.property_edits));
    field(json, "additions", static_cast<std::uint64_t>(report.additions));
    field(json, "removals", static_cast<std::uint64_t>(report.removals));
    field(json, "connects", static_cast<std::uint64_t>(report.connects));
    field(json, "disconnects", static_cast<std::uint64_t>(report.disconnects));
    field(json, "title_edits", static_cast<std::uint64_t>(report.title_edits));
    json.end_object();
}

void write_build(JsonWriter& json) {
    json.begin_object();
    json.key("gui_forms_version");
    json.value(NODE_GRAPH_GUI_FORMS_VERSION);
    json.key("build_type");
    json.value(NODE_GRAPH_BUILD_TYPE);
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

std::filesystem::path record_path(const std::filesystem::path& root, std::string_view host,
                                  std::string_view workload, std::uint64_t seed) {
    char seed_text[24]{};
    const int written = std::snprintf(seed_text, sizeof(seed_text), "%016llx", static_cast<unsigned long long>(seed));
    std::string name(workload);
    name.push_back('-');
    if (written > 0) name.append(seed_text, static_cast<std::size_t>(written));
    name.append(".json");
    const std::filesystem::path path = root / "dev" / "measurements" / std::string(host) / name;
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

} // namespace node_graph
