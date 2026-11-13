#include "flame_view.hpp"

#include "profile_ink.hpp"

#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/surface_root/surface_root.hpp>
#include <gui_forms/types/painter/painter.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

namespace flame_graph {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

constexpr std::size_t maximum_outlined_related = 256U;

gf::FontSpec label_font() {
    gf::FontSpec font{};
    font.size = FlameView::label_size;
    return font;
}

gp::PlotModifiers modifiers_of(gf::Modifier modifiers) noexcept {
    gp::PlotModifiers result{};
    result.shift = gf::has_modifier(modifiers, gf::Modifier::shift);
    result.control = gf::has_modifier(modifiers, gf::Modifier::control);
    result.alt = gf::has_modifier(modifiers, gf::Modifier::alt);
    return result;
}

// Every gesture goes to the view model as an x-axis gesture, so only the
// horizontal range moves.
gp::PlotZone horizontal_zone() noexcept {
    gp::PlotZone zone{};
    zone.kind = gp::PlotZoneKind::x_axis;
    return zone;
}

gp::Axis make_axis(gp::AxisSide side, gp::PlotRange range) noexcept {
    gp::Axis axis{};
    axis.kind = gp::ScaleKind::linear;
    axis.range = range;
    axis.side = side;
    return axis;
}

} // namespace

// Among a node's children (in offset order), those ending at or before
// the weight.
struct FlameView::ChildEndsBefore final {
    const TraceModel* model{};
    double weight{};
    bool operator()(std::uint32_t child) const noexcept {
        const CallNode& node = (*model).node(CallNodeIndex{child});
        return static_cast<double>(node.offset + node.total) <= weight;
    }
};

FlameView::FlameView(gf::StableId stable_id) : gf::Control(std::move(stable_id)) {
    focusable = true;
    const gp::Axis x_axes[1]{make_axis(gp::AxisSide::bottom, gp::PlotRange{0.0, 1.0})};
    const gp::Axis y_axes[1]{make_axis(gp::AxisSide::left, gp::PlotRange{0.0, 1.0})};
    range_model_.set_axes(std::span<const gp::Axis>(x_axes), std::span<const gp::Axis>(y_axes));
    label_.reserve(256U);
}

FlameView::~FlameView() {}

void FlameView::set_inputs(const ProfileInputs& inputs) noexcept {
    inputs_ = inputs;
    invalidate(gf::Dirty::paint);
}

void FlameView::model_changed() {
    zoom_root_.reset();
    row_scroll_ = 0.0;
    deepest_ = 0U;
    ellipsis_width_ = -1.0;
    if (!inputs_.ready()) {
        invalidate(gf::Dirty::paint);
        return;
    }
    const TraceModel& model = *inputs_.model;
    for (const CallNode& node : model.nodes()) deepest_ = std::max<std::size_t>(deepest_, node.depth);
    // One reservation per load: the walk holds at most every node, the cell
    // arrays at most maximum_cells.
    walk_.reserve(model.nodes().size());
    const std::size_t cells = std::min(model.nodes().size(), maximum_cells);
    cell_rects_.reserve(cells);
    cell_colors_.reserve(cells);
    cell_nodes_.reserve(cells);
    label_widths_.assign(model.names().size(), -1.0F);
    set_range(gp::PlotRange{0.0, static_cast<double>(std::max<std::int64_t>(1, model.total_weight()))});
}

void FlameView::inputs_changed() noexcept {
    invalidate(gf::Dirty::paint);
}

void FlameView::set_orientation(FlameOrientation orientation) {
    if (orientation_ == orientation) return;
    orientation_ = orientation;
    invalidate(gf::Dirty::paint);
}

gp::PlotRange FlameView::x_range() const noexcept {
    const std::span<const gp::Axis> axes = range_model_.x_axes();
    if (axes.empty()) return gp::PlotRange{};
    return axes[0].range;
}

gp::PlotDataBounds FlameView::data_bounds() const noexcept {
    gp::PlotDataBounds bounds{};
    const double total = inputs_.ready() ? static_cast<double>((*inputs_.model).total_weight()) : 1.0;
    bounds.x[0] = gp::PlotRange{0.0, std::max(1.0, total)};
    bounds.y[0] = gp::PlotRange{0.0, 1.0};
    return bounds;
}

void FlameView::set_range(gp::PlotRange range) {
    static_cast<void>(range_model_.set_x_range(0U, range));
    static_cast<void>(clamp_range());
    invalidate(gf::Dirty::paint);
}

bool FlameView::clamp_range() {
    const gp::PlotRange bounds = data_bounds().x[0];
    gp::PlotRange range = x_range();
    const gp::PlotRange before = range;
    const double extent = std::min(range.extent(), bounds.extent());
    if (range.minimum < bounds.minimum) range.minimum = bounds.minimum;
    if (range.minimum + extent > bounds.maximum) range.minimum = bounds.maximum - extent;
    range.maximum = range.minimum + extent;
    if (range == before) return false;
    static_cast<void>(range_model_.set_x_range(0U, range));
    return true;
}

bool FlameView::zoom_to(CallNodeIndex node) {
    if (!inputs_.ready() || node.value >= (*inputs_.model).nodes().size()) return false;
    const CallNode& record = (*inputs_.model).node(node);
    if (record.total <= 0) return false;
    zoom_root_ = node;
    ++statistics_.zooms;
    const double start = static_cast<double>(record.offset);
    set_range(gp::PlotRange{start, start + static_cast<double>(record.total)});
    return true;
}

void FlameView::reset_zoom() {
    zoom_root_.reset();
    ++statistics_.resets;
    set_range(data_bounds().x[0]);
}

void FlameView::scroll_rows(double rows) {
    row_scroll_ += rows * row_height;
    clamp_scroll();
    ++statistics_.row_scrolls;
    invalidate(gf::Dirty::paint);
}

void FlameView::clamp_scroll() noexcept {
    const double content = static_cast<double>(deepest_ + 1U) * row_height;
    const double limit = std::max(0.0, content - plot_rect_.height);
    row_scroll_ = std::clamp(row_scroll_, 0.0, limit);
}

std::size_t FlameView::visible_rows() const noexcept {
    if (plot_rect_.height <= 0.0) return 0U;
    return static_cast<std::size_t>(std::ceil(plot_rect_.height / row_height)) + 1U;
}

// -- geometry ---------------------------------------------------------------------------------

double FlameView::device_x(double weight) const noexcept {
    const gp::PlotRange range = x_range();
    const double extent = range.extent() > 0.0 ? range.extent() : 1.0;
    const double device = plot_rect_.x + (weight - range.minimum) / extent * plot_rect_.width;
    return device;
}

double FlameView::weight_at(double device) const noexcept {
    const gp::PlotRange range = x_range();
    const double width = plot_rect_.width > 0.0 ? plot_rect_.width : 1.0;
    const double weight = range.minimum + (device - plot_rect_.x) / width * range.extent();
    return weight;
}

double FlameView::row_top(std::size_t depth) const noexcept {
    const double rows = static_cast<double>(depth);
    if (orientation_ == FlameOrientation::icicle) return plot_rect_.y + rows * row_height - row_scroll_;
    return plot_rect_.y + plot_rect_.height - (rows + 1.0) * row_height + row_scroll_;
}

std::optional<std::size_t> FlameView::depth_at(double device_y) const noexcept {
    if (device_y < plot_rect_.y || device_y > plot_rect_.y + plot_rect_.height) return std::nullopt;
    double rows = 0.0;
    if (orientation_ == FlameOrientation::icicle) {
        rows = (device_y - plot_rect_.y + row_scroll_) / row_height;
    } else {
        rows = (plot_rect_.y + plot_rect_.height + row_scroll_ - device_y) / row_height;
    }
    if (rows < 0.0) return std::nullopt;
    return static_cast<std::size_t>(rows);
}

std::optional<CallNodeIndex> FlameView::node_at(gf::Point local) const {
    if (!inputs_.ready() || !laid_out_) return std::nullopt;
    if (local.x < plot_rect_.x || local.x > plot_rect_.x + plot_rect_.width) return std::nullopt;
    const std::optional<std::size_t> depth = depth_at(local.y);
    if (!depth.has_value()) return std::nullopt;
    const TraceModel& model = *inputs_.model;
    const double weight = weight_at(local.x);
    if (weight < 0.0 || weight >= static_cast<double>(model.total_weight())) return std::nullopt;
    // Down from the root: at each depth the child whose extent holds the
    // weight (children are in offset order, so a binary search finds it).
    CallNodeIndex node = root_node;
    for (std::size_t level = 0U; level < *depth; ++level) {
        const std::span<const std::uint32_t> children = model.children(node);
        const std::span<const std::uint32_t>::iterator found =
            std::partition_point(children.begin(), children.end(), ChildEndsBefore{&model, weight});
        if (found == children.end()) return std::nullopt;
        const CallNode& child = model.node(CallNodeIndex{*found});
        if (static_cast<double>(child.offset) > weight) return std::nullopt;
        node = CallNodeIndex{*found};
    }
    return node;
}

std::optional<gf::Rect> FlameView::cell_rect(CallNodeIndex node) const {
    if (!inputs_.ready() || !laid_out_ || node.value >= (*inputs_.model).nodes().size()) return std::nullopt;
    const CallNode& record = (*inputs_.model).node(node);
    const double x0 = device_x(static_cast<double>(record.offset));
    const double x1 = device_x(static_cast<double>(record.offset + record.total));
    const gf::Rect rect{x0, row_top(record.depth), x1 - x0, row_height};
    return rect;
}

gf::Color FlameView::cell_color(const CallNode& node) const noexcept {
    if (node.synthetic) return lane_header_fill;
    const std::vector<gf::Color>& colors = *inputs_.colors;
    const gf::Color base = node.name.value < colors.size() ? colors[node.name.value] : dimmed_fill;
    const SearchFlags& search = *inputs_.search;
    if (!search.active()) return base;
    if (search.matches(node.name)) return search_fill;
    return dimmed_fill;
}

// -- layout and painting ------------------------------------------------------------------------

gf::Size FlameView::measure(gf::Size available) {
    return available;
}

void FlameView::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    plot_rect_ = gf::Rect{0.0, header_height, final_bounds.width, std::max(0.0, final_bounds.height - header_height)};
    range_model_.set_plot(plot_rect_);
    laid_out_ = plot_rect_.width > 0.0 && plot_rect_.height > 0.0;
    clamp_scroll();
}

void FlameView::paint_header(gf::Painter& painter) {
    const gf::Rect bounds = client_rectangle();
    painter.fill_rect(gf::Rect{0.0, 0.0, bounds.width, header_height}, ruler_fill);
    gf::FontSpec font = label_font();
    const TraceModel& model = *inputs_.model;
    const CallNodeIndex shown = zoom_root_.value_or(root_node);
    const CallNode& node = model.node(shown);
    FormattedNumber weight{};
    format_weight(node.total, model.unit(), weight);
    FormattedNumber share{};
    format_share(node.total, model.total_weight(), share);
    label_.assign(orientation_ == FlameOrientation::icicle ? "Icicle: " : "Flame graph: ");
    label_.append(model.names().text(node.name));
    label_.append("  ");
    label_.append(weight.text());
    label_.append(" (");
    label_.append(share.text());
    label_.append(")");
    painter.draw_text_utf8(gf::Point{6.0, 14.0}, label_, font, label_ink);
    const SearchFlags& search = *inputs_.search;
    if (!search.active()) return;
    FormattedNumber matched{};
    format_share(search.summary.matched_weight, model.total_weight(), matched);
    label_.assign("matched ");
    label_.append(matched.text());
    font.weight = 600U;
    const gf::Size size = painter.measure_text_utf8(label_, font);
    painter.draw_text_utf8(gf::Point{bounds.width - size.width - 6.0, 14.0}, label_, font, search_fill);
}

void FlameView::on_paint(gf::Painter& painter, gf::Rect local_damage) {
    static_cast<void>(local_damage);
    const gf::Rect bounds = client_rectangle();
    painter.fill_rect(bounds, view_background);
    ++statistics_.frames_painted;
    statistics_.last_frame_visits = 0U;
    statistics_.last_frame_cells = 0U;
    statistics_.last_frame_labels = 0U;
    if (!inputs_.ready() || !laid_out_ || (*inputs_.model).nodes().empty()) return;
    paint_header(painter);
    const TraceModel& model = *inputs_.model;
    const double left = plot_rect_.x;
    const double right = plot_rect_.x + plot_rect_.width;
    const double top = plot_rect_.y;
    const double bottom = plot_rect_.y + plot_rect_.height;
    cell_rects_.clear();
    cell_colors_.clear();
    cell_nodes_.clear();
    walk_.clear();
    walk_.push_back(root_node.value);
    std::uint64_t visits = 0U;
    while (!walk_.empty()) {
        const std::uint32_t index = walk_.back();
        walk_.pop_back();
        ++visits;
        const CallNode& node = model.node(CallNodeIndex{index});
        const double x0 = device_x(static_cast<double>(node.offset));
        const double x1 = device_x(static_cast<double>(node.offset + node.total));
        if (x1 < left || x0 > right) {
            ++statistics_.cells_culled;
            continue;
        }
        if (x1 - x0 < minimum_cell_width) {
            ++statistics_.cells_pruned_narrow;
            continue;
        }
        const double y = row_top(node.depth);
        // Deeper rows lie further in the growth direction: past the far
        // edge, nothing below this node can show.
        const bool beyond = orientation_ == FlameOrientation::icicle ? y > bottom : y + row_height < top;
        if (beyond) continue;
        const bool row_visible = y + row_height >= top && y <= bottom;
        if (row_visible) {
            if (cell_rects_.size() < maximum_cells) {
                const double clipped_left = std::max(x0, left - 1.0);
                const double clipped_right = std::min(x1, right + 1.0);
                // One unit of gap right and below, so neighbours read apart.
                const double width = std::max(0.5, clipped_right - clipped_left - 1.0);
                cell_rects_.push_back(gf::Rect{clipped_left, y, width, row_height - 1.0});
                cell_colors_.push_back(cell_color(node));
                cell_nodes_.push_back(index);
            } else {
                ++statistics_.cells_over_cap;
            }
        }
        const std::span<const std::uint32_t> children = model.children(CallNodeIndex{index});
        for (const std::uint32_t child : children) walk_.push_back(child);
    }
    painter.save();
    painter.clip_rect(plot_rect_);
    painter.fill_rects_each(std::span<const gf::Rect>(cell_rects_.data(), cell_rects_.size()),
                            std::span<const gf::Color>(cell_colors_.data(), cell_colors_.size()));
    paint_outlines(painter);
    paint_labels(painter);
    painter.restore();
    statistics_.cells_visited += visits;
    statistics_.cells_drawn += cell_rects_.size();
    statistics_.last_frame_visits = visits;
    statistics_.last_frame_cells = cell_rects_.size();
}

void FlameView::paint_outlines(gf::Painter& painter) {
    const ProfileSelection& selection = *inputs_.selection;
    if (!selection.node.has_value() && !selection.name.has_value()) return;
    const TraceModel& model = *inputs_.model;
    std::size_t related = 0U;
    for (std::size_t cell = 0U; cell < cell_nodes_.size(); ++cell) {
        const std::uint32_t index = cell_nodes_[cell];
        const CallNode& node = model.node(CallNodeIndex{index});
        if (selection.node.has_value() && (*selection.node).value == index) {
            painter.stroke_rect(cell_rects_[cell], selection_ink, 2.0);
            continue;
        }
        if (selection.name.has_value() && node.name == *selection.name && related < maximum_outlined_related) {
            painter.stroke_rect(cell_rects_[cell], related_ink, 1.0);
            ++related;
        }
    }
}

void FlameView::paint_labels(gf::Painter& painter) {
    const gf::FontSpec font = label_font();
    const TraceModel& model = *inputs_.model;
    if (ellipsis_width_ < 0.0) ellipsis_width_ = painter.measure_text_utf8(label_ellipsis, font).width;
    std::uint64_t labels = 0U;
    for (std::size_t cell = 0U; cell < cell_nodes_.size(); ++cell) {
        const gf::Rect& rect = cell_rects_[cell];
        if (rect.width < label_minimum_width) continue;
        const CallNode& node = model.node(CallNodeIndex{cell_nodes_[cell]});
        const std::string_view text = model.names().text(node.name);
        if (node.name.value >= label_widths_.size()) continue;
        float& measured = label_widths_[node.name.value];
        if (measured < 0.0F) {
            measured = static_cast<float>(painter.measure_text_utf8(text, font).width);
            ++statistics_.label_measures;
        }
        // Labels start at the visible part of the cell.
        const double start = std::max(rect.x, plot_rect_.x) + label_padding;
        const double available = rect.x + rect.width - start - label_padding;
        const gf::Point origin{start, rect.y + row_height * 0.5 + font.size * 0.35};
        if (static_cast<double>(measured) <= available) {
            painter.draw_text_utf8(origin, text, font, label_ink);
        } else {
            // A glyph's margin for the estimate (cut_label).
            if (!cut_label(text, static_cast<double>(measured), available - font.size * 0.5, ellipsis_width_, label_)) {
                continue;
            }
            painter.draw_text_utf8(origin, label_, font, label_ink);
            ++statistics_.labels_cut;
        }
        ++labels;
    }
    statistics_.labels_drawn += labels;
    statistics_.last_frame_labels = labels;
}

// -- input -------------------------------------------------------------------------------------

void FlameView::take_change(const gp::PlotViewChange& change) {
    if (!change.view && !change.transient) return;
    if (change.view) {
        static_cast<void>(clamp_range());
        ++statistics_.view_changes;
        view_changed_.emit();
    }
    invalidate(gf::Dirty::paint);
}

void FlameView::on_mouse_down(gf::MouseEventArgs& arguments) {
    gf::SurfaceRoot* root = attached_surface_root();
    if (root != nullptr) static_cast<void>((*root).request_focus(shared_from_this()));
    if (arguments.button != gf::PointerButton::primary || !laid_out_) return;
    pressed_ = true;
    press_point_ = arguments.location;
    set_pointer_capture(true);
    take_change(range_model_.pointer_pressed(arguments.location, gp::PlotPointerButton::primary,
                                             modifiers_of(arguments.modifiers), horizontal_zone()));
    arguments.handled = true;
}

void FlameView::on_mouse_move(gf::MouseEventArgs& arguments) {
    if (!pressed_) return;
    take_change(range_model_.pointer_moved(arguments.location));
    arguments.handled = true;
}

void FlameView::on_mouse_up(gf::MouseEventArgs& arguments) {
    if (!pressed_) return;
    pressed_ = false;
    set_pointer_capture(false);
    take_change(range_model_.pointer_released(arguments.location));
    arguments.handled = true;
    const double moved_x = arguments.location.x - press_point_.x;
    const double moved_y = arguments.location.y - press_point_.y;
    if (moved_x * moved_x + moved_y * moved_y > gp::drag_threshold * gp::drag_threshold) return;
    const std::optional<CallNodeIndex> node = node_at(arguments.location);
    if (!node.has_value()) return;
    ++statistics_.picks;
    NodePick pick{};
    pick.node = *node;
    pick.name = (*inputs_.model).node(*node).name;
    node_picked_.emit(pick);
}

void FlameView::on_mouse_wheel(gf::MouseEventArgs& arguments) {
    if (!laid_out_) return;
    double notches = arguments.wheel_delta.y;
    if (arguments.wheel_unit == gf::WheelDeltaUnit::pixels) notches /= 40.0;
    if (gf::has_modifier(arguments.modifiers, gf::Modifier::shift)) {
        // Towards the deep end on a wheel turned towards the user.
        scroll_rows(-notches);
    } else {
        take_change(range_model_.wheel(arguments.location, notches, horizontal_zone()));
    }
    arguments.handled = true;
}

void FlameView::on_mouse_double_click(gf::MouseEventArgs& arguments) {
    const std::optional<CallNodeIndex> node = node_at(arguments.location);
    if (node.has_value() && *node != root_node && zoom_to(*node)) {
        view_changed_.emit();
    } else {
        reset_zoom();
        view_changed_.emit();
    }
    arguments.handled = true;
}

void FlameView::on_key_down(gf::KeyEventArgs& arguments) {
    gf::Control::on_key_down(arguments);
    if (arguments.handled) return;
    const std::uint32_t key = arguments.physical_key;
    const gp::PlotModifiers modifiers = modifiers_of(arguments.modifiers);
    if (key == gf::PhysicalKey::home || key == gf::PhysicalKey::escape) {
        reset_zoom();
        view_changed_.emit();
    } else if (key == gf::PhysicalKey::equal || key == gf::PhysicalKey::keypad_plus) {
        take_change(range_model_.key(gp::PlotKey::zoom_in, modifiers, data_bounds()));
    } else if (key == gf::PhysicalKey::minus || key == gf::PhysicalKey::keypad_minus) {
        take_change(range_model_.key(gp::PlotKey::zoom_out, modifiers, data_bounds()));
    } else if (key == gf::PhysicalKey::left) {
        take_change(range_model_.key(gp::PlotKey::left, modifiers, data_bounds()));
    } else if (key == gf::PhysicalKey::right) {
        take_change(range_model_.key(gp::PlotKey::right, modifiers, data_bounds()));
    } else if (key == gf::PhysicalKey::page_down) {
        scroll_rows(static_cast<double>(visible_rows()) * 0.5);
    } else if (key == gf::PhysicalKey::page_up) {
        scroll_rows(-static_cast<double>(visible_rows()) * 0.5);
    } else {
        return;
    }
    arguments.handled = true;
}

} // namespace flame_graph
