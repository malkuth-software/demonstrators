// The views and their window content, headless: an application with no
// host, a window holding ProfilerRoot over a small folded trace, layout and
// input dispatched by hand. No raster is attached, so nothing is painted
// here (offscreen_paint_tests paints); hit testing in both orientations,
// zoom to a node, the cell and span geometry, picks reaching the other
// views, the table's filter and sort, and the search through the root.

#include "../src/profiler_root.hpp"
#include "test_support.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/window/window.hpp>

#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string_view>

namespace gf = gui_forms;
using flame_graph::CallNodeIndex;
using flame_graph::FlameOrientation;
using flame_graph::FlameView;
using flame_graph::ProfilerRoot;
using flame_graph::TableColumn;
using flame_graph_tests::require;

namespace {

// Weights: main 10 = parse 5 (lex 3, own 2) + render 5.
constexpr std::string_view trace_text = "main;parse;lex 3\n"
                                        "main;parse 2\n"
                                        "main;render 5\n";

std::optional<CallNodeIndex> child_named(const flame_graph::TraceModel& model, CallNodeIndex parent,
                                         std::string_view text) {
    for (const std::uint32_t child : model.children(parent)) {
        if (model.names().text(model.node(CallNodeIndex{child}).name) == text) return CallNodeIndex{child};
    }
    return std::nullopt;
}

struct Fixture final {
    std::shared_ptr<ProfilerRoot> root;
    gf::Window window;

    explicit Fixture(gf::Application& application)
        : root(gf::make_control<ProfilerRoot>(gf::StableId("flame-graph"))),
          window(application, root, gf::Size{1200.0, 800.0}) {
        const flame_graph::LoadReport report = (*root).load_text(trace_text, flame_graph::TraceFormat::folded);
        require(report.loaded, "the folded trace loads");
        window.perform_layout();
    }

    void click(gf::Control& control, gf::Point local) {
        const gf::Point at = control.point_to_window(local);
        gf::PointerEvent move{};
        move.action = gf::PointerAction::move;
        move.position = at;
        static_cast<void>(window.dispatch_pointer(move));
        gf::PointerEvent down{};
        down.action = gf::PointerAction::down;
        down.position = at;
        down.button = gf::PointerButton::primary;
        static_cast<void>(window.dispatch_pointer(down));
        gf::PointerEvent up = down;
        up.action = gf::PointerAction::up;
        static_cast<void>(window.dispatch_pointer(up));
        window.perform_layout();
    }
};

void flame_hit_testing(gf::Application& application) {
    Fixture fixture(application);
    ProfilerRoot& root = *fixture.root;
    FlameView& flame = root.flame();
    const flame_graph::TraceModel& model = root.model();
    const CallNodeIndex main_node = *child_named(model, flame_graph::root_node, "main");
    const CallNodeIndex parse = *child_named(model, main_node, "parse");
    const CallNodeIndex render = *child_named(model, main_node, "render");
    const CallNodeIndex lex = *child_named(model, parse, "lex");
    const gf::Rect plot = flame.plot_rect();
    require(plot.width > 0.0 && plot.height > 0.0, "the flame view is laid out");
    require(flame.orientation() == FlameOrientation::icicle, "the icicle first");
    const double row = FlameView::row_height;
    require(flame.node_at(gf::Point{plot.x + 10.0, plot.y + row * 0.5}) == flame_graph::root_node,
            "the icicle's top row is the root");
    require(flame.node_at(gf::Point{plot.x + plot.width * 0.25, plot.y + row * 2.5}) == parse, "parse left");
    require(flame.node_at(gf::Point{plot.x + plot.width * 0.75, plot.y + row * 2.5}) == render, "render right");
    require(!flame.node_at(gf::Point{plot.x + plot.width * 0.75, plot.y + row * 3.5}).has_value(),
            "nothing above render");
    const std::optional<gf::Rect> lex_cell = flame.cell_rect(lex);
    require(lex_cell.has_value(), "lex has a cell");
    const gf::Point lex_centre{(*lex_cell).x + (*lex_cell).width / 2.0, (*lex_cell).y + row / 2.0};
    require(flame.node_at(lex_centre) == lex, "a cell's centre hits its node");
    require(std::fabs((*lex_cell).width - plot.width * 0.3) < 1.0e-6, "lex is 3 of 10 wide");

    require(flame.zoom_to(render), "zoom to render");
    require(flame.x_range().minimum == 5.0 && flame.x_range().maximum == 10.0, "the range is render's extent");
    require(flame.node_at(gf::Point{plot.x + plot.width * 0.25, plot.y + row * 2.5}) == render,
            "zoomed, render fills the row");
    require(flame.node_at(gf::Point{plot.x + 5.0, plot.y + row * 1.5}) == main_node,
            "an ancestor stays under the zoom");
    flame.reset_zoom();
    require(flame.x_range().minimum == 0.0 && flame.x_range().maximum == 10.0, "reset shows everything");

    flame.set_orientation(FlameOrientation::flame);
    require(flame.node_at(gf::Point{plot.x + 10.0, plot.y + plot.height - row * 0.5}) == flame_graph::root_node,
            "the flame graph's bottom row is the root");
    require(flame.node_at(gf::Point{plot.x + plot.width * 0.1, plot.y + plot.height - row * 3.5}) == lex,
            "callees above");
    root.toggle_orientation();
    require(flame.orientation() == FlameOrientation::icicle && root.counters().orientation_switches == 2U,
            "the root's switch");
}

void picks_and_table(gf::Application& application) {
    Fixture fixture(application);
    ProfilerRoot& root = *fixture.root;
    FlameView& flame = root.flame();
    const flame_graph::TraceModel& model = root.model();
    const CallNodeIndex main_node = *child_named(model, flame_graph::root_node, "main");
    const CallNodeIndex render = *child_named(model, main_node, "render");
    const std::optional<gf::Rect> cell = flame.cell_rect(render);
    require(cell.has_value(), "render has a cell");
    fixture.click(flame, gf::Point{(*cell).x + (*cell).width / 2.0, (*cell).y + 5.0});
    require(root.selection().node == render, "a click in the flame graph selects render");
    require(root.counters().flame_picks == 1U, "one flame pick");
    flame_graph::DetailsTable& table = root.table();
    const std::optional<std::size_t> function = model.function_of(model.node(render).name);
    const std::optional<gf::VirtualRowIndex> current = table.rows().selected_row();
    require(function.has_value() && current.has_value() && table.function_at_row((*current).value) == function,
            "the table follows the pick");
    require(root.counters().table_picks == 0U, "the table's own selection raised no pick");

    // The timeline: lane 0, depth 0 is main over all ten samples.
    flame_graph::TimelineView& timeline = root.timeline();
    const gf::Rect lane_plot = timeline.plot_rect();
    const gf::Point main_point{lane_plot.x + lane_plot.width / 2.0,
                               timeline.lane_top(flame_graph::ThreadIndex{0U}) +
                                   flame_graph::TimelineView::lane_header_height +
                                   flame_graph::TimelineView::row_height / 2.0};
    const std::optional<flame_graph::SpanHit> hit = timeline.span_at(main_point);
    require(hit.has_value() && (*hit).node == main_node, "the timeline's top row is main");
    fixture.click(timeline, main_point);
    require(root.selection().node == main_node && root.counters().timeline_picks == 1U, "a timeline pick selects");

    require(table.row_count() == 4U, "four functions");
    table.set_sort(TableColumn::name, false);
    require(model.names().text(model.functions()[*table.function_at_row(0U)].name) == "lex", "by name: lex first");
    require(model.names().text(model.functions()[*table.function_at_row(3U)].name) == "render",
            "by name: render last");
    table.set_sort(TableColumn::self, true);
    const flame_graph::FunctionRow& top = model.functions()[*table.function_at_row(0U)];
    require(top.self == 5, "by self: render's 5 first");
    root.set_search("PAR");
    require(table.row_count() == 1U, "the search filters the table");
    require(root.search().summary.matched_weight == 5, "parse weighs 5");
    root.set_search("");
    require(table.row_count() == 4U && !root.search().active(), "an empty search shows every row");
    require(table.column_at(table.client_rectangle().width - 1.0) == TableColumn::share, "the last column");
    require(table.column_at(1.0) == TableColumn::name, "the first column");
}

void refused_load(gf::Application& application) {
    Fixture fixture(application);
    ProfilerRoot& root = *fixture.root;
    const flame_graph::LoadReport report = root.load_text("[{\"ph\":", flame_graph::TraceFormat::automatic);
    require(!report.loaded && root.counters().load_failures == 1U, "a broken trace is refused");
    fixture.window.perform_layout();
    require(!root.flame().node_at(gf::Point{10.0, 30.0}).has_value(), "an empty trace hits nothing");
    require(root.table().row_count() == 0U, "an empty table");
}

} // namespace

int main() {
    try {
        gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
        require(static_cast<bool>(created), "the application is made");
        flame_hit_testing(*created.application);
        picks_and_table(*created.application);
        refused_load(*created.application);
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "view_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("view_tests: passed");
    return 0;
}
