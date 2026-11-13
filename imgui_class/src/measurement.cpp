#include "measurement.hpp"

#include <gui_forms/allocation.hpp>
#include <gui_forms/window/window.hpp>

#include <fstream>
#include <system_error>

namespace imgui_class {

namespace gf = gui_forms;

namespace {

void field(JsonWriter& json, std::string_view name, std::uint64_t value) {
    json.key(name);
    json.value(value);
}

} // namespace

CounterSnapshot read_counters(gf::Window& window, DemoRoot& root) {
    CounterSnapshot snapshot{};
    snapshot.root = root.counters();
    snapshot.color_field = root.color_field().statistics();
    snapshot.model_advances = root.model().advances();
    snapshot.vector_changes = root.vector_field().changes();
    snapshot.visible_rows = root.visible_rows();
    snapshot.metrics = window.metrics_snapshot();
    snapshot.allocations = gf::allocation_statistics();
    return snapshot;
}

void write_counters(JsonWriter& json, const CounterSnapshot& snapshot) {
    const DemoRootCounters& root = snapshot.root;
    json.begin_object();
    json.key("root");
    json.begin_object();
    field(json, "page_switches", root.page_switches);
    field(json, "button_clicks", root.button_clicks);
    field(json, "check_changes", root.check_changes);
    field(json, "radio_changes", root.radio_changes);
    field(json, "toggle_changes", root.toggle_changes);
    field(json, "combo_changes", root.combo_changes);
    field(json, "list_changes", root.list_changes);
    field(json, "text_changes", root.text_changes);
    field(json, "slider_changes", root.slider_changes);
    field(json, "effect_changes", root.effect_changes);
    field(json, "tree_selections", root.tree_selections);
    field(json, "tree_expansions", root.tree_expansions);
    field(json, "filter_runs", root.filter_runs);
    field(json, "sorts", root.sorts);
    field(json, "advances", root.advances);
    field(json, "grid_cells_written", root.grid_cells_written);
    field(json, "cell_plots_painted", root.cell_plots_painted);
    field(json, "cell_bars_painted", root.cell_bars_painted);
    field(json, "cell_plot_points", root.cell_plot_points);
    field(json, "value_commits", root.value_commits);
    field(json, "vector_changes", root.vector_changes);
    field(json, "colour_changes", root.colour_changes);
    field(json, "colour_text_changes", root.colour_text_changes);
    field(json, "sparkline_pushes", root.sparkline_pushes);
    field(json, "section_toggles", root.section_toggles);
    field(json, "style_compiles", root.style_compiles);
    field(json, "style_refusals", root.style_refusals);
    field(json, "style_warnings", root.style_warnings);
    field(json, "style_unattached", root.style_unattached);
    field(json, "status_updates", root.status_updates);
    json.end_object();
    json.key("color_field");
    json.begin_object();
    field(json, "frames_painted", snapshot.color_field.frames_painted);
    field(json, "gestures", snapshot.color_field.gestures);
    field(json, "changes", snapshot.color_field.changes);
    json.end_object();
    field(json, "model_advances", snapshot.model_advances);
    field(json, "vector_field_changes", snapshot.vector_changes);
    field(json, "visible_rows", static_cast<std::uint64_t>(snapshot.visible_rows));
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
    json.value(IMGUI_CLASS_GUI_FORMS_VERSION);
    json.key("build_type");
    json.value(IMGUI_CLASS_BUILD_TYPE);
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
    const std::filesystem::path path = root / "dev" / "measurements" / "imgui_class" / std::string(host) / name;
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

} // namespace imgui_class
