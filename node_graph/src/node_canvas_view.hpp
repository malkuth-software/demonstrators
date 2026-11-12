#pragma once

// The graph editor's canvas (NODE_GRAPH_SPEC sections 3 to 6): a custom
// GUI.Forms control that paints and hit-tests the document's records
// (nodes, ports and links are records, never child controls) over a
// world-aligned blueprint grid, and turns pointer and key input into the
// document's named operations.
//
// GUI.Forms has no public NodeCanvas control yet (phase N1), so this is the
// app's own canvas built on public pieces: NodeGraph, LinkRouter, the
// public DragTracker (with its magnet: snap radius 16, release radius 24),
// the per-event raisers in the owner's input order (MouseUp before Click;
// suppress_click after a drag so the release selects nothing;
// suppress_key_press for the keys the canvas consumes), the grid levels of
// the Painter vocabulary and the paragraph layout for node titles. What it
// had to build itself is listed in GAPS.md.
//
// Gestures, each a DragTracker session (one at a time):
//   move     primary press on a node body; the selection moves, grid snap on
//            by default and Ctrl inverts it; one undo step per drag.
//   link     primary press on an output port (or on a connected input, which
//            detaches its link into the drag); compatible inputs nearby are
//            the magnet's targets; release on one connects, release elsewhere
//            asks for the palette filtered to compatible kinds.
//   marquee  primary press on empty canvas; full containment, Alt touching.
//   knife    Ctrl + secondary press; cuts the links the stroke crosses.
//   pan      middle press, or Space held with a primary press.
// Escape during any of them ends it (the window routes Escape to the drag
// engine first) and restores what it changed.
//
// Ownership: the canvas is a control (shared, owned by its parent); it
// borrows the document and the evaluator, which must outlive it (the window
// declares them first). Its DragTracker is declared last so that it ends
// first. One thread: the window's.

#include "canvas_geometry.hpp"
#include "evaluator.hpp"
#include "graph_document.hpp"

#include <gui_forms/control.hpp>
#include <gui_forms/controls/link_router.hpp>
#include <gui_forms/controls/node_graph.hpp>
#include <gui_forms/drag/drag_tracker/drag_tracker.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/events/key_events/key_events.hpp>
#include <gui_forms/events/mouse_events/mouse_events.hpp>
#include <gui_forms/text/paragraph_layout/paragraph_layout.hpp>
#include <gui_forms/types/paint_marks/paint_marks.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace node_graph {

enum class LinkStyle : std::uint8_t { soft, routed };

// The background variants (spec 3: white paper, blue, green, grey). The
// spec wants them as theme variants of `gui-forms`; a custom control has no
// theme part for a canvas region yet (GAPS.md G2), so they are app colours.
enum class BackgroundVariant : std::uint8_t { paper, blue, green, grey };

enum class CanvasHitKind : std::uint8_t { none, node_body, node_caption, port, link };

struct CanvasHit final {
    CanvasHitKind kind{CanvasHitKind::none};
    gui_forms::NodeId node{};
    gui_forms::PortIndex port{};
    gui_forms::LinkId link{};
};

enum class Gesture : std::uint8_t { none, move, link, marquee, knife, pan };

// The canvas asks its owner to open the edit host over a node (double-click,
// Enter or F2); `title` asks for the caption (rename) rather than the body.
struct EditRequest final {
    gui_forms::NodeId node{};
    bool title{};
};

// The canvas asks its owner for the add-node palette at `world` (shown at
// `window_position`): after a double-click on empty canvas, or a link drag
// released off every port, in which case `from` is the dragged output and
// the palette lists only kinds with a compatible input.
struct PaletteRequest final {
    gui_forms::WorldPoint world{};
    gui_forms::Point window_position{};
    std::optional<gui_forms::PortRef> from{};
};

// Plain-data statistics (they only grow, except the `last_` and current
// fields). Interval timing is the windowed workloads' (docs/WORKLOADS.md).
struct CanvasStatistics final {
    std::uint64_t frames_painted{};
    std::uint64_t nodes_painted{};
    std::uint64_t links_painted{};
    std::uint64_t links_culled{};
    std::uint64_t link_points_flattened{};
    std::uint64_t titles_laid_out{};
    std::uint64_t title_cache_hits{};
    std::uint64_t title_lines_drawn{};
    std::uint64_t text_runs_drawn{};
    std::uint64_t text_skipped_small{};
    std::uint64_t hit_tests{};
    std::uint64_t hit_test_link_checks{};
    std::uint64_t index_rebuilds{};
    std::uint64_t gestures_started{};
    std::uint64_t moves_committed{};
    std::uint64_t gestures_cancelled{};
    std::uint64_t link_drags{};
    std::uint64_t magnet_candidates{};
    std::uint64_t magnet_captures{};
    std::uint64_t connections_made{};
    std::uint64_t connections_refused{};
    std::uint64_t links_detached{};
    std::uint64_t marquee_selections{};
    std::uint64_t knife_cuts{};
    std::uint64_t palette_requests{};
    std::uint64_t edit_requests{};
    std::uint64_t clicks_suppressed{};
    std::uint64_t key_presses_suppressed{};
    std::uint64_t ambient_frames{};
    std::uint64_t ambient_enlistments{};
    std::uint64_t router_runs{};
    std::uint64_t router_expanded{};
    std::uint64_t router_storage_growths{};
    std::uint64_t router_fallbacks{};
    gui_forms::LinkRouteStatistics last_route{};
    std::size_t last_frame_nodes{};
    std::size_t last_frame_links{};
    std::size_t last_frame_live_sinks{};
    LevelOfDetail level{LevelOfDetail::full};
    std::uint64_t level_transitions{};
};

// A title laid out once per node, font size and title text, and reused
// while those hold (GAPS.md G4: no per-record text cache facility).
struct TitleLayout final {
    std::uint32_t generation{};
    std::uint64_t title_hash{};
    double font_size{};
    double width{};
    std::size_t line_count{};
    std::array<gui_forms::ParagraphLine, 2> lines{};
    bool valid{};
};

// One compatible input offered to the magnet during a link drag.
struct LinkCandidate final {
    gui_forms::PortRef port{};
    gui_forms::Point view_anchor{};
    double distance{};
};

class NodeCanvasView final : public gui_forms::Control {
public:
    // World units between grid lines at level 0 (the snap grid).
    static constexpr double grid_spacing = 16.0;
    // View units, zoom-independent (spec 4.2).
    static constexpr double magnet_capture_radius = 16.0;
    static constexpr double magnet_release_radius = 24.0;
    static constexpr double port_hit_radius = 8.0;
    static constexpr double link_hit_distance = 4.0;
    // The visible area plus this margin (view units) supplies link targets.
    static constexpr double candidate_margin = 200.0;
    // The most link points one flattening writes.
    static constexpr std::size_t maximum_link_points = 1024U;

    NodeCanvasView(gui_forms::StableId stable_id, GraphDocument& document, Evaluator& evaluator);
    ~NodeCanvasView() override;
    NodeCanvasView(const NodeCanvasView&) = delete;
    NodeCanvasView& operator=(const NodeCanvasView&) = delete;

    // -- view ---------------------------------------------------------------------------
    [[nodiscard]] const ViewTransform& view() const noexcept { return view_; }
    void set_view(const ViewTransform& view);
    void zoom_about(gui_forms::Point local, double factor);
    void pan_by(gui_forms::Point delta);
    void frame_all();
    void frame_selection();
    void set_link_style(LinkStyle style);
    [[nodiscard]] LinkStyle link_style() const noexcept { return link_style_; }
    void set_background(BackgroundVariant variant);
    [[nodiscard]] BackgroundVariant background() const noexcept { return background_; }
    void set_snap_to_grid(bool snap) noexcept { snap_ = snap; }
    // The evaluation time in seconds; tests and workloads set it, otherwise
    // the motion clock's frames advance it.
    void set_time(double seconds);
    [[nodiscard]] double time() const noexcept { return time_; }

    // -- selection --------------------------------------------------------------------
    [[nodiscard]] const std::vector<gui_forms::NodeId>& selection() const& noexcept { return selection_; }
    const std::vector<gui_forms::NodeId>& selection() const&& = delete;
    [[nodiscard]] bool selected(gui_forms::NodeId node) const noexcept;
    [[nodiscard]] std::optional<gui_forms::LinkId> selected_link() const noexcept { return selected_link_; }
    void select_only(gui_forms::NodeId node);
    void add_to_selection(gui_forms::NodeId node);
    void toggle_selection(gui_forms::NodeId node);
    void clear_selection();
    void select_all();
    // Selects the live nodes in `world` (wholly inside, or touching).
    std::size_t select_in_rect(const gui_forms::WorldRect& world, bool touching, bool add);

    // -- operations (each one undo step) ---------------------------------------------
    void delete_selection();
    void undo();
    void redo();
    // Disconnects every link the view segment from-to crosses; returns how
    // many it cut.
    std::size_t cut_links(gui_forms::Point from, gui_forms::Point to);
    // Connects `from` to the nearest compatible free input of another node
    // (the keyboard link, key L, without the target list: GAPS.md G13).
    bool link_to_nearest(gui_forms::PortRef from);
    // Called after the owner changed the document (a load, an edit-host
    // commit, a palette insertion): drops caches, evaluates, repaints.
    void document_changed();

    // -- hit testing --------------------------------------------------------------------
    [[nodiscard]] CanvasHit hit_test(gui_forms::Point local);
    // Links whose drawn path crosses the view segment, by brute force over
    // every link (the knife's oracle in tests) or culled (the canvas's own).
    void links_crossing(gui_forms::Point from, gui_forms::Point to, bool cull, std::vector<gui_forms::LinkId>& out);
    // A port's anchor in view units, or none for a stale port.
    [[nodiscard]] std::optional<gui_forms::Point> port_view_anchor(gui_forms::PortRef port) const;

    // -- events ---------------------------------------------------------------------------
    [[nodiscard]] gui_forms::Event<const EditRequest&>& edit_requested() noexcept { return edit_requested_; }
    [[nodiscard]] gui_forms::Event<const PaletteRequest&>& palette_requested() noexcept { return palette_requested_; }
    // Raised after every change the canvas made to the document or the view.
    [[nodiscard]] gui_forms::Event<>& changed() noexcept { return changed_; }
    [[nodiscard]] gui_forms::Event<>& save_requested() noexcept { return save_requested_; }

    // -- statistics ---------------------------------------------------------------------
    [[nodiscard]] const CanvasStatistics& statistics() const noexcept { return statistics_; }
    [[nodiscard]] gui_forms::DragTrackerStatistics drag_statistics() const;
    [[nodiscard]] Gesture gesture() const noexcept { return gesture_; }
    [[nodiscard]] const SpatialIndexStatistics& index_statistics() const noexcept { return index_.statistics(); }
    [[nodiscard]] const std::vector<std::uint32_t>& visible_nodes() const& noexcept { return visible_; }
    const std::vector<std::uint32_t>& visible_nodes() const&& = delete;

    // -- Control --------------------------------------------------------------------------
    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    [[nodiscard]] gui_forms::SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<gui_forms::SemanticNode> semantic_virtual_children() const override;
    bool on_ambient_frame(gui_forms::FrameTime now) override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect local_damage) override;

protected:
    void on_mouse_down(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_move(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_up(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_wheel(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_click(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_double_click(gui_forms::MouseEventArgs& arguments) override;
    void on_key_down(gui_forms::KeyEventArgs& arguments) override;
    void on_key_up(gui_forms::KeyEventArgs& arguments) override;
    void on_context_menu_requested(gui_forms::ContextMenuRequest& request) override;
    void on_detaching_from_window(gui_forms::SurfaceRoot& former_window) noexcept override;

private:
    // Drag targets (DragTrackerCallbacks), switching on gesture_.
    void plan_drag(gui_forms::DragPlan& plan);
    void preview_drag(const gui_forms::DragTrackerFrame& frame);
    void finish_drag(const gui_forms::DragTrackerFrame& frame);
    void announce_drag(gui_forms::DragAnnouncementRequest& request);
    void scroll_drag(gui_forms::DragAutoScrollStep& step);

    void start_gesture(Gesture gesture, const gui_forms::MouseEventArgs& arguments, gui_forms::Rect window_source,
                       double threshold);
    void plan_move(gui_forms::DragPlan& plan);
    void plan_link(gui_forms::DragPlan& plan);
    void collect_link_candidates(gui_forms::Point press_view);
    void finish_move(const gui_forms::DragTrackerFrame& frame);
    void finish_link(const gui_forms::DragTrackerFrame& frame);
    // The placement terms every free gesture shares: no grid, bounds far
    // past the window, free drop.
    void free_placement(gui_forms::DragPlan& plan) const;
    // Ends a gesture whose session ended without a plan (a click, or Escape
    // while armed): `finished` is not called for those.
    void settle_unplanned_gesture();

    void ensure_index();
    void after_edit();
    void refresh_selection_flags();
    [[nodiscard]] gui_forms::Rect window_viewport() const;
    [[nodiscard]] gui_forms::NodeId id_of_slot(std::uint32_t slot) const noexcept;
    // The drawn path of a link in view units into link_points_ (soft curve
    // or routed path); returns the point count (0 for a stale link).
    std::size_t link_path(std::uint32_t slot, const gui_forms::LinkRecord& link, std::size_t segments);
    [[nodiscard]] std::size_t curve_segments() const noexcept;

    // Painting (node_canvas_paint.cpp).
    void paint_background(gui_forms::Painter& painter, gui_forms::Rect viewport);
    void paint_links(gui_forms::Painter& painter, gui_forms::Rect viewport);
    void paint_nodes(gui_forms::Painter& painter);
    void paint_node(gui_forms::Painter& painter, std::uint32_t slot, const gui_forms::NodeRecord& node);
    void paint_title(gui_forms::Painter& painter, std::uint32_t slot, const gui_forms::NodeRecord& node,
                     gui_forms::Rect caption, double font_size);
    void paint_body(gui_forms::Painter& painter, std::uint32_t slot, const gui_forms::NodeRecord& node,
                    gui_forms::Rect body);
    void paint_ports(gui_forms::Painter& painter);
    void paint_overlay(gui_forms::Painter& painter);
    void enlist_live_sinks();

    GraphDocument& document_;
    Evaluator& evaluator_;
    ViewTransform view_{};
    LodSelector lod_{};
    SpatialIndex index_{};
    gui_forms::LinkRouter router_{};
    gui_forms::LinkRouterOptions router_options_{};
    LinkStyle link_style_{LinkStyle::soft};
    BackgroundVariant background_{BackgroundVariant::blue};
    bool snap_{true};
    double time_{};
    std::optional<gui_forms::FrameTime> clock_start_{};
    bool painted_since_frame_{};

    // Selection: the identities, and one flag per node slot for painting.
    std::vector<gui_forms::NodeId> selection_{};
    std::vector<std::uint8_t> selected_flags_{};
    std::optional<gui_forms::LinkId> selected_link_{};

    // The gesture in progress and its terms.
    Gesture gesture_{Gesture::none};
    gui_forms::NodeId move_anchor_{};
    gui_forms::Point press_view_{};
    gui_forms::Point current_view_{};
    gui_forms::Point pan_last_{};
    bool gesture_planned_{};
    bool gesture_group_open_{};
    gui_forms::PortRef link_from_{};
    std::optional<std::uint32_t> link_hit_{};
    std::vector<LinkCandidate> candidates_{};
    std::vector<gui_forms::DragTarget> targets_{};
    std::optional<gui_forms::NodeId> pending_click_select_{};
    bool knife_finished_{};
    bool space_held_{};
    std::string announcement_{};

    // Per-frame work storage, reserved once and reused.
    std::vector<std::uint32_t> visible_{};
    std::vector<std::uint32_t> query_{};
    std::vector<gui_forms::Point> link_points_{};
    std::vector<gui_forms::LinkId> crossing_{};
    std::array<std::vector<gui_forms::Point>, socket_type_count> port_centres_{};
    std::array<std::vector<gui_forms::Point>, socket_type_count> dimmed_centres_{};
    std::array<std::vector<gui_forms::Point>, 3> lamp_centres_{};
    std::vector<gui_forms::Point> preview_points_{};
    std::vector<TitleLayout> titles_{};
    gui_forms::ParagraphBuffers paragraph_{};
    double line_box_font_size_{};
    gui_forms::LineBoxMetrics line_box_{};
    std::vector<gui_forms::Rect> live_sink_rects_{};
    std::string text_{};

    CanvasStatistics statistics_{};
    gui_forms::Event<const EditRequest&> edit_requested_{};
    gui_forms::Event<const PaletteRequest&> palette_requested_{};
    gui_forms::Event<> changed_{};
    gui_forms::Event<> save_requested_{};
    // Declared last: it borrows this control and ends first.
    std::unique_ptr<gui_forms::DragTracker> drag_{};
};

} // namespace node_graph
