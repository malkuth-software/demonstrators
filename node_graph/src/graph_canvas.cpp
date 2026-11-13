#include "graph_canvas.hpp"

#include <gui_forms/surface_root/surface_root.hpp>

#include <array>
#include <chrono>
#include <span>
#include <utility>

namespace node_graph {

namespace gf = gui_forms;

namespace {

struct BackgroundName final {
    BackgroundVariant variant{};
    std::string_view name{};
};

constexpr std::array<BackgroundName, 4> background_names{{
    {BackgroundVariant::paper, "paper"},
    {BackgroundVariant::blue, "blue"},
    {BackgroundVariant::green, "green"},
    {BackgroundVariant::grey, "grey"},
}};

bool command_held(gf::Modifier modifiers) noexcept {
    const bool held = gf::has_modifier(modifiers, gf::Modifier::control) || gf::has_modifier(modifiers, gf::Modifier::meta);
    return held;
}

} // namespace

std::string_view background_class(BackgroundVariant variant) noexcept {
    for (const BackgroundName& entry : background_names) {
        if (entry.variant == variant) return entry.name;
    }
    return "blue";
}

GraphCanvas::GraphCanvas(gf::StableId stable_id, GraphDocument& document, Evaluator& evaluator)
    : gf::NodeCanvas(std::move(stable_id)), document_(document), evaluator_(evaluator) {
    focusable = true;
    live_sink_rects_.reserve(64U);
    preview_points_.reserve(plot_history_samples + 2U);
    text_.reserve(96U);
    number_.reserve(32U);
    static_cast<void>(add_class(background_class(background_)));
    gf::NodeCanvasCallbacks callbacks{};
    callbacks.check_link = gf::Delegate<gf::NodeLinkCheck&>::bind<GraphCanvas, &GraphCanvas::check_link>(*this);
    callbacks.edit = gf::Delegate<gf::NodeEditRequest&>::bind<GraphCanvas, &GraphCanvas::edit_node>(*this);
    callbacks.paint_body = gf::Delegate<gf::NodeBodyPaint&>::bind<GraphCanvas, &GraphCanvas::paint_body>(*this);
    set_callbacks(callbacks);
    set_graph(&document_.editable_graph());
}

GraphCanvas::~GraphCanvas() {
    // Rolls back a gesture or an edit still open in the document's graph
    // and drops the borrow before the document can go.
    set_graph(nullptr);
}

void GraphCanvas::set_edit_target(gf::Delegate<gf::NodeEditRequest&> target) {
    edit_target_ = target;
}

void GraphCanvas::set_background(BackgroundVariant variant) {
    if (variant == background_) return;
    static_cast<void>(remove_class(background_class(background_)));
    background_ = variant;
    static_cast<void>(add_class(background_class(background_)));
    ++counters_.background_changes;
    // The canvas resolves its look again when its classes change.
    invalidate(gf::Dirty::paint);
}

void GraphCanvas::set_time(double seconds) {
    time_ = seconds;
    if (evaluator_.evaluate(time_)) {
        ++counters_.evaluations;
        invalidate(gf::Dirty::paint);
    }
}

void GraphCanvas::document_changed() {
    evaluate_and_mark();
    refresh();
}

void GraphCanvas::evaluate_and_mark() {
    if (evaluator_.evaluate(time_)) ++counters_.evaluations;
    const gf::NodeGraph& graph = document_.graph();
    if (flow_revision_ == graph.revision()) return;
    flow_revision_ = graph.revision();
    const std::span<const gf::LinkRecord> links = graph.link_slots();
    std::size_t flowing = 0U;
    for (std::uint32_t slot = 0U; slot < links.size(); ++slot) {
        const gf::LinkRecord& link = links[slot];
        if (!link.live) continue;
        const NodeState* source = evaluator_.state(link.from.node.index);
        const bool moving = source != nullptr && (*source).time_dependent;
        const gf::LinkId identity{slot, link.generation};
        if (link_flowing(identity) != moving) {
            static_cast<void>(set_link_flowing(identity, moving));
            ++counters_.flow_marks;
        }
        if (moving) ++flowing;
    }
    counters_.flowing_links = flowing;
}

// -- callbacks ----------------------------------------------------------------------------

void GraphCanvas::check_link(gf::NodeLinkCheck& check) {
    ++counters_.link_checks;
    // The canvas's default (equal socket numbers) is replaced by the app's
    // conversion table and its cycle refusal.
    check.accepted = document_.check_connection(check.from, check.to) == ConnectStatus::connected;
    if (!check.accepted) ++counters_.link_checks_refused;
}

void GraphCanvas::edit_node(gf::NodeEditRequest& request) {
    if (edit_target_) edit_target_(request);
}

// -- raisers ------------------------------------------------------------------------------

void GraphCanvas::on_selection_changed(const gf::NodeCanvasSelection& arguments) {
    ++counters_.selection_raised;
    gf::NodeCanvas::on_selection_changed(arguments);
}

void GraphCanvas::on_link_connected(const gf::NodeLinkConnected& arguments) {
    ++counters_.link_connected_raised;
    gf::NodeCanvas::on_link_connected(arguments);
}

void GraphCanvas::on_graph_edited(const gf::NodeGraphEdited& arguments) {
    ++counters_.graph_edited_raised;
    // The canvas edited the graph directly: the document counts it (and
    // autosaves), then evaluation and the flow marks catch up.
    document_.canvas_edited(arguments.kind);
    evaluate_and_mark();
    gf::NodeCanvas::on_graph_edited(arguments);
}

void GraphCanvas::on_view_changed(const gf::SceneViewTransform& arguments) {
    ++counters_.view_raised;
    gf::NodeCanvas::on_view_changed(arguments);
}

void GraphCanvas::on_palette_requested(const gf::NodePaletteRequest& arguments) {
    ++counters_.palette_raised;
    gf::NodeCanvas::on_palette_requested(arguments);
}

void GraphCanvas::on_edit_ended(const gf::NodeEditEnded& arguments) {
    ++counters_.edit_ended_raised;
    gf::NodeCanvas::on_edit_ended(arguments);
}

void GraphCanvas::on_key_down(gf::KeyEventArgs& arguments) {
    gf::NodeCanvas::on_key_down(arguments);
    if (arguments.handled) return;
    if (arguments.physical_key == gf::PhysicalKey::s && command_held(arguments.modifiers)) {
        arguments.handled = true;
        arguments.suppress_key_press = true;
        ++counters_.save_requests;
        save_requested_.emit();
    }
}

// -- paint and the motion clock ------------------------------------------------------------

void GraphCanvas::on_paint(gf::Painter& painter, gf::Rect local_damage) {
    // Lamps first, so the canvas paints this evaluation's (a lamp write
    // moves lamp_revision only, so no cache is dropped).
    if (evaluator_.evaluate(time_)) ++counters_.evaluations;
    read_body_ink();
    gf::NodeCanvas::on_paint(painter, local_damage);
    collect_live_sinks();
    enlist_live_sinks();
}

void GraphCanvas::collect_live_sinks() {
    live_sink_rects_.clear();
    if (detail_level() != gf::SceneDetailLevel::full) {
        counters_.last_frame_live_sinks = 0U;
        return;
    }
    const gf::NodeGraph& graph = document_.graph();
    const std::span<const gf::NodeRecord> slots = graph.node_slots();
    for (const std::uint32_t slot : visible_nodes()) {
        if (slot >= slots.size() || !slots[slot].live) continue;
        const NodeState* state = evaluator_.state(slot);
        if (state == nullptr || !(*state).time_dependent || !(*state).kind.has_value()) continue;
        if (kind_at(*(*state).kind).family != KindFamily::sink) continue;
        live_sink_rects_.push_back(view().rect_to_view(slots[slot].bounds));
    }
    counters_.last_frame_live_sinks = live_sink_rects_.size();
}

void GraphCanvas::enlist_live_sinks() {
    painted_since_frame_ = true;
    if (live_sink_rects_.empty()) return;
    gf::SurfaceRoot* root = attached_surface_root();
    if (root == nullptr) return;
    // One enlistment per control: NodeCanvas enlists the same record for
    // its flow and withdraws it when the flow stops (GAPS.md N12), so a
    // live sink enlists again on its next paint.
    const std::chrono::milliseconds interval(live_sink_interval_ms);
    const bool fresh = (*root).motion_clock().enlist_control_ambient(*this, interval);
    if (fresh) ++counters_.ambient_enlistments;
}

bool GraphCanvas::on_ambient_frame(gf::FrameTime now) {
    const bool flow = gf::NodeCanvas::on_ambient_frame(now);
    // Not painted since the last frame: the sinks are no longer shown, so
    // the clock may drop the canvas (the idle rule, spec 4.1).
    const bool sinks = painted_since_frame_ && !live_sink_rects_.empty();
    painted_since_frame_ = false;
    if (sinks) {
        if (!clock_start_.has_value()) clock_start_ = now;
        const std::chrono::duration<double> elapsed = now - *clock_start_;
        time_ = elapsed.count();
        if (evaluator_.evaluate(time_)) ++counters_.evaluations;
        // Damage only the live sinks' bounds.
        for (const gf::Rect& rect : live_sink_rects_) invalidate(rect);
        ++counters_.ambient_frames;
    }
    const bool keep = flow || sinks;
    return keep;
}

} // namespace node_graph
