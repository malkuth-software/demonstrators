#pragma once

// The graph editor's canvas (NODE_GRAPH_SPEC sections 3 to 6): GUI.Forms'
// public NodeCanvas (cp31), given the app's meaning. NodeCanvas paints and
// hit-tests the graph's records over the theme's blueprint grid and turns
// input into the graph's named operations: pan and zoom, levels of detail,
// soft or routed links, magnetic link drags through its DragTracker (snap
// radius 16, release radius 24, view units), selection, marquee, moves
// with grid snap, the knife, undo and redo, the keyboard graph and the
// hosted edit. This subclass adds what only the app knows:
//
// - check_link: the socket-type conversion table and the cycle refusal
//   (GraphDocument::check_connection), asked for every magnet candidate and
//   before every connect;
// - paint_body: value bars on unconnected inputs and the kind's live
//   preview (swatch, curve, sparkline, readout) from the evaluator;
// - edit: forwarded to the edit target the window content binds
//   (set_edit_target), which hands back the kind's reused editor;
// - the background variants (paper, blue, green, grey), which are classes
//   of the theme's node canvas (gui-forms theme, cp31), never app colours;
// - evaluation and lamps: the evaluator runs before each paint and after
//   each canvas edit; it writes the output lamps (NodeGraph::set_port_lamp,
//   which moves lamp_revision only), and the canvas paints them;
// - links whose source depends on time are marked flowing (the theme's
//   flow marks), and visible time-driven sinks enlist the canvas on the
//   motion clock; with none visible no frames run (the idle rule);
// - Ctrl+S asks for a save.
//
// Raisers: each of NodeCanvas's per-event raisers is overridden to count
// and to keep the document's counters and autosave in step, then calls the
// base, which raises the event (the window content subscribes to
// palette_requested and edit_ended). A raiser runs after its change and
// outside the canvas's callback scope, so it may use the canvas.
//
// Ownership: a control (shared, owned by its parent). It borrows the
// document's graph, the document and the evaluator, which must outlive it
// (GraphRoot declares them before its children); the destructor detaches
// the graph first. One thread: the window's.

#include "evaluator.hpp"
#include "graph_document.hpp"

#include <gui_forms/controls/node_canvas.hpp>
#include <gui_forms/delegate.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/events/key_events/key_events.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace node_graph {

// The canvas's grid variants: theme classes of the node canvas part table.
enum class BackgroundVariant : std::uint8_t { paper, blue, green, grey };

[[nodiscard]] std::string_view background_class(BackgroundVariant variant) noexcept;

// Counters of what the subclass adds (they only grow, except the `last_`
// fields). NodeCanvas keeps its own (NodeCanvasStatistics).
struct GraphCanvasCounters final {
    // One per raiser call, so a test can see each event's raiser ran.
    std::uint64_t selection_raised{};
    std::uint64_t link_connected_raised{};
    std::uint64_t graph_edited_raised{};
    std::uint64_t view_raised{};
    std::uint64_t palette_raised{};
    std::uint64_t edit_ended_raised{};
    std::uint64_t link_checks{};
    std::uint64_t link_checks_refused{};
    std::uint64_t bodies_painted{};
    std::uint64_t value_bars_painted{};
    std::uint64_t previews_painted{};
    std::uint64_t body_text_skipped{};
    std::uint64_t evaluations{};
    std::uint64_t flow_marks{};
    std::uint64_t ambient_frames{};
    std::uint64_t ambient_enlistments{};
    std::uint64_t background_changes{};
    std::uint64_t save_requests{};
    std::size_t last_frame_live_sinks{};
    std::size_t flowing_links{};
};

// The colours a body paint reads, copied from the canvas look before each
// paint (paint_body runs inside the canvas's callback scope, so it reads
// this copy rather than resolving anything).
struct BodyInk final {
    gui_forms::Color text{};
    gui_forms::Color bar_back{};
    gui_forms::Color bar_fill{};
    gui_forms::Color outline{};
};

class GraphCanvas final : public gui_forms::NodeCanvas {
public:
    // The motion clock's interval for live sinks (spec 4.1: ambient cap).
    static constexpr std::int64_t live_sink_interval_ms = 33;

    GraphCanvas(gui_forms::StableId stable_id, GraphDocument& document, Evaluator& evaluator);
    ~GraphCanvas() override;
    GraphCanvas(const GraphCanvas&) = delete;
    GraphCanvas& operator=(const GraphCanvas&) = delete;

    // The window content's edit target (NodeCanvasCallbacks::edit); empty
    // declines every edit.
    void set_edit_target(gui_forms::Delegate<gui_forms::NodeEditRequest&> target);

    void set_background(BackgroundVariant variant);
    [[nodiscard]] BackgroundVariant background() const noexcept { return background_; }

    // The evaluation time in seconds; tests and workloads set it, otherwise
    // the motion clock's frames advance it.
    void set_time(double seconds);
    [[nodiscard]] double time() const noexcept { return time_; }

    // After the app changed the document outside the canvas (a load, a
    // palette insertion, an edit commit, seeded edits): evaluates, marks the
    // flowing links and refreshes the canvas.
    void document_changed();

    [[nodiscard]] const GraphCanvasCounters& counters() const noexcept { return counters_; }

    // Raised on Ctrl+S.
    [[nodiscard]] gui_forms::Event<>& save_requested() noexcept { return save_requested_; }

    // -- Control ----------------------------------------------------------------------
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect local_damage) override;

protected:
    bool on_ambient_frame(gui_forms::FrameTime now) override;
    void on_selection_changed(const gui_forms::NodeCanvasSelection& arguments) override;
    void on_link_connected(const gui_forms::NodeLinkConnected& arguments) override;
    void on_graph_edited(const gui_forms::NodeGraphEdited& arguments) override;
    void on_view_changed(const gui_forms::SceneViewTransform& arguments) override;
    void on_palette_requested(const gui_forms::NodePaletteRequest& arguments) override;
    void on_edit_ended(const gui_forms::NodeEditEnded& arguments) override;
    void on_key_down(gui_forms::KeyEventArgs& arguments) override;

private:
    // NodeCanvasCallbacks targets.
    void check_link(gui_forms::NodeLinkCheck& check);
    void edit_node(gui_forms::NodeEditRequest& request);
    void paint_body(gui_forms::NodeBodyPaint& request);

    // graph_canvas_body.cpp.
    void paint_value_bars(gui_forms::Painter& painter, std::uint32_t slot, const gui_forms::NodeRecord& node,
                          const NodeState* state, gui_forms::Rect body, double zoom);
    void paint_preview(gui_forms::Painter& painter, std::uint32_t slot, const NodeState& state, gui_forms::Rect body,
                       double zoom);
    void read_body_ink();

    // Evaluates at time_ and, when the structure moved on, marks as flowing
    // exactly the links whose source node depends on time.
    void evaluate_and_mark();
    void collect_live_sinks();
    void enlist_live_sinks();

    GraphDocument& document_;
    Evaluator& evaluator_;
    gui_forms::Delegate<gui_forms::NodeEditRequest&> edit_target_{};
    BackgroundVariant background_{BackgroundVariant::blue};
    double time_{};
    std::optional<gui_forms::FrameTime> clock_start_{};
    bool painted_since_frame_{};
    std::uint64_t flow_revision_{};
    BodyInk ink_{};

    // Per-frame storage, reserved once and reused.
    std::vector<gui_forms::Rect> live_sink_rects_{};
    std::vector<gui_forms::Point> preview_points_{};
    std::string text_{};
    std::string number_{};

    GraphCanvasCounters counters_{};
    gui_forms::Event<> save_requested_{};
};

} // namespace node_graph
