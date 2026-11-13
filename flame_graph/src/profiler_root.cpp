#include "profiler_root.hpp"

#include "profile_ink.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace flame_graph {

namespace gf = gui_forms;

ProfilerRoot::ProfilerRoot(gf::StableId stable_id) : gf::Control(std::move(stable_id)) {
    status_text_.reserve(256U);
    colors_.reserve(4096U);
}

ProfilerRoot::~ProfilerRoot() {
    // The views may outlive this control (shared children): nothing of
    // ours stays borrowed.
    const ProfileInputs none{};
    if (flame_ != nullptr) (*flame_).set_inputs(none);
    if (timeline_ != nullptr) (*timeline_).set_inputs(none);
    if (table_ != nullptr) (*table_).set_inputs(none);
}

void ProfilerRoot::initialize_control_tree() {
    search_box_ = gf::make_control<gf::TextBox>(gf::StableId("flame-graph.search"));
    orientation_button_ = gf::make_control<gf::Button>(gf::StableId("flame-graph.orientation"), "Flame graph");
    reset_button_ = gf::make_control<gf::Button>(gf::StableId("flame-graph.reset"), "Reset zoom");
    flame_ = gf::make_control<FlameView>(gf::StableId("flame-graph.flame"));
    timeline_ = gf::make_control<TimelineView>(gf::StableId("flame-graph.timeline"));
    table_ = gf::make_control<DetailsTable>(gf::StableId("flame-graph.table"));
    status_ = gf::make_control<gf::Label>(gf::StableId("flame-graph.status"));
    add_child(search_box_);
    add_child(orientation_button_);
    add_child(reset_button_);
    add_child(flame_);
    add_child(timeline_);
    add_child(table_);
    add_child(status_);
    gf::on((*search_box_).text_changed(), *this, &ProfilerRoot::on_search_text);
    gf::on((*orientation_button_).clicked(), *this, &ProfilerRoot::on_orientation_clicked);
    gf::on((*reset_button_).clicked(), *this, &ProfilerRoot::on_reset_clicked);
    gf::on((*flame_).node_picked(), *this, &ProfilerRoot::on_flame_pick);
    gf::on((*timeline_).node_picked(), *this, &ProfilerRoot::on_timeline_pick);
    gf::on((*table_).function_picked(), *this, &ProfilerRoot::on_table_pick);
    gf::on((*table_).function_activated(), *this, &ProfilerRoot::on_table_activated);
    push_inputs();
    refresh_status();
}

void ProfilerRoot::push_inputs() {
    ProfileInputs inputs{};
    inputs.model = &model_;
    inputs.colors = &colors_;
    inputs.search = &search_;
    inputs.selection = &selection_;
    (*flame_).set_inputs(inputs);
    (*timeline_).set_inputs(inputs);
    (*table_).set_inputs(inputs);
}

// -- loading ------------------------------------------------------------------------------------

void ProfilerRoot::model_loaded() {
    fill_frame_colors(model_.names(), colors_);
    // A copy: search() reassigns search_.query from the view it is given.
    const std::string query = search_.query;
    model_.search(query, search_);
    selection_ = ProfileSelection{};
    (*flame_).model_changed();
    (*timeline_).model_changed();
    (*table_).model_changed();
    refresh_status();
}

LoadReport ProfilerRoot::load_text(std::string_view text, TraceFormat format) {
    last_report_ = load_trace_text(text, format, model_);
    if (last_report_.loaded) {
        ++counters_.loads;
    } else {
        ++counters_.load_failures;
    }
    model_loaded();
    return last_report_;
}

LoadReport ProfilerRoot::load_file(const std::filesystem::path& path, TraceFormat format) {
    last_report_ = load_trace_file(path, format, model_, load_buffer_);
    if (last_report_.loaded) {
        ++counters_.loads;
    } else {
        ++counters_.load_failures;
    }
    model_loaded();
    return last_report_;
}

void ProfilerRoot::generate(const GeneratorOptions& options) {
    generate_trace(options, model_);
    last_report_ = LoadReport{};
    last_report_.loaded = true;
    ++counters_.loads;
    model_loaded();
}

// -- search, selection, view --------------------------------------------------------------------

void ProfilerRoot::set_search(std::string_view query) {
    model_.search(query, search_);
    ++counters_.searches;
    (*flame_).inputs_changed();
    (*timeline_).inputs_changed();
    (*table_).search_changed();
    refresh_status();
}

void ProfilerRoot::select(const ProfileSelection& selection) {
    selection_ = selection;
    if (selection_.node.has_value() && (*selection_.node).value >= model_.nodes().size()) selection_.node.reset();
    if (selection_.node.has_value() && !selection_.name.has_value()) {
        selection_.name = model_.node(*selection_.node).name;
    }
    std::optional<std::size_t> function{};
    if (selection_.name.has_value()) function = model_.function_of(*selection_.name);
    (*table_).select_function(function);
    (*table_).selection_changed_outside();
    (*flame_).inputs_changed();
    (*timeline_).inputs_changed();
    refresh_status();
}

void ProfilerRoot::set_orientation(FlameOrientation orientation) {
    if ((*flame_).orientation() == orientation) return;
    (*flame_).set_orientation(orientation);
    ++counters_.orientation_switches;
    // The button names the mode it switches to.
    (*orientation_button_).text = orientation == FlameOrientation::icicle ? "Flame graph" : "Icicle";
}

void ProfilerRoot::toggle_orientation() {
    const FlameOrientation next =
        (*flame_).orientation() == FlameOrientation::icicle ? FlameOrientation::flame : FlameOrientation::icicle;
    set_orientation(next);
}

void ProfilerRoot::reset_zoom() {
    (*flame_).reset_zoom();
    (*timeline_).reset_view();
    ++counters_.zoom_resets;
}

void ProfilerRoot::refresh_status() {
    ++counters_.status_updates;
    status_text_.clear();
    if (!last_report_.loaded) {
        status_text_.assign("No trace");
        if (!last_report_.message.empty()) {
            status_text_.append(": ");
            status_text_.append(last_report_.message);
        }
    } else {
        const TraceStatistics& statistics = model_.statistics();
        FormattedNumber number{};
        format_count(model_.threads().size(), number);
        status_text_.append(number.text());
        status_text_.append(" threads, ");
        format_count(statistics.spans, number);
        status_text_.append(number.text());
        status_text_.append(" spans, ");
        format_count(statistics.nodes, number);
        status_text_.append(number.text());
        status_text_.append(" nodes, ");
        format_count(statistics.functions, number);
        status_text_.append(number.text());
        status_text_.append(" functions");
        if (search_.active()) {
            status_text_.append("; search: ");
            format_count(search_.summary.nodes_matched, number);
            status_text_.append(number.text());
            status_text_.append(" nodes, ");
            format_share(search_.summary.matched_weight, model_.total_weight(), number);
            status_text_.append(number.text());
        }
        if (selection_.name.has_value()) {
            status_text_.append("; selected ");
            status_text_.append(model_.names().text(*selection_.name));
            if (selection_.node.has_value()) {
                const CallNode& node = model_.node(*selection_.node);
                format_weight(node.total, model_.unit(), number);
                status_text_.append(" total ");
                status_text_.append(number.text());
                format_weight(node.self, model_.unit(), number);
                status_text_.append(", self ");
                status_text_.append(number.text());
            }
        }
    }
    (*status_).text = status_text_;
}

// -- events -------------------------------------------------------------------------------------

void ProfilerRoot::on_search_text(const std::string& text) {
    set_search(text);
}

void ProfilerRoot::on_orientation_clicked(gf::ButtonBase& button) {
    static_cast<void>(button);
    toggle_orientation();
}

void ProfilerRoot::on_reset_clicked(gf::ButtonBase& button) {
    static_cast<void>(button);
    reset_zoom();
}

void ProfilerRoot::on_flame_pick(const NodePick& pick) {
    ++counters_.flame_picks;
    ProfileSelection selection{};
    selection.node = pick.node;
    selection.name = pick.name;
    select(selection);
}

void ProfilerRoot::on_timeline_pick(const NodePick& pick) {
    ++counters_.timeline_picks;
    ProfileSelection selection{};
    selection.node = pick.node;
    selection.name = pick.name;
    select(selection);
}

void ProfilerRoot::on_table_pick(const FunctionPick& pick) {
    ++counters_.table_picks;
    ProfileSelection selection{};
    selection.name = pick.name;
    select(selection);
}

void ProfilerRoot::on_table_activated(const FunctionPick& pick) {
    ++counters_.table_activations;
    // The box shows the name; the search is run here too, whether or not
    // the box raises text_changed for a text the program set.
    const std::string name(model_.names().text(pick.name));
    (*search_box_).text = name;
    set_search(name);
}

// -- layout -------------------------------------------------------------------------------------

gf::Size ProfilerRoot::measure(gf::Size available) {
    return available;
}

void ProfilerRoot::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    const double width = final_bounds.width;
    const double body = std::max(0.0, final_bounds.height - toolbar_height - status_height);
    const double flame_height = std::floor(body * flame_share);
    const double timeline_height = std::floor(body * timeline_share);
    const double table_height = std::max(0.0, body - flame_height - timeline_height);
    const double control_y = 4.0;
    const double control_height = toolbar_height - 8.0;
    set_child_layout(search_box_, gf::Rect{6.0, control_y, search_width, control_height});
    const double buttons_x = 6.0 + search_width + 8.0;
    set_child_layout(orientation_button_, gf::Rect{buttons_x, control_y, button_width, control_height});
    set_child_layout(reset_button_, gf::Rect{buttons_x + button_width + 6.0, control_y, button_width, control_height});
    double y = toolbar_height;
    set_child_layout(flame_, gf::Rect{0.0, y, width, flame_height});
    y += flame_height;
    set_child_layout(timeline_, gf::Rect{0.0, y, width, timeline_height});
    y += timeline_height;
    set_child_layout(table_, gf::Rect{0.0, y, width, table_height});
    y += table_height;
    set_child_layout(status_, gf::Rect{0.0, y, width, status_height});
}

} // namespace flame_graph
