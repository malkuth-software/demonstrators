#include "node_canvas_view.hpp"

#include <gui_forms/surface_root/surface_root.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <span>

namespace node_graph {

namespace gf = gui_forms;

namespace {

// The square a magnet target occupies around its port (view units).
constexpr double target_side = 12.0;
// The world-aligned origin of the move gesture's snap bounds: a multiple of
// the grid spacing far enough out that no graph reaches it.
constexpr double snap_bounds_cells = 65536.0;
constexpr double caption_height = 24.0;

bool command_held(gf::Modifier modifiers) noexcept {
    const bool held = gf::has_modifier(modifiers, gf::Modifier::control) || gf::has_modifier(modifiers, gf::Modifier::meta);
    return held;
}

struct CandidateNearer final {
    bool operator()(const LinkCandidate& left, const LinkCandidate& right) const noexcept {
        return left.distance < right.distance;
    }
};

struct NearestInput final {
    gf::PortRef port{};
    double distance{};
};

struct NearestInputFirst final {
    bool operator()(const NearestInput& left, const NearestInput& right) const noexcept {
        return left.distance < right.distance;
    }
};

double distance_between(gf::Point left, gf::Point right) noexcept {
    const double distance = std::hypot(left.x - right.x, left.y - right.y);
    return distance;
}

} // namespace

NodeCanvasView::NodeCanvasView(gf::StableId stable_id, GraphDocument& document, Evaluator& evaluator)
    : gf::Control(std::move(stable_id)), document_(document), evaluator_(evaluator) {
    focusable = true;
    visible_.reserve(4096U);
    query_.reserve(4096U);
    link_points_.resize(maximum_link_points);
    crossing_.reserve(256U);
    for (std::vector<gf::Point>& centres : port_centres_) centres.reserve(4096U);
    for (std::vector<gf::Point>& centres : dimmed_centres_) centres.reserve(4096U);
    for (std::vector<gf::Point>& centres : lamp_centres_) centres.reserve(4096U);
    preview_points_.reserve(plot_history_samples + 2U);
    candidates_.reserve(gf::maximum_drag_targets);
    targets_.reserve(gf::maximum_drag_targets);
    announcement_.reserve(96U);
    text_.reserve(96U);
    live_sink_rects_.reserve(64U);
    paragraph_.reserve(256U, 4U);
    drag_ = std::make_unique<gf::DragTracker>(*this);
    gf::DragTrackerCallbacks callbacks{};
    callbacks.plan = gf::Delegate<gf::DragPlan&>::bind<NodeCanvasView, &NodeCanvasView::plan_drag>(*this);
    callbacks.preview = gf::Delegate<const gf::DragTrackerFrame&>::bind<NodeCanvasView, &NodeCanvasView::preview_drag>(*this);
    callbacks.finished = gf::Delegate<const gf::DragTrackerFrame&>::bind<NodeCanvasView, &NodeCanvasView::finish_drag>(*this);
    callbacks.announce =
        gf::Delegate<gf::DragAnnouncementRequest&>::bind<NodeCanvasView, &NodeCanvasView::announce_drag>(*this);
    callbacks.scroll = gf::Delegate<gf::DragAutoScrollStep&>::bind<NodeCanvasView, &NodeCanvasView::scroll_drag>(*this);
    (*drag_).callbacks = callbacks;
}

NodeCanvasView::~NodeCanvasView() {
    // A gesture's undo group must not stay open past the canvas.
    if (gesture_group_open_) document_.cancel_gesture();
    drag_.reset();
}

gf::Size NodeCanvasView::measure(gf::Size available) {
    return available;
}

gf::DragTrackerStatistics NodeCanvasView::drag_statistics() const {
    if (drag_ == nullptr) return gf::DragTrackerStatistics{};
    return (*drag_).statistics();
}

// -- view ---------------------------------------------------------------------------------

void NodeCanvasView::set_view(const ViewTransform& view) {
    view_ = view;
    view_.zoom_about(gf::Point{}, 1.0);
    invalidate(gf::Dirty::paint);
}

void NodeCanvasView::zoom_about(gf::Point local, double factor) {
    view_.zoom_about(local, factor);
    invalidate(gf::Dirty::paint);
    changed_.emit();
}

void NodeCanvasView::pan_by(gf::Point delta) {
    view_.pan_by(delta);
    invalidate(gf::Dirty::paint);
}

void NodeCanvasView::frame_all() {
    const gf::NodeGraph& graph = document_.graph();
    bool any = false;
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    for (const gf::NodeRecord& node : graph.node_slots()) {
        if (!node.live) continue;
        if (!any) {
            left = node.bounds.x;
            top = node.bounds.y;
            right = node.bounds.right();
            bottom = node.bounds.bottom();
            any = true;
            continue;
        }
        left = std::min(left, node.bounds.x);
        top = std::min(top, node.bounds.y);
        right = std::max(right, node.bounds.right());
        bottom = std::max(bottom, node.bounds.bottom());
    }
    if (!any) return;
    const gf::Rect client = client_rectangle();
    view_.frame(gf::WorldRect{left, top, right - left, bottom - top}, gf::Size{client.width, client.height}, 40.0);
    invalidate(gf::Dirty::paint);
    changed_.emit();
}

void NodeCanvasView::frame_selection() {
    const gf::NodeGraph& graph = document_.graph();
    bool any = false;
    gf::WorldRect box{};
    for (const gf::NodeId id : selection_) {
        const gf::NodeRecord* node = graph.node(id);
        if (node == nullptr) continue;
        if (!any) {
            box = (*node).bounds;
            any = true;
            continue;
        }
        const double right = std::max(box.right(), (*node).bounds.right());
        const double bottom = std::max(box.bottom(), (*node).bounds.bottom());
        box.x = std::min(box.x, (*node).bounds.x);
        box.y = std::min(box.y, (*node).bounds.y);
        box.width = right - box.x;
        box.height = bottom - box.y;
    }
    if (!any) return;
    const gf::Rect client = client_rectangle();
    view_.frame(box, gf::Size{client.width, client.height}, 80.0);
    invalidate(gf::Dirty::paint);
    changed_.emit();
}

void NodeCanvasView::set_link_style(LinkStyle style) {
    link_style_ = style;
    invalidate(gf::Dirty::paint);
}

void NodeCanvasView::set_background(BackgroundVariant variant) {
    background_ = variant;
    invalidate(gf::Dirty::paint);
}

void NodeCanvasView::set_time(double seconds) {
    time_ = seconds;
    if (evaluator_.evaluate(time_)) invalidate(gf::Dirty::paint);
}

// -- selection ------------------------------------------------------------------------------

gf::NodeId NodeCanvasView::id_of_slot(std::uint32_t slot) const noexcept {
    const std::span<const gf::NodeRecord> slots = document_.graph().node_slots();
    if (slot >= slots.size()) return gf::NodeId{slot, 0U};
    const gf::NodeId id{slot, slots[slot].generation};
    return id;
}

bool NodeCanvasView::selected(gf::NodeId node) const noexcept {
    if (node.index >= selected_flags_.size() || selected_flags_[node.index] == 0U) return false;
    const gf::NodeId current = id_of_slot(node.index);
    return current == node;
}

void NodeCanvasView::refresh_selection_flags() {
    const gf::NodeGraph& graph = document_.graph();
    const std::size_t slots = graph.node_slots().size();
    selected_flags_.assign(slots, 0U);
    std::size_t kept = 0U;
    // Drop stale identities (an undo can end a selected node's record).
    for (std::size_t index = 0U; index < selection_.size(); ++index) {
        const gf::NodeId id = selection_[index];
        if (graph.node(id) == nullptr || selected_flags_[id.index] != 0U) continue;
        selected_flags_[id.index] = 1U;
        selection_[kept] = id;
        ++kept;
    }
    selection_.resize(kept);
    if (selected_link_.has_value() && graph.link(*selected_link_) == nullptr) selected_link_.reset();
}

void NodeCanvasView::select_only(gf::NodeId node) {
    selection_.clear();
    selected_link_.reset();
    if (document_.graph().node(node) != nullptr) selection_.push_back(node);
    refresh_selection_flags();
    invalidate(gf::Dirty::paint);
}

void NodeCanvasView::add_to_selection(gf::NodeId node) {
    if (document_.graph().node(node) == nullptr || selected(node)) return;
    selection_.push_back(node);
    refresh_selection_flags();
    invalidate(gf::Dirty::paint);
}

void NodeCanvasView::toggle_selection(gf::NodeId node) {
    if (!selected(node)) {
        add_to_selection(node);
        return;
    }
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < selection_.size(); ++index) {
        if (selection_[index] == node) continue;
        selection_[kept] = selection_[index];
        ++kept;
    }
    selection_.resize(kept);
    refresh_selection_flags();
    invalidate(gf::Dirty::paint);
}

void NodeCanvasView::clear_selection() {
    if (selection_.empty() && !selected_link_.has_value()) return;
    selection_.clear();
    selected_link_.reset();
    refresh_selection_flags();
    invalidate(gf::Dirty::paint);
}

void NodeCanvasView::select_all() {
    selection_.clear();
    selected_link_.reset();
    const std::span<const gf::NodeRecord> slots = document_.graph().node_slots();
    for (std::size_t slot = 0U; slot < slots.size(); ++slot) {
        if (slots[slot].live) selection_.push_back(gf::NodeId{static_cast<std::uint32_t>(slot), slots[slot].generation});
    }
    refresh_selection_flags();
    invalidate(gf::Dirty::paint);
}

std::size_t NodeCanvasView::select_in_rect(const gf::WorldRect& world, bool touching, bool add) {
    ensure_index();
    index_.query(document_.graph(), world, !touching, query_);
    if (!add) selection_.clear();
    selected_link_.reset();
    refresh_selection_flags();
    std::size_t added = 0U;
    for (const std::uint32_t slot : query_) {
        if (selected_flags_[slot] != 0U) continue;
        selection_.push_back(id_of_slot(slot));
        selected_flags_[slot] = 1U;
        ++added;
    }
    invalidate(gf::Dirty::paint);
    return added;
}

// -- operations -----------------------------------------------------------------------------

void NodeCanvasView::after_edit() {
    refresh_selection_flags();
    static_cast<void>(evaluator_.evaluate(time_));
    invalidate(gf::Dirty::paint | gf::Dirty::semantics);
    changed_.emit();
}

void NodeCanvasView::document_changed() {
    after_edit();
}

void NodeCanvasView::delete_selection() {
    if (!selection_.empty()) {
        static_cast<void>(document_.remove_nodes(std::span<const gf::NodeId>(selection_.data(), selection_.size())));
        selection_.clear();
    } else if (selected_link_.has_value()) {
        static_cast<void>(document_.disconnect(*selected_link_));
        selected_link_.reset();
    } else {
        return;
    }
    after_edit();
}

void NodeCanvasView::undo() {
    if (gesture_group_open_) return;
    static_cast<void>(document_.undo());
    after_edit();
}

void NodeCanvasView::redo() {
    if (gesture_group_open_) return;
    static_cast<void>(document_.redo());
    after_edit();
}

std::size_t NodeCanvasView::cut_links(gf::Point from, gf::Point to) {
    links_crossing(from, to, true, crossing_);
    if (crossing_.empty()) return 0U;
    document_.begin_gesture();
    std::size_t cut = 0U;
    for (const gf::LinkId link : crossing_) {
        if (document_.disconnect(link).status == gf::GraphStatus::ok) ++cut;
    }
    static_cast<void>(document_.end_gesture());
    statistics_.knife_cuts += cut;
    after_edit();
    return cut;
}

bool NodeCanvasView::link_to_nearest(gf::PortRef from) {
    const gf::NodeGraph& graph = document_.graph();
    const gf::PortRecord* source = graph.port(from);
    const gf::NodeRecord* from_node = graph.node(from.node);
    if (source == nullptr || from_node == nullptr || (*source).direction != gf::PortDirection::output) return false;
    const gf::WorldPoint anchor = gf::port_anchor(*from_node, (*source).side, (*source).offset);
    std::vector<NearestInput> inputs{};
    const std::span<const gf::NodeRecord> slots = graph.node_slots();
    for (std::size_t slot = 0U; slot < slots.size(); ++slot) {
        const gf::NodeRecord& node = slots[slot];
        if (!node.live || slot == from.node.index) continue;
        for (std::size_t port = 0U; port < node.ports.size(); ++port) {
            const gf::PortRecord& target = node.ports[port];
            if (target.direction != gf::PortDirection::input) continue;
            if (!connectable((*source).socket_type, target.socket_type)) continue;
            const gf::PortRef ref{gf::NodeId{static_cast<std::uint32_t>(slot), node.generation},
                                  gf::PortIndex{static_cast<std::uint16_t>(port)}};
            if (graph.link(graph.link_into(ref)) != nullptr) continue;
            const gf::WorldPoint at = gf::port_anchor(node, target.side, target.offset);
            inputs.push_back(NearestInput{ref, std::hypot(at.x - anchor.x, at.y - anchor.y)});
        }
    }
    std::sort(inputs.begin(), inputs.end(), NearestInputFirst{});
    // The nearest few only: each try may walk the graph for a cycle.
    const std::size_t tries = std::min<std::size_t>(inputs.size(), 16U);
    for (std::size_t index = 0U; index < tries; ++index) {
        const ConnectResult result = document_.connect(from, inputs[index].port);
        if (result.status == ConnectStatus::connected) {
            ++statistics_.connections_made;
            after_edit();
            return true;
        }
    }
    ++statistics_.connections_refused;
    return false;
}

// -- geometry helpers -----------------------------------------------------------------------

void NodeCanvasView::ensure_index() {
    const gf::NodeGraph& graph = document_.graph();
    if (index_.built_revision() == graph.revision() && index_.statistics().builds > 0U) return;
    index_.build(graph);
    ++statistics_.index_rebuilds;
}

gf::Rect NodeCanvasView::window_viewport() const {
    const gf::Rect window = rectangle_to_window(client_rectangle());
    return window;
}

std::optional<gf::Point> NodeCanvasView::port_view_anchor(gf::PortRef port) const {
    const gf::NodeGraph& graph = document_.graph();
    const gf::NodeRecord* node = graph.node(port.node);
    const gf::PortRecord* record = graph.port(port);
    if (node == nullptr || record == nullptr) return std::nullopt;
    const gf::WorldPoint anchor = gf::port_anchor(*node, (*record).side, (*record).offset);
    const gf::Point view = view_.to_view(anchor);
    return view;
}

std::size_t NodeCanvasView::curve_segments() const noexcept {
    switch (lod_.level()) {
    case LevelOfDetail::overview: return 1U;
    case LevelOfDetail::silhouette: return 8U;
    case LevelOfDetail::compact: return 16U;
    case LevelOfDetail::full: return 24U;
    }
    return 24U;
}

std::size_t NodeCanvasView::link_path(std::uint32_t slot, const gf::LinkRecord& link, std::size_t segments) {
    const gf::LinkId id{slot, link.generation};
    const std::span<gf::Point> storage(link_points_.data(), link_points_.size());
    if (link_style_ == LinkStyle::routed) {
        const gf::RoutedLink* route = router_.find(id);
        if (route != nullptr) {
            const std::span<const gf::WorldPoint> drawn = router_.drawn(*route);
            const std::size_t count = std::min(drawn.size(), storage.size());
            for (std::size_t index = 0U; index < count; ++index) storage[index] = view_.to_view(drawn[index]);
            return count;
        }
    }
    const std::optional<gf::Point> from = port_view_anchor(link.from);
    const std::optional<gf::Point> to = port_view_anchor(link.to);
    if (!from.has_value() || !to.has_value()) return 0U;
    const CubicCurve curve = soft_link_curve(*from, *to, view_.zoom);
    const std::size_t count = flatten_curve(curve, segments, storage);
    return count;
}

// -- hit testing ----------------------------------------------------------------------------

CanvasHit NodeCanvasView::hit_test(gf::Point local) {
    ++statistics_.hit_tests;
    CanvasHit hit{};
    ensure_index();
    const gf::NodeGraph& graph = document_.graph();
    const gf::WorldPoint world = view_.to_world(local);
    const double reach = port_hit_radius / view_.zoom;
    const gf::WorldRect around{world.x - reach, world.y - reach, 2.0 * reach, 2.0 * reach};
    index_.query(graph, around, false, query_);
    const std::span<const gf::NodeRecord> slots = graph.node_slots();
    // Ports first: the nearest within the radius.
    double nearest = port_hit_radius;
    for (const std::uint32_t slot : query_) {
        const gf::NodeRecord& node = slots[slot];
        for (std::size_t port = 0U; port < node.ports.size(); ++port) {
            const gf::PortRecord& record = node.ports[port];
            const gf::Point anchor = view_.to_view(gf::port_anchor(node, record.side, record.offset));
            const double distance = distance_between(anchor, local);
            if (distance > nearest) continue;
            nearest = distance;
            hit.kind = CanvasHitKind::port;
            hit.node = gf::NodeId{slot, node.generation};
            hit.port = gf::PortIndex{static_cast<std::uint16_t>(port)};
        }
    }
    if (hit.kind == CanvasHitKind::port) return hit;
    // Then bodies: the topmost (latest slot, painted last) containing it.
    std::optional<std::uint32_t> top{};
    for (const std::uint32_t slot : query_) {
        if (!slots[slot].bounds.contains(world)) continue;
        if (!top.has_value() || slot > *top) top = slot;
    }
    if (top.has_value()) {
        const gf::NodeRecord& node = slots[*top];
        hit.node = gf::NodeId{*top, node.generation};
        const double caption_bottom = view_.to_view(gf::WorldPoint{node.bounds.x, node.bounds.y}).y + caption_height * view_.zoom;
        hit.kind = local.y <= caption_bottom ? CanvasHitKind::node_caption : CanvasHitKind::node_body;
        return hit;
    }
    // Then links within link_hit_distance. Every link is visited (no
    // spatial index for links; GAPS.md G8), culled by its curve's box.
    const std::span<const gf::LinkRecord> links = graph.link_slots();
    for (std::size_t slot = 0U; slot < links.size(); ++slot) {
        const gf::LinkRecord& link = links[slot];
        if (!link.live) continue;
        ++statistics_.hit_test_link_checks;
        if (link_style_ == LinkStyle::soft) {
            const std::optional<gf::Point> from = port_view_anchor(link.from);
            const std::optional<gf::Point> to = port_view_anchor(link.to);
            if (!from.has_value() || !to.has_value()) continue;
            const gf::Rect box = curve_bounds(soft_link_curve(*from, *to, view_.zoom));
            const bool near_box = local.x >= box.x - link_hit_distance && local.x <= box.x + box.width + link_hit_distance &&
                                  local.y >= box.y - link_hit_distance && local.y <= box.y + box.height + link_hit_distance;
            if (!near_box) continue;
        }
        const std::size_t count = link_path(static_cast<std::uint32_t>(slot), link, 24U);
        const std::span<const gf::Point> path(link_points_.data(), count);
        if (distance_to_polyline(path, local) > link_hit_distance) continue;
        hit.kind = CanvasHitKind::link;
        hit.link = gf::LinkId{static_cast<std::uint32_t>(slot), link.generation};
        return hit;
    }
    return hit;
}

void NodeCanvasView::links_crossing(gf::Point from, gf::Point to, bool cull, std::vector<gf::LinkId>& out) {
    out.clear();
    const gf::NodeGraph& graph = document_.graph();
    const std::span<const gf::LinkRecord> links = graph.link_slots();
    const double left = std::min(from.x, to.x);
    const double right = std::max(from.x, to.x);
    const double top = std::min(from.y, to.y);
    const double bottom = std::max(from.y, to.y);
    for (std::size_t slot = 0U; slot < links.size(); ++slot) {
        const gf::LinkRecord& link = links[slot];
        if (!link.live) continue;
        if (cull && link_style_ == LinkStyle::soft) {
            const std::optional<gf::Point> a = port_view_anchor(link.from);
            const std::optional<gf::Point> b = port_view_anchor(link.to);
            if (!a.has_value() || !b.has_value()) continue;
            const gf::Rect box = curve_bounds(soft_link_curve(*a, *b, view_.zoom));
            const bool apart = box.x > right || box.x + box.width < left || box.y > bottom || box.y + box.height < top;
            if (apart) continue;
        }
        const std::size_t count = link_path(static_cast<std::uint32_t>(slot), link, 24U);
        const std::span<const gf::Point> path(link_points_.data(), count);
        if (polyline_crosses(path, from, to)) out.push_back(gf::LinkId{static_cast<std::uint32_t>(slot), link.generation});
    }
}

// -- input ------------------------------------------------------------------------------------

void NodeCanvasView::settle_unplanned_gesture() {
    if (gesture_ == Gesture::none || (*drag_).running()) return;
    if (gesture_group_open_) {
        document_.cancel_gesture();
        gesture_group_open_ = false;
        after_edit();
    }
    gesture_ = Gesture::none;
}

void NodeCanvasView::start_gesture(Gesture gesture, const gf::MouseEventArgs& arguments, gf::Rect window_source,
                                   double threshold) {
    gesture_ = gesture;
    gesture_planned_ = false;
    press_view_ = arguments.location;
    current_view_ = arguments.location;
    pan_last_ = gf::Point{window_source.x, window_source.y};
    link_hit_.reset();
    gf::DragStart start{};
    start.key = static_cast<std::uint64_t>(gesture);
    start.source = window_source;
    start.threshold = threshold;
    const gf::DragStartResult result = (*drag_).press(gf::pointer_event_of(arguments), start);
    if (result == gf::DragStartResult::armed || result == gf::DragStartResult::started) {
        ++statistics_.gestures_started;
        return;
    }
    // Refused or not started: `finished` ran for a refused plan; otherwise
    // nothing was started.
    if (gesture_ != Gesture::none) settle_unplanned_gesture();
    gesture_ = Gesture::none;
}

void NodeCanvasView::on_mouse_down(gf::MouseEventArgs& arguments) {
    settle_unplanned_gesture();
    const bool second_press = arguments.clicks > 1U;
    if (!second_press && !(*drag_).running()) {
        gf::SurfaceRoot* root = attached_surface_root();
        if (root != nullptr) static_cast<void>((*root).request_focus(shared_from_this()));
        pending_click_select_.reset();
        knife_finished_ = false;
        const gf::Point local = arguments.location;
        const gf::Point window = arguments.window_position;
        const gf::Rect pointer_source{window.x, window.y, 1.0, 1.0};
        const bool shift = gf::has_modifier(arguments.modifiers, gf::Modifier::shift);
        const bool command = command_held(arguments.modifiers);
        if (arguments.button == gf::PointerButton::middle ||
            (arguments.button == gf::PointerButton::primary && space_held_)) {
            start_gesture(Gesture::pan, arguments, pointer_source, 0.0);
        } else if (arguments.button == gf::PointerButton::secondary && command) {
            start_gesture(Gesture::knife, arguments, pointer_source, 2.0);
        } else if (arguments.button == gf::PointerButton::primary) {
            const CanvasHit hit = hit_test(local);
            const gf::NodeGraph& graph = document_.graph();
            if (hit.kind == CanvasHitKind::port) {
                const gf::PortRef port{hit.node, hit.port};
                const gf::PortRecord* record = graph.port(port);
                const std::optional<gf::Point> anchor = port_view_anchor(port);
                if (record != nullptr && anchor.has_value()) {
                    const gf::Point anchor_window = point_to_window(*anchor);
                    const gf::Rect source{anchor_window.x - 1.0, anchor_window.y - 1.0, 2.0, 2.0};
                    if ((*record).direction == gf::PortDirection::output) {
                        link_from_ = port;
                        start_gesture(Gesture::link, arguments, source, gf::default_drag_threshold);
                    } else {
                        const gf::LinkId into = graph.link_into(port);
                        const gf::LinkRecord* link = graph.link(into);
                        if (link != nullptr) {
                            // Detach: the link's output becomes the drag's
                            // origin; detach and reconnect are one step.
                            link_from_ = (*link).from;
                            document_.begin_gesture();
                            gesture_group_open_ = true;
                            static_cast<void>(document_.disconnect(into));
                            ++statistics_.links_detached;
                            // The press is at the input: the dangling end
                            // starts there, so the drag's subject is the
                            // input's square and its threshold zero.
                            start_gesture(Gesture::link, arguments, source, 0.0);
                            after_edit();
                        }
                    }
                }
            } else if (hit.kind == CanvasHitKind::node_body || hit.kind == CanvasHitKind::node_caption) {
                if (shift) {
                    add_to_selection(hit.node);
                } else if (command) {
                    toggle_selection(hit.node);
                } else if (!selected(hit.node)) {
                    select_only(hit.node);
                } else {
                    // A press on a selected node keeps the selection for a
                    // drag; the click (after the release) narrows it.
                    pending_click_select_ = hit.node;
                }
                if (selected(hit.node)) {
                    move_anchor_ = hit.node;
                    const gf::NodeRecord* node = graph.node(hit.node);
                    if (node != nullptr) {
                        const gf::Rect source = rectangle_to_window(view_.rect_to_view((*node).bounds));
                        start_gesture(Gesture::move, arguments, source, gf::default_drag_threshold);
                    }
                }
            } else if (hit.kind == CanvasHitKind::link) {
                selection_.clear();
                selected_link_ = hit.link;
                refresh_selection_flags();
                invalidate(gf::Dirty::paint);
            } else {
                if (!shift && !command) clear_selection();
                start_gesture(Gesture::marquee, arguments, pointer_source, gf::default_drag_threshold);
            }
        }
        if (gesture_ != Gesture::none) arguments.handled = true;
    }
    gf::Control::on_mouse_down(arguments);
}

void NodeCanvasView::on_mouse_move(gf::MouseEventArgs& arguments) {
    if ((*drag_).running()) {
        const bool taken = (*drag_).pointer(gf::pointer_event_of(arguments));
        if (taken) arguments.handled = true;
    }
    gf::Control::on_mouse_move(arguments);
}

void NodeCanvasView::on_mouse_up(gf::MouseEventArgs& arguments) {
    if ((*drag_).running()) {
        const bool planned = gesture_planned_;
        const bool taken = (*drag_).pointer(gf::pointer_event_of(arguments));
        if (taken) arguments.handled = true;
        if (planned) {
            // Input order rule 3: the release of a drag is no click, so the
            // click handler does not narrow the selection after a move.
            arguments.suppress_click = true;
            pending_click_select_.reset();
            ++statistics_.clicks_suppressed;
        }
        // A release while armed is the press's click: the session ended
        // without a plan, and `finished` was not called.
        if (!planned) settle_unplanned_gesture();
    }
    gf::Control::on_mouse_up(arguments);
}

void NodeCanvasView::on_mouse_click(gf::MouseEventArgs& arguments) {
    if (arguments.button == gf::PointerButton::primary && pending_click_select_.has_value()) {
        select_only(*pending_click_select_);
        pending_click_select_.reset();
    }
    gf::Control::on_mouse_click(arguments);
}

void NodeCanvasView::on_mouse_double_click(gf::MouseEventArgs& arguments) {
    if (arguments.button == gf::PointerButton::primary) {
        const CanvasHit hit = hit_test(arguments.location);
        if (hit.kind == CanvasHitKind::node_body || hit.kind == CanvasHitKind::node_caption) {
            select_only(hit.node);
            const EditRequest request{hit.node, hit.kind == CanvasHitKind::node_caption};
            ++statistics_.edit_requests;
            edit_requested_.emit(request);
        } else if (hit.kind == CanvasHitKind::none) {
            PaletteRequest request{};
            request.world = view_.to_world(arguments.location);
            request.window_position = arguments.window_position;
            ++statistics_.palette_requests;
            palette_requested_.emit(request);
        }
        arguments.handled = true;
    }
    gf::Control::on_mouse_double_click(arguments);
}

void NodeCanvasView::on_mouse_wheel(gf::MouseEventArgs& arguments) {
    double steps = arguments.wheel_delta.y;
    if (arguments.wheel_unit == gf::WheelDeltaUnit::pixels) steps /= 40.0;
    const double base = command_held(arguments.modifiers) ? 1.02 : 1.15;
    const double factor = std::pow(base, steps);
    zoom_about(arguments.location, factor);
    arguments.handled = true;
    gf::Control::on_mouse_wheel(arguments);
}

void NodeCanvasView::on_key_down(gf::KeyEventArgs& arguments) {
    gf::Control::on_key_down(arguments);
    if (arguments.handled) return;
    const std::uint32_t key = arguments.physical_key;
    const bool command = command_held(arguments.modifiers);
    const bool shift = gf::has_modifier(arguments.modifiers, gf::Modifier::shift);
    bool consumed = true;
    if (key == gf::PhysicalKey::delete_forward || key == gf::PhysicalKey::backspace) {
        delete_selection();
    } else if (key == gf::PhysicalKey::z && command) {
        if (shift) {
            redo();
        } else {
            undo();
        }
    } else if (key == gf::PhysicalKey::y && command) {
        redo();
    } else if (key == gf::PhysicalKey::a && command) {
        select_all();
    } else if (key == gf::PhysicalKey::s && command) {
        save_requested_.emit();
    } else if (key == gf::PhysicalKey::home) {
        frame_all();
    } else if (key == gf::PhysicalKey::f && !command) {
        frame_selection();
    } else if (key == gf::PhysicalKey::left || key == gf::PhysicalKey::right || key == gf::PhysicalKey::up ||
               key == gf::PhysicalKey::down) {
        double dx = 0.0;
        double dy = 0.0;
        if (key == gf::PhysicalKey::left) dx = -1.0;
        if (key == gf::PhysicalKey::right) dx = 1.0;
        if (key == gf::PhysicalKey::up) dy = -1.0;
        if (key == gf::PhysicalKey::down) dy = 1.0;
        if (command && !selection_.empty()) {
            // Ctrl+arrows move the selection one grid cell: one step each.
            const gf::WorldPoint delta{dx * grid_spacing, dy * grid_spacing};
            static_cast<void>(document_.move_nodes(std::span<const gf::NodeId>(selection_.data(), selection_.size()), delta));
            after_edit();
        } else {
            pan_by(gf::Point{-dx * 40.0, -dy * 40.0});
        }
    } else if (key == gf::PhysicalKey::enter || key == gf::PhysicalKey::f2) {
        if (!selection_.empty()) {
            const EditRequest request{selection_.front(), key == gf::PhysicalKey::f2};
            ++statistics_.edit_requests;
            edit_requested_.emit(request);
        } else {
            consumed = false;
        }
    } else if (key == gf::PhysicalKey::l && !command) {
        bool linked = false;
        if (!selection_.empty()) {
            const gf::NodeRecord* node = document_.graph().node(selection_.front());
            if (node != nullptr) {
                for (std::size_t port = 0U; port < (*node).ports.size() && !linked; ++port) {
                    if ((*node).ports[port].direction != gf::PortDirection::output) continue;
                    linked = link_to_nearest(gf::PortRef{selection_.front(), gf::PortIndex{static_cast<std::uint16_t>(port)}});
                }
            }
        }
        static_cast<void>(linked);
    } else if (key == gf::PhysicalKey::space) {
        space_held_ = true;
    } else if (key == gf::PhysicalKey::escape) {
        if (selection_.empty() && !selected_link_.has_value()) {
            consumed = false;
        } else {
            clear_selection();
        }
    } else {
        consumed = false;
    }
    if (consumed) {
        arguments.handled = true;
        // The canvas's letter keys (L, F, Space) produce no text input.
        arguments.suppress_key_press = true;
        ++statistics_.key_presses_suppressed;
    }
}

void NodeCanvasView::on_key_up(gf::KeyEventArgs& arguments) {
    gf::Control::on_key_up(arguments);
    if (arguments.physical_key == gf::PhysicalKey::space) space_held_ = false;
}

void NodeCanvasView::on_context_menu_requested(gf::ContextMenuRequest& request) {
    // The knife's release (Ctrl + secondary) is not a menu request.
    if (knife_finished_) {
        knife_finished_ = false;
        request.handled = true;
    }
    gf::Control::on_context_menu_requested(request);
}

void NodeCanvasView::on_detaching_from_window(gf::SurfaceRoot& former_window) noexcept {
    if (drag_ != nullptr) (*drag_).owner_detaching(former_window);
    gf::Control::on_detaching_from_window(former_window);
}

// -- drag targets -----------------------------------------------------------------------------

void NodeCanvasView::free_placement(gf::DragPlan& plan) const {
    const gf::Rect viewport = window_viewport();
    plan.kind = gf::DragKind::placement;
    plan.placement.bounds = gf::Rect{viewport.x - 1.0e6, viewport.y - 1.0e6, viewport.width + 2.0e6,
                                     viewport.height + 2.0e6};
    plan.placement.grid = gf::Size{};
    plan.placement.grid_by_default = false;
    plan.placement.free_drop = true;
}

void NodeCanvasView::plan_drag(gf::DragPlan& plan) {
    gesture_planned_ = true;
    plan.container = this;
    switch (gesture_) {
    case Gesture::move:
        plan_move(plan);
        plan.auto_scroll.viewport = window_viewport();
        return;
    case Gesture::link:
        plan_link(plan);
        plan.auto_scroll.viewport = window_viewport();
        return;
    case Gesture::marquee:
    case Gesture::knife:
        free_placement(plan);
        plan.auto_scroll.viewport = window_viewport();
        return;
    case Gesture::pan:
        free_placement(plan);
        return;
    case Gesture::none:
        break;
    }
    // No gesture (or one this canvas does not know): nothing to drag.
    plan.refused = true;
}

void NodeCanvasView::plan_move(gf::DragPlan& plan) {
    const double cell = grid_spacing * view_.zoom;
    const double far = -grid_spacing * snap_bounds_cells;
    const gf::Point corner = point_to_window(view_.to_view(gf::WorldPoint{far, far}));
    const double side = 2.0 * grid_spacing * snap_bounds_cells * view_.zoom;
    plan.kind = gf::DragKind::placement;
    plan.placement.bounds = gf::Rect{corner.x, corner.y, side, side};
    plan.placement.grid = gf::Size{cell, cell};
    plan.placement.grid_by_default = snap_;
    plan.placement.grid_toggle = gf::Modifier::control;
    plan.placement.free_drop = true;
    if (!gesture_group_open_) {
        document_.begin_gesture();
        gesture_group_open_ = true;
    }
}

void NodeCanvasView::collect_link_candidates(gf::Point press_view) {
    candidates_.clear();
    targets_.clear();
    const gf::NodeGraph& graph = document_.graph();
    const gf::PortRecord* source = graph.port(link_from_);
    if (source == nullptr) return;
    ensure_index();
    const gf::Rect client = client_rectangle();
    const gf::Rect area{client.x - candidate_margin, client.y - candidate_margin, client.width + 2.0 * candidate_margin,
                        client.height + 2.0 * candidate_margin};
    index_.query(graph, view_.rect_to_world(area), false, query_);
    const std::span<const gf::NodeRecord> slots = graph.node_slots();
    for (const std::uint32_t slot : query_) {
        if (slot == link_from_.node.index) continue;
        const gf::NodeRecord& node = slots[slot];
        for (std::size_t port = 0U; port < node.ports.size(); ++port) {
            const gf::PortRecord& record = node.ports[port];
            if (record.direction != gf::PortDirection::input) continue;
            if (!connectable((*source).socket_type, record.socket_type)) continue;
            const gf::PortRef ref{gf::NodeId{slot, node.generation}, gf::PortIndex{static_cast<std::uint16_t>(port)}};
            if (graph.link(graph.link_into(ref)) != nullptr) continue;
            const gf::Point anchor = view_.to_view(gf::port_anchor(node, record.side, record.offset));
            candidates_.push_back(LinkCandidate{ref, anchor, distance_between(anchor, press_view)});
        }
    }
    // Nearest first, capped at the tracker's target limit (more is refused,
    // never truncated, by the tracker; the provider caps).
    std::sort(candidates_.begin(), candidates_.end(), CandidateNearer{});
    if (candidates_.size() > gf::maximum_drag_targets) candidates_.resize(gf::maximum_drag_targets);
    for (std::size_t index = 0U; index < candidates_.size(); ++index) {
        const gf::Point window = point_to_window(candidates_[index].view_anchor);
        const gf::Rect bounds{window.x - target_side / 2.0, window.y - target_side / 2.0, target_side, target_side};
        targets_.push_back(gf::DragTarget{bounds, static_cast<std::uint32_t>(index), 0U});
    }
    statistics_.magnet_candidates += candidates_.size();
}

void NodeCanvasView::plan_link(gf::DragPlan& plan) {
    if (plan.epoch <= 1U) ++statistics_.link_drags;
    // The press moves with the content when an auto-scroll step plans
    // again, so the candidates are collected around the press each time.
    collect_link_candidates(point_from_window(plan.press));
    free_placement(plan);
    plan.placement.targets = std::span<const gf::DragTarget>(targets_.data(), targets_.size());
    plan.placement.snap_radius = magnet_capture_radius;
    plan.placement.release_radius = magnet_release_radius;
}

void NodeCanvasView::preview_drag(const gf::DragTrackerFrame& frame) {
    const gf::Point top_left = point_from_window(frame.point);
    switch (gesture_) {
    case Gesture::move: {
        const gf::NodeRecord* anchor = document_.graph().node(move_anchor_);
        if (anchor == nullptr) {
            (*drag_).cancel();
            return;
        }
        const gf::WorldPoint world = view_.to_world(top_left);
        const gf::WorldPoint delta{world.x - (*anchor).bounds.x, world.y - (*anchor).bounds.y};
        if (delta.x != 0.0 || delta.y != 0.0) {
            static_cast<void>(document_.move_nodes(std::span<const gf::NodeId>(selection_.data(), selection_.size()), delta));
            invalidate(gf::Dirty::paint);
        }
        return;
    }
    case Gesture::link: {
        const std::optional<std::uint32_t> before = link_hit_;
        link_hit_.reset();
        if (frame.hit == gf::DragHit::target && frame.target < candidates_.size()) link_hit_ = frame.target;
        if (link_hit_.has_value() && !before.has_value()) ++statistics_.magnet_captures;
        // The subject is the 2 x 2 square about the press: its centre is
        // the dangling end.
        current_view_ = gf::Point{top_left.x + 1.0, top_left.y + 1.0};
        invalidate(gf::Dirty::paint);
        return;
    }
    case Gesture::marquee:
    case Gesture::knife:
        current_view_ = top_left;
        invalidate(gf::Dirty::paint);
        return;
    case Gesture::pan: {
        const gf::Point delta{frame.point.x - pan_last_.x, frame.point.y - pan_last_.y};
        pan_last_ = frame.point;
        pan_by(delta);
        return;
    }
    case Gesture::none:
        break;
    }
    // A frame for no gesture: ignored (the session is someone else's).
}

void NodeCanvasView::finish_move(const gf::DragTrackerFrame& frame) {
    if (!gesture_group_open_) return;
    gesture_group_open_ = false;
    if (gf::drag_end_commits(frame.end)) {
        static_cast<void>(document_.end_gesture());
        ++statistics_.moves_committed;
    } else {
        document_.cancel_gesture();
        ++statistics_.gestures_cancelled;
    }
}

void NodeCanvasView::finish_link(const gf::DragTrackerFrame& frame) {
    const bool commits = gf::drag_end_commits(frame.end);
    if (commits && frame.end == gf::DragEnd::dropped && frame.hit == gf::DragHit::target &&
        frame.target < candidates_.size()) {
        const ConnectResult result = document_.connect(link_from_, candidates_[frame.target].port);
        if (result.status == ConnectStatus::connected) {
            ++statistics_.connections_made;
        } else {
            ++statistics_.connections_refused;
        }
    } else if (commits) {
        // Released off every port: the palette, filtered to kinds with a
        // compatible input, at the release point.
        PaletteRequest request{};
        request.world = view_.to_world(current_view_);
        request.window_position = point_to_window(current_view_);
        request.from = link_from_;
        ++statistics_.palette_requests;
        palette_requested_.emit(request);
    }
    if (gesture_group_open_) {
        gesture_group_open_ = false;
        if (commits) {
            static_cast<void>(document_.end_gesture());
        } else {
            document_.cancel_gesture();
            ++statistics_.gestures_cancelled;
        }
    } else if (!commits) {
        ++statistics_.gestures_cancelled;
    }
}

void NodeCanvasView::finish_drag(const gf::DragTrackerFrame& frame) {
    const Gesture ended = gesture_;
    gesture_ = Gesture::none;
    switch (ended) {
    case Gesture::move:
        finish_move(frame);
        break;
    case Gesture::link:
        finish_link(frame);
        break;
    case Gesture::marquee:
        if (gf::drag_end_commits(frame.end)) {
            const gf::Point corner = point_from_window(frame.point);
            const gf::Rect view_rect{std::min(press_view_.x, corner.x), std::min(press_view_.y, corner.y),
                                     std::fabs(corner.x - press_view_.x), std::fabs(corner.y - press_view_.y)};
            const bool touching = gf::has_modifier(frame.modifiers, gf::Modifier::alt);
            const bool add = gf::has_modifier(frame.modifiers, gf::Modifier::shift);
            static_cast<void>(select_in_rect(view_.rect_to_world(view_rect), touching, add));
            ++statistics_.marquee_selections;
        }
        break;
    case Gesture::knife:
        if (gf::drag_end_commits(frame.end)) {
            const gf::Point end = point_from_window(frame.point);
            static_cast<void>(cut_links(press_view_, end));
            knife_finished_ = true;
        }
        break;
    case Gesture::pan:
    case Gesture::none:
        break;
    }
    link_hit_.reset();
    after_edit();
}

void NodeCanvasView::announce_drag(gf::DragAnnouncementRequest& request) {
    announcement_.clear();
    char buffer[64]{};
    switch (gesture_) {
    case Gesture::move: {
        const int written = std::snprintf(buffer, sizeof(buffer), "Moving %zu nodes", selection_.size());
        if (written > 0) announcement_.append(buffer, static_cast<std::size_t>(written));
        break;
    }
    case Gesture::link:
        if (link_hit_.has_value() && *link_hit_ < candidates_.size()) {
            const gf::PortRecord* port = document_.graph().port(candidates_[*link_hit_].port);
            announcement_.append("Over input ");
            if (port != nullptr) announcement_.append((*port).name);
        } else {
            announcement_.append("Linking");
        }
        break;
    case Gesture::marquee: announcement_.append("Selecting"); break;
    case Gesture::knife: announcement_.append("Cutting links"); break;
    case Gesture::pan: break;
    case Gesture::none: break;
    }
    request.text = announcement_;
}

void NodeCanvasView::scroll_drag(gf::DragAutoScrollStep& step) {
    // An unbounded canvas: every requested step applies.
    pan_by(gf::Point{-step.requested.x, -step.requested.y});
    step.applied = step.requested;
}

// -- motion clock ------------------------------------------------------------------------------

bool NodeCanvasView::on_ambient_frame(gf::FrameTime now) {
    // Not painted since the last frame: the plots are no longer shown, so
    // the clock drops the canvas (the idle rule, spec 4.1).
    if (!painted_since_frame_ || live_sink_rects_.empty()) return false;
    painted_since_frame_ = false;
    if (!clock_start_.has_value()) clock_start_ = now;
    const std::chrono::duration<double> elapsed = now - *clock_start_;
    time_ = elapsed.count();
    static_cast<void>(evaluator_.evaluate(time_));
    // Damage only the live sinks' bounds.
    for (const gf::Rect& rect : live_sink_rects_) invalidate(rect);
    ++statistics_.ambient_frames;
    return true;
}

void NodeCanvasView::enlist_live_sinks() {
    painted_since_frame_ = true;
    if (live_sink_rects_.empty()) return;
    gf::SurfaceRoot* root = attached_surface_root();
    if (root == nullptr) return;
    // Enlisting from paint: the clock's record is made on the first frame
    // (unverified that enlisting during a paint is accepted; GAPS.md R3).
    const bool fresh = (*root).motion_clock().enlist_control_ambient(*this, std::chrono::milliseconds(33));
    if (fresh) ++statistics_.ambient_enlistments;
}

// -- semantics --------------------------------------------------------------------------------

gf::SemanticDescriptor NodeCanvasView::semantic_descriptor() const {
    gf::SemanticDescriptor descriptor = gf::Control::semantic_descriptor();
    descriptor.role = gf::SemanticRole::group;
    descriptor.name = "Node graph";
    char buffer[64]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%zu nodes, %zu links", document_.graph().node_count(),
                                      document_.graph().link_count());
    if (written > 0) descriptor.value.assign(buffer, static_cast<std::size_t>(written));
    return descriptor;
}

std::vector<gf::SemanticNode> NodeCanvasView::semantic_virtual_children() const {
    // The visible set only (spec 6): nodes as items named by caption and
    // kind, ports as their children. Links as relations (FlowsTo) cannot be
    // published: the semantics have no relations (GAPS.md G11).
    std::vector<gf::SemanticNode> children{};
    const gf::NodeGraph& graph = document_.graph();
    const std::span<const gf::NodeRecord> slots = graph.node_slots();
    children.reserve(visible_.size());
    for (const std::uint32_t slot : visible_) {
        if (slot >= slots.size() || !slots[slot].live) continue;
        const gf::NodeRecord& node = slots[slot];
        gf::SemanticNode item{};
        item.stable_id = "node.";
        item.stable_id.append(node.key);
        item.role = gf::SemanticRole::list_item;
        item.name = node.title;
        item.name.append(", ");
        item.name.append(node.kind);
        item.bounds = view_.rect_to_view(node.bounds);
        item.states = gf::SemanticState::visible;
        for (std::size_t port = 0U; port < node.ports.size(); ++port) {
            const gf::PortRecord& record = node.ports[port];
            gf::SemanticNode child{};
            child.stable_id = item.stable_id;
            child.stable_id.append(".port.");
            child.stable_id.append(record.name);
            child.role = gf::SemanticRole::static_text;
            child.name = record.direction == gf::PortDirection::input ? "input " : "output ";
            child.name.append(record.name);
            if (record.direction == gf::PortDirection::input) {
                const gf::PortRef ref{gf::NodeId{slot, node.generation}, gf::PortIndex{static_cast<std::uint16_t>(port)}};
                const gf::LinkRecord* link = graph.link(graph.link_into(ref));
                if (link != nullptr) {
                    const gf::NodeRecord* from = graph.node((*link).from.node);
                    if (from != nullptr) {
                        child.name.append(", connected to ");
                        child.name.append((*from).title);
                    }
                }
            }
            item.children.push_back(std::move(child));
        }
        children.push_back(std::move(item));
    }
    return children;
}

} // namespace node_graph
