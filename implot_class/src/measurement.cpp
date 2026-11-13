#include "measurement.hpp"

#include <gui_forms/allocation.hpp>
#include <gui_forms/window/window.hpp>

#include <fstream>
#include <system_error>

namespace implot_class {

namespace gf = gui_forms;

namespace {

void field(JsonWriter& json, std::string_view name, std::uint64_t value) {
    json.key(name);
    json.value(value);
}

} // namespace

CounterSnapshot read_counters(gf::Window& window, PlotRoot& root) {
    CounterSnapshot snapshot{};
    snapshot.root = root.counters();
    snapshot.heatmap = root.heatmap().statistics();
    snapshot.histogram = root.histogram().statistics();
    snapshot.drag = root.drag().statistics();
    snapshot.feed = root.feed().counters();
    if (root.live_channel() != nullptr) snapshot.channel = (*root.live_channel()).statistics();
    snapshot.live_frames_run = root.live_plot().frames_run();
    snapshot.live_column_copies = root.live_plot().live_column_copies();
    snapshot.subplot_propagations = root.subplots().propagations();
    snapshot.subplot_echoes = root.subplots().echoes();
    snapshot.histogram_rebins = root.histogram().rebins();
    snapshot.metrics = window.metrics_snapshot();
    snapshot.allocations = gf::allocation_statistics();
    return snapshot;
}

void write_frame_statistics(JsonWriter& json, const PlotFrameStatistics& statistics) {
    json.begin_object();
    field(json, "frames_painted", statistics.frames_painted);
    field(json, "mark_calls", statistics.mark_calls);
    field(json, "points_in", statistics.points_in);
    field(json, "points_drawn", statistics.points_drawn);
    field(json, "truncated_calls", statistics.truncated_calls);
    field(json, "view_changes", statistics.view_changes);
    field(json, "content_drags", statistics.content_drags);
    field(json, "tick_generations", statistics.tick_generations);
    field(json, "ticks_truncated", statistics.ticks_truncated);
    field(json, "last_frame_points_drawn", statistics.last_frame_points_drawn);
    json.end_object();
}

void write_counters(JsonWriter& json, const CounterSnapshot& snapshot) {
    json.begin_object();
    json.key("root");
    json.begin_object();
    field(json, "page_switches", snapshot.root.page_switches);
    field(json, "colormap_changes", snapshot.root.colormap_changes);
    field(json, "bin_changes", snapshot.root.bin_changes);
    field(json, "live_toggles", snapshot.root.live_toggles);
    field(json, "sources_withdrawn", snapshot.root.sources_withdrawn);
    field(json, "sources_restored", snapshot.root.sources_restored);
    field(json, "handle_moves", snapshot.root.handle_moves);
    field(json, "status_updates", snapshot.root.status_updates);
    json.end_object();
    json.key("heatmap");
    write_frame_statistics(json, snapshot.heatmap);
    json.key("histogram");
    write_frame_statistics(json, snapshot.histogram);
    json.key("drag");
    write_frame_statistics(json, snapshot.drag);
    json.key("live");
    json.begin_object();
    field(json, "feed_blocks", snapshot.feed.blocks_written);
    field(json, "feed_samples", snapshot.feed.samples_written);
    field(json, "feed_paused_periods", snapshot.feed.paused_periods);
    field(json, "channel_published", snapshot.channel.published);
    field(json, "channel_consumed", snapshot.channel.consumed);
    field(json, "channel_overrun", snapshot.channel.overrun);
    field(json, "frames_run", snapshot.live_frames_run);
    field(json, "column_copies", snapshot.live_column_copies);
    json.end_object();
    field(json, "subplot_propagations", snapshot.subplot_propagations);
    field(json, "subplot_echoes", snapshot.subplot_echoes);
    field(json, "histogram_rebins", snapshot.histogram_rebins);
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
    json.value(IMPLOT_CLASS_GUI_FORMS_VERSION);
    json.key("build_type");
    json.value(IMPLOT_CLASS_BUILD_TYPE);
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
    json.key("gd_export");
#if defined(IMPLOT_CLASS_WITH_GD)
    json.value(true);
#else
    json.value(false);
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
    const std::filesystem::path path = root / "dev" / "measurements" / "implot_class" / std::string(host) / name;
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

} // namespace implot_class
