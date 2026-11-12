// The canvas and its window content, headless: an application with no
// host, a window holding GraphRoot, layout and input dispatched by hand.
// No raster is attached, so nothing is painted here (offscreen_paint_tests
// paints); hit testing, gestures, the DragTracker sessions, the edit hosts
// and the palette are exercised through real pointer and key events.

#include "../src/graph_root.hpp"
#include "../src/kinds.hpp"
#include "test_support.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/window/window.hpp>

#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gf = gui_forms;
using node_graph::GraphRoot;
using node_graph::NodeCanvasView;
using node_graph_tests::require;

namespace {

constexpr gf::Size window_size{1200.0, 800.0};

// One window over a small hand-made graph, the view at world = view (the
// canvas sits at the window's origin, zoom 1):
//   two (constant) at 40,40     sum (add) at 320,40     shown (readout) at 600,40
//   three (constant) at 40,200  spare (constant) at 40,360
struct Fixture final {
    std::shared_ptr<GraphRoot> root;
    gf::Window window;
    gf::NodeId two{};
    gf::NodeId three{};
    gf::NodeId spare{};
    gf::NodeId sum{};
    gf::NodeId shown{};

    explicit Fixture(gf::Application& application)
        : root(gf::make_control<GraphRoot>(gf::StableId("node-graph"))), window(application, root, window_size) {
        node_graph::GraphDocument& document = (*root).document();
        two = add(document, "constant", 40.0, 40.0);
        three = add(document, "constant", 40.0, 200.0);
        spare = add(document, "constant", 40.0, 360.0);
        sum = add(document, "add", 320.0, 40.0);
        shown = add(document, "readout", 600.0, 40.0);
        const node_graph::ConnectResult linked =
            document.connect(gf::PortRef{sum, gf::PortIndex{2U}}, gf::PortRef{shown, gf::PortIndex{0U}});
        require(linked.status == node_graph::ConnectStatus::connected, "sum -> shown");
        // The fixture's own edits are the start: nothing to undo.
        // Limits of zero steps trim the log (as the generator does).
        gf::NodeGraph& graph = document.graph_for_generation();
        const gf::UndoLimits limits = graph.undo_limits();
        graph.set_undo_limits(gf::UndoLimits{0U, limits.maximum_bytes});
        graph.set_undo_limits(limits);
        require(graph.undo_steps() == 0U, "the fixture starts with nothing to undo");
        canvas().document_changed();
        canvas().set_view(node_graph::ViewTransform{});
        window.perform_layout();
    }

    static gf::NodeId add(node_graph::GraphDocument& document, const char* kind, double x, double y) {
        const std::optional<node_graph::KindIndex> index = node_graph::find_kind(kind);
        require(index.has_value(), "the kind exists");
        const gf::GraphResult result = document.add_node(*index, gf::WorldPoint{x, y});
        require(result.status == gf::GraphStatus::ok, "the fixture's node is added");
        return result.node;
    }

    NodeCanvasView& canvas() { return (*root).canvas(); }
    std::string saved() {
        std::string text{};
        (*root).document().save_text(text);
        return text;
    }
    gf::Point caption(gf::NodeId node) {
        const gf::NodeRecord* record = (*root).document().graph().node(node);
        require(record != nullptr, "the node is live");
        const gf::Rect rect = canvas().view().rect_to_view((*record).bounds);
        return gf::Point{rect.x + rect.width / 2.0, rect.y + 12.0};
    }
    gf::Point body(gf::NodeId node) {
        const gf::NodeRecord* record = (*root).document().graph().node(node);
        require(record != nullptr, "the node is live");
        const gf::Rect rect = canvas().view().rect_to_view((*record).bounds);
        return gf::Point{rect.x + rect.width / 2.0, rect.y + rect.height - 10.0};
    }
    gf::Point anchor(gf::NodeId node, std::uint16_t port) {
        const std::optional<gf::Point> point = canvas().port_view_anchor(gf::PortRef{node, gf::PortIndex{port}});
        require(point.has_value(), "the port has an anchor");
        return *point;
    }
};

void pointer(gf::Window& window, gf::PointerAction action, gf::Point position, gf::PointerButton button,
             gf::Modifier modifiers, std::uint32_t clicks) {
    gf::PointerEvent event{};
    event.action = action;
    event.position = position;
    event.modifiers = modifiers;
    if (action != gf::PointerAction::move) {
        event.button = button;
        event.click_count = clicks;
    }
    static_cast<void>(window.dispatch_pointer(event));
}

void click(gf::Window& window, gf::Point at, gf::Modifier modifiers) {
    pointer(window, gf::PointerAction::down, at, gf::PointerButton::primary, modifiers, 1U);
    pointer(window, gf::PointerAction::up, at, gf::PointerButton::primary, modifiers, 1U);
    window.perform_layout();
}

void double_click(gf::Window& window, gf::Point at) {
    click(window, at, gf::Modifier::none);
    pointer(window, gf::PointerAction::down, at, gf::PointerButton::primary, gf::Modifier::none, 2U);
    pointer(window, gf::PointerAction::up, at, gf::PointerButton::primary, gf::Modifier::none, 2U);
    window.perform_layout();
}

void drag(gf::Window& window, gf::Point from, gf::Point to, gf::PointerButton button, gf::Modifier modifiers) {
    pointer(window, gf::PointerAction::down, from, button, modifiers, 1U);
    constexpr std::size_t steps = 12U;
    for (std::size_t step = 1U; step <= steps; ++step) {
        const double share = static_cast<double>(step) / static_cast<double>(steps);
        const gf::Point at{from.x + (to.x - from.x) * share, from.y + (to.y - from.y) * share};
        pointer(window, gf::PointerAction::move, at, button, modifiers, 0U);
    }
    pointer(window, gf::PointerAction::up, to, button, modifiers, 1U);
    window.perform_layout();
}

void key(gf::Window& window, std::uint32_t physical_key, gf::Modifier modifiers) {
    gf::KeyEvent down{};
    down.action = gf::KeyAction::down;
    down.physical_key = physical_key;
    down.modifiers = modifiers;
    static_cast<void>(window.dispatch_key(down));
    gf::KeyEvent up = down;
    up.action = gf::KeyAction::up;
    static_cast<void>(window.dispatch_key(up));
    window.perform_layout();
}

void selection_by_click_and_marquee(gf::Application& application) {
    Fixture fixture(application);
    NodeCanvasView& canvas = fixture.canvas();
    click(fixture.window, fixture.body(fixture.two), gf::Modifier::none);
    require(canvas.selection().size() == 1U && canvas.selected(fixture.two), "a click selects one node");
    click(fixture.window, fixture.body(fixture.three), gf::Modifier::shift);
    require(canvas.selection().size() == 2U && canvas.selected(fixture.three), "Shift+click adds");
    click(fixture.window, fixture.body(fixture.two), gf::Modifier::control);
    require(canvas.selection().size() == 1U && !canvas.selected(fixture.two), "Ctrl+click toggles");
    click(fixture.window, gf::Point{1000.0, 700.0}, gf::Modifier::none);
    require(canvas.selection().empty(), "a click on empty canvas clears the selection");
    // A marquee around the three constants (empty start, empty end).
    drag(fixture.window, gf::Point{20.0, 20.0}, gf::Point{260.0, 460.0}, gf::PointerButton::primary, gf::Modifier::none);
    require(canvas.selection().size() == 3U, "the marquee selects the nodes inside it");
    require(canvas.statistics().marquee_selections == 1U, "the marquee is counted");
}

void drag_is_one_undo_step(gf::Application& application) {
    Fixture fixture(application);
    NodeCanvasView& canvas = fixture.canvas();
    const gf::NodeGraph& graph = (*fixture.root).document().graph();
    const gf::WorldRect before = (*graph.node(fixture.sum)).bounds;
    const gf::Point from = fixture.caption(fixture.sum);
    drag(fixture.window, from, gf::Point{from.x + 64.0, from.y + 48.0}, gf::PointerButton::primary, gf::Modifier::none);
    const gf::WorldRect after = (*graph.node(fixture.sum)).bounds;
    require(after.x != before.x && after.y != before.y, "the node moved");
    require(std::fmod(after.x, NodeCanvasView::grid_spacing) == 0.0 && std::fmod(after.y, NodeCanvasView::grid_spacing) == 0.0,
            "a moved node lands on the grid");
    require(graph.undo_steps() == 1U, "the whole drag is one undo step");
    require(canvas.statistics().moves_committed == 1U, "one move committed");
    require(canvas.statistics().clicks_suppressed >= 1U, "the click after a drag is suppressed");
    require(canvas.selection().size() == 1U && canvas.selected(fixture.sum), "the dragged node is selected");

    require(fixture.window.request_focus((*fixture.root).canvas_pointer()), "the canvas takes focus");
    key(fixture.window, gf::PhysicalKey::z, gf::Modifier::control);
    require((*graph.node(fixture.sum)).bounds.x == before.x, "Ctrl+Z puts it back");
    key(fixture.window, gf::PhysicalKey::y, gf::Modifier::control);
    require((*graph.node(fixture.sum)).bounds.x == after.x, "Ctrl+Y moves it again");
    key(fixture.window, gf::PhysicalKey::z, gf::Modifier::control);
    require((*graph.node(fixture.sum)).bounds.x == before.x, "Ctrl+Z again puts it back");
    key(fixture.window, gf::PhysicalKey::z, gf::Modifier::control | gf::Modifier::shift);
    require((*graph.node(fixture.sum)).bounds.x == after.x, "Ctrl+Shift+Z redoes too");
}

void magnet_connects_and_palette_follows_a_miss(gf::Application& application) {
    Fixture fixture(application);
    NodeCanvasView& canvas = fixture.canvas();
    const gf::NodeGraph& graph = (*fixture.root).document().graph();
    const gf::Point output = fixture.anchor(fixture.two, 0U);
    const gf::Point input = fixture.anchor(fixture.sum, 0U);
    // Released 10 units off the input: inside the 16-unit capture radius.
    drag(fixture.window, output, gf::Point{input.x - 6.0, input.y + 8.0}, gf::PointerButton::primary, gf::Modifier::none);
    const gf::LinkRecord* link = graph.link(graph.link_into(gf::PortRef{fixture.sum, gf::PortIndex{0U}}));
    require(link != nullptr, "the magnet connected the link");
    require(canvas.statistics().magnet_captures >= 1U && canvas.statistics().connections_made == 1U,
            "the capture and the connection are counted");
    require(graph.undo_steps() == 1U, "the connection is one undo step");

    // A release on empty canvas asks for the palette, filtered by the type.
    const gf::Point spare_output = fixture.anchor(fixture.spare, 0U);
    drag(fixture.window, spare_output, gf::Point{700.0, 500.0}, gf::PointerButton::primary, gf::Modifier::none);
    GraphRoot& root = *fixture.root;
    require(root.palette_open(), "a missed release opens the palette");
    require(!root.palette_kinds().empty(), "the palette lists kinds");
    for (const node_graph::KindIndex kind : root.palette_kinds()) {
        gf::PortIndex accepted{};
        require(node_graph::kind_accepts(node_graph::kind_at(kind), 0U, accepted), "every listed kind accepts a float");
    }
    const std::size_t steps = graph.undo_steps();
    require(root.insert_from_palette(0U), "the first kind is inserted");
    require(!root.palette_open(), "inserting closes the palette");
    require(root.counters().palette_links == 1U, "the new node is connected to the dragged port");
    require(graph.undo_steps() == steps + 1U, "insertion and connection are one undo step");
}

void edit_hosts(gf::Application& application) {
    Fixture fixture(application);
    GraphRoot& root = *fixture.root;
    const gf::NodeGraph& graph = root.document().graph();
    double_click(fixture.window, fixture.caption(fixture.two));
    require(root.edit_host() != nullptr, "a double-click on a caption opens the edit host");
    root.commit_edit("Two");
    require((*graph.node(fixture.two)).title == "Two", "Enter commits the title");
    require(graph.undo_steps() == 1U, "the commit is one undo step");
    require(root.edit_host() == nullptr, "the host closes on commit");

    const std::string before = fixture.saved();
    double_click(fixture.window, fixture.caption(fixture.three));
    require(root.edit_host() != nullptr, "a second constant opens the same kind's host");
    root.cancel_edit();
    require(fixture.saved() == before, "Escape leaves the document byte-identical");
    require(root.counters().edit_hosts_created == 1U, "one host for the constant kind, made once");

    // The body of a node with a primary property edits that property.
    double_click(fixture.window, fixture.body(fixture.sum));
    require(root.edit_host() != nullptr, "a double-click on a body opens the host");
    root.commit_edit("4");
    const gf::NodeProperty* b = graph.property(fixture.sum, "b");
    require(b != nullptr && (*b).value == "4", "the primary property took the text");
    require(root.counters().edit_hosts_created == 2U, "a second kind made a second host");
    // An unchanged commit records nothing.
    const std::size_t steps = graph.undo_steps();
    double_click(fixture.window, fixture.body(fixture.sum));
    root.commit_edit("4");
    require(graph.undo_steps() == steps, "an unchanged commit is no step");
}

void delete_key_and_knife(gf::Application& application) {
    Fixture fixture(application);
    NodeCanvasView& canvas = fixture.canvas();
    const gf::NodeGraph& graph = (*fixture.root).document().graph();
    click(fixture.window, fixture.body(fixture.spare), gf::Modifier::none);
    require(fixture.window.request_focus((*fixture.root).canvas_pointer()), "the canvas takes focus");
    key(fixture.window, gf::PhysicalKey::delete_forward, gf::Modifier::none);
    require(graph.node(fixture.spare) == nullptr, "Delete removes the selection");
    require(canvas.statistics().key_presses_suppressed >= 1U, "the consumed key suppresses its key press");

    // The knife (Ctrl + secondary drag) across the sum -> shown link.
    const gf::Point from = fixture.anchor(fixture.sum, 2U);
    const gf::Point to = fixture.anchor(fixture.shown, 0U);
    const gf::Point middle{(from.x + to.x) / 2.0, (from.y + to.y) / 2.0};
    std::vector<gf::LinkId> brute{};
    std::vector<gf::LinkId> culled{};
    const gf::Point cut_from{middle.x, middle.y - 60.0};
    const gf::Point cut_to{middle.x, middle.y + 60.0};
    canvas.links_crossing(cut_from, cut_to, false, brute);
    canvas.links_crossing(cut_from, cut_to, true, culled);
    require(brute.size() == 1U && culled == brute, "the culled crossing test agrees with brute force");
    drag(fixture.window, cut_from, cut_to, gf::PointerButton::secondary, gf::Modifier::control);
    require(graph.link(brute.front()) == nullptr, "the knife cut the link");
    require(canvas.statistics().knife_cuts >= 1U, "the cut is counted");
    require(!(*fixture.root).menu().is_open(), "the knife's release opens no context menu");
}

} // namespace

int main() {
    try {
        gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
        require(static_cast<bool>(created), "the application is made");
        gf::Application& application = *created.application;
        selection_by_click_and_marquee(application);
        drag_is_one_undo_step(application);
        magnet_connects_and_palette_follows_a_miss(application);
        edit_hosts(application);
        delete_key_and_knife(application);
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "canvas_tests: %s\n", failure.what());
        return 1;
    }
    std::printf("canvas_tests: ok\n");
    return 0;
}
