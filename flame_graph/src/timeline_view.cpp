#include "timeline_view.hpp"

#include "profile_ink.hpp"

#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/plots/axis/axis.hpp>
#include <gui_forms/plots/scale/scale.hpp>
#include <gui_forms/surface_root/surface_root.hpp>
#include <gui_forms/types/paint_marks/paint_marks.hpp>
#include <gui_forms/types/painter/painter.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <utility>

namespace flame_graph {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

constexpr std::size_t maximum_outlined_related = 256U;
constexpr std::size_t maximum_tick_segments = 64U;

gf::FontSpec label_font() {
    gf::FontSpec font{};
    font.size = TimelineView::label_size;
    return font;
}

gp::PlotModifiers modifiers_of(gf::Modifier modifiers) noexcept {
    gp::PlotModifiers result{};
    result.shift = gf::has_modifier(modifiers, gf::Modifier::shift);
    result.control = gf::has_modifier(modifiers, gf::Modifier::control);
    result.alt = gf::has_modifier(modifiers, gf::Modifier::alt);
    return result;
}

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

// Measures tick labels with the painter, for the generator's thinning.
class PainterLabelMetrics final : public gp::TickLabelMetrics {
public:
    PainterLabelMetrics(gf::Painter& painter, gf::FontSpec font) noexcept : painter_(&painter), font_(font) {}

    [[nodiscard]] double label_extent(std::string_view label) const override {
        const gf::Size size = (*painter_).measure_text_utf8(label, font_);
        return size.width;
    }

private:
    gf::Painter* painter_{};
    gf::FontSpec font_{};
};

} // namespace

// Spans (of one row, in start order) that end at or before a time.
struct TimelineView::EndsAtOrBefore final {
    std::int64_t time{};
    bool operator()(const TraceSpan& span) const noexcept { return span.end() <= time; }
};

// Spans that start before a time.
struct TimelineView::StartsBefore final {
    std::int64_t time{};
    bool operator()(const TraceSpan& span) const noexcept { return span.start < time; }
};

// Lane tops at or above a content y.
struct TimelineView::LaneStartsAtOrBefore final {
    double y{};
    bool operator()(double top) const noexcept { return top <= y; }
};

TimelineView::TimelineView(gf::StableId stable_id) : gf::Control(std::move(stable_id)) {
    focusable = true;
    const gp::Axis x_axes[1]{make_axis(gp::AxisSide::top, gp::PlotRange{0.0, 1.0})};
    const gp::Axis y_axes[1]{make_axis(gp::AxisSide::left, gp::PlotRange{0.0, 1.0})};
    range_model_.set_axes(std::span<const gp::Axis>(x_axes), std::span<const gp::Axis>(y_axes));
    label_.reserve(256U);
    lane_tops_.reserve(64U);
}

TimelineView::~TimelineView() {}

void TimelineView::set_inputs(const ProfileInputs& inputs) noexcept {
    inputs_ = inputs;
    invalidate(gf::Dirty::paint);
}

std::size_t TimelineView::lane_rows(const ThreadLane& lane) const noexcept {
    if (lane.spans.empty()) return 0U;
    return std::min<std::size_t>(static_cast<std::size_t>(lane.max_depth) + 1U, maximum_lane_rows);
}

void TimelineView::model_changed() {
    row_scroll_ = 0.0;
    lane_tops_.clear();
    content_height_ = 0.0;
    ellipsis_width_ = -1.0;
    if (!inputs_.ready()) {
        invalidate(gf::Dirty::paint);
        return;
    }
    const TraceModel& model = *inputs_.model;
    unit_scale_ = model.unit() == TraceUnit::nanoseconds ? 1.0e-6 : 1.0;
    origin_ = model.first_time();
    double y = 0.0;
    for (const ThreadLane& lane : model.threads()) {
        lane_tops_.push_back(y);
        y += lane_header_height + static_cast<double>(lane_rows(lane)) * row_height + lane_gap;
    }
    content_height_ = y;
    bar_rects_.reserve(maximum_bars);
    bar_colors_.reserve(maximum_bars);
    bar_sources_.reserve(maximum_bars);
    label_widths_.assign(model.names().size(), -1.0F);
    reset_view();
}

void TimelineView::inputs_changed() noexcept {
    invalidate(gf::Dirty::paint);
}

gp::PlotRange TimelineView::x_range() const noexcept {
    const std::span<const gp::Axis> axes = range_model_.x_axes();
    if (axes.empty()) return gp::PlotRange{};
    return axes[0].range;
}

gp::PlotDataBounds TimelineView::data_bounds() const noexcept {
    gp::PlotDataBounds bounds{};
    double extent = 1.0;
    if (inputs_.ready()) {
        const TraceModel& model = *inputs_.model;
        extent = static_cast<double>(model.last_time() - model.first_time()) * unit_scale_;
    }
    bounds.x[0] = gp::PlotRange{0.0, std::max(extent, 1.0e-6)};
    bounds.y[0] = gp::PlotRange{0.0, 1.0};
    return bounds;
}

void TimelineView::set_x_range(gp::PlotRange range) {
    static_cast<void>(range_model_.set_x_range(0U, range));
    static_cast<void>(clamp_range());
    invalidate(gf::Dirty::paint);
}

void TimelineView::reset_view() {
    set_x_range(data_bounds().x[0]);
}

bool TimelineView::clamp_range() {
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

void TimelineView::scroll_rows(double rows) {
    row_scroll_ += rows * row_height;
    clamp_scroll();
    ++statistics_.row_scrolls;
    invalidate(gf::Dirty::paint);
}

void TimelineView::clamp_scroll() noexcept {
    const double limit = std::max(0.0, content_height_ - plot_rect_.height);
    row_scroll_ = std::clamp(row_scroll_, 0.0, limit);
}

double TimelineView::device_x(std::int64_t time) const noexcept {
    const gp::PlotRange range = x_range();
    const double extent = range.extent() > 0.0 ? range.extent() : 1.0;
    const double axis = static_cast<double>(time - origin_) * unit_scale_;
    const double device = plot_rect_.x + (axis - range.minimum) / extent * plot_rect_.width;
    return device;
}

double TimelineView::time_at(double device) const noexcept {
    const gp::PlotRange range = x_range();
    const double width = plot_rect_.width > 0.0 ? plot_rect_.width : 1.0;
    const double axis = range.minimum + (device - plot_rect_.x) / width * range.extent();
    const double time = static_cast<double>(origin_) + axis / unit_scale_;
    return time;
}

double TimelineView::lane_top(ThreadIndex lane) const noexcept {
    if (lane.value >= lane_tops_.size()) return plot_rect_.y;
    return plot_rect_.y + lane_tops_[lane.value] - row_scroll_;
}

std::optional<SpanHit> TimelineView::span_at(gf::Point local) const {
    if (!inputs_.ready() || !laid_out_ || lane_tops_.empty()) return std::nullopt;
    if (local.x < plot_rect_.x || local.x > plot_rect_.x + plot_rect_.width) return std::nullopt;
    if (local.y < plot_rect_.y || local.y > plot_rect_.y + plot_rect_.height) return std::nullopt;
    const double content_y = local.y - plot_rect_.y + row_scroll_;
    const std::vector<double>::const_iterator after =
        std::partition_point(lane_tops_.begin(), lane_tops_.end(), LaneStartsAtOrBefore{content_y});
    if (after == lane_tops_.begin()) return std::nullopt;
    const std::size_t lane_index = static_cast<std::size_t>(after - lane_tops_.begin()) - 1U;
    const ThreadLane& lane = (*inputs_.model).threads()[lane_index];
    const double within = content_y - lane_tops_[lane_index] - lane_header_height;
    if (within < 0.0) return std::nullopt;
    const std::size_t depth = static_cast<std::size_t>(within / row_height);
    if (depth >= lane_rows(lane)) return std::nullopt;
    const std::int64_t time = static_cast<std::int64_t>(std::floor(time_at(local.x)));
    const std::vector<TraceSpan>::const_iterator first = lane.spans.begin() + lane.depth_start[depth];
    const std::vector<TraceSpan>::const_iterator last = lane.spans.begin() + lane.depth_start[depth + 1U];
    const std::vector<TraceSpan>::const_iterator found = std::partition_point(first, last, EndsAtOrBefore{time});
    if (found == last || (*found).start > time) return std::nullopt;
    SpanHit hit{};
    hit.lane = ThreadIndex{static_cast<std::uint32_t>(lane_index)};
    hit.span = static_cast<std::uint32_t>(found - lane.spans.begin());
    hit.node = (*found).node;
    hit.name = (*found).name;
    return hit;
}

gf::Color TimelineView::span_color(const TraceSpan& span) const noexcept {
    const std::vector<gf::Color>& colors = *inputs_.colors;
    const gf::Color base = span.name.value < colors.size() ? colors[span.name.value] : dimmed_fill;
    const SearchFlags& search = *inputs_.search;
    if (!search.active()) return base;
    if (search.matches(span.name)) return search_fill;
    return dimmed_fill;
}

// -- layout and painting ------------------------------------------------------------------------

gf::Size TimelineView::measure(gf::Size available) {
    return available;
}

void TimelineView::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    plot_rect_ = gf::Rect{0.0, ruler_height, final_bounds.width, std::max(0.0, final_bounds.height - ruler_height)};
    range_model_.set_plot(plot_rect_);
    laid_out_ = plot_rect_.width > 0.0 && plot_rect_.height > 0.0;
    clamp_scroll();
}

void TimelineView::paint_ruler(gf::Painter& painter) {
    const gf::Rect bounds = client_rectangle();
    painter.fill_rect(gf::Rect{0.0, 0.0, bounds.width, ruler_height}, ruler_fill);
    const gf::FontSpec font = label_font();
    const std::string_view unit = (*inputs_.model).unit() == TraceUnit::nanoseconds ? "ms" : "samples";
    const std::span<const gp::Axis> axes = range_model_.x_axes();
    if (axes.empty()) return;
    const gp::ScaleMappingResult mapping =
        gp::ScaleMapping::make(gp::axis_scale(axes[0], plot_rect_.x, plot_rect_.x + plot_rect_.width));
    if (mapping.check != gp::ScaleCheck::valid) return;
    gp::TickRequest request{};
    const PainterLabelMetrics metrics(painter, font);
    const gp::TickOutcome outcome = gp::generate_ticks(mapping.mapping, request, &metrics, ticks_);
    ++statistics_.tick_generations;
    if (outcome == gp::TickOutcome::too_small) return;
    std::array<gf::PaintSegment, maximum_tick_segments> segments{};
    std::size_t segment_count = 0U;
    for (const gp::MajorTick& tick : ticks_.majors()) {
        if (segment_count < segments.size()) {
            segments[segment_count] = gf::PaintSegment{gf::Point{tick.device, ruler_height - 6.0},
                                                       gf::Point{tick.device, ruler_height}};
            ++segment_count;
        }
        if (!tick.labelled) continue;
        painter.draw_text_utf8(gf::Point{tick.device + 3.0, 12.0}, tick.label.text(), font, quiet_ink);
    }
    gf::StrokeStyle stroke{};
    stroke.color = quiet_ink;
    painter.stroke_segments(std::span<const gf::PaintSegment>(segments.data(), segment_count), stroke);
    const gf::Size size = painter.measure_text_utf8(unit, font);
    painter.fill_rect(gf::Rect{bounds.width - size.width - 8.0, 0.0, size.width + 8.0, ruler_height - 7.0}, ruler_fill);
    painter.draw_text_utf8(gf::Point{bounds.width - size.width - 4.0, 12.0}, unit, font, label_ink);
}

void TimelineView::push_bar(gf::Rect rect, gf::Color color, BarSource source) {
    if (bar_rects_.size() >= maximum_bars) {
        ++statistics_.bars_over_cap;
        return;
    }
    bar_rects_.push_back(rect);
    bar_colors_.push_back(color);
    bar_sources_.push_back(source);
}

void TimelineView::paint_row(const ThreadLane& lane, std::uint32_t lane_index, std::size_t depth, double y) {
    const double left = plot_rect_.x;
    const double right = plot_rect_.x + plot_rect_.width;
    const std::int64_t view_start = static_cast<std::int64_t>(std::floor(time_at(left)));
    const std::int64_t view_end = static_cast<std::int64_t>(std::ceil(time_at(right)));
    const std::vector<TraceSpan>::const_iterator row_first = lane.spans.begin() + lane.depth_start[depth];
    const std::vector<TraceSpan>::const_iterator row_last = lane.spans.begin() + lane.depth_start[depth + 1U];
    std::vector<TraceSpan>::const_iterator at = std::partition_point(row_first, row_last, EndsAtOrBefore{view_start});
    const double bar_height = row_height - 1.0;
    bool pending = false;
    double pending_left = 0.0;
    double pending_right = 0.0;
    while (at != row_last && (*at).start <= view_end) {
        const TraceSpan& span = *at;
        ++frame_visits_;
        const double x0 = device_x(span.start);
        const double x1 = device_x(span.end());
        if (x1 - x0 < minimum_span_width) {
            ++statistics_.spans_merged;
            if (pending && x0 <= pending_right + 1.0) {
                pending_right = std::max(pending_right, std::max(x1, x0 + 1.0));
            } else {
                if (pending) {
                    push_bar(gf::Rect{pending_left, y, std::max(1.0, pending_right - pending_left), bar_height},
                             merged_fill, BarSource{lane_index, std::nullopt});
                }
                pending = true;
                pending_left = x0;
                pending_right = std::max(x1, x0 + 1.0);
            }
            // Jump past every span starting inside the merged pixel; the
            // last of them may reach beyond it, so it is visited next.
            const std::int64_t jump_time = static_cast<std::int64_t>(std::ceil(time_at(std::floor(pending_right) + 1.0)));
            const std::vector<TraceSpan>::const_iterator beyond =
                std::partition_point(at + 1, row_last, StartsBefore{jump_time});
            std::vector<TraceSpan>::const_iterator next = at + 1;
            if (beyond - next > 1) {
                ++statistics_.jumps;
                statistics_.spans_jumped += static_cast<std::uint64_t>((beyond - next) - 1);
                next = beyond - 1;
            }
            at = next;
            continue;
        }
        if (pending) {
            push_bar(gf::Rect{pending_left, y, std::max(1.0, pending_right - pending_left), bar_height}, merged_fill,
                     BarSource{lane_index, std::nullopt});
            pending = false;
        }
        const double clipped_left = std::max(x0, left - 1.0);
        const double clipped_right = std::min(x1, right + 1.0);
        const double width = std::max(1.0, clipped_right - clipped_left - 1.0);
        push_bar(gf::Rect{clipped_left, y, width, bar_height}, span_color(span),
                 BarSource{lane_index, static_cast<std::uint32_t>(at - lane.spans.begin())});
        ++at;
    }
    if (pending) {
        push_bar(gf::Rect{pending_left, y, std::max(1.0, pending_right - pending_left), bar_height}, merged_fill,
                 BarSource{lane_index, std::nullopt});
    }
}

void TimelineView::paint_lane_headers(gf::Painter& painter) {
    const TraceModel& model = *inputs_.model;
    const gf::FontSpec font = label_font();
    const double top = plot_rect_.y;
    const double bottom = plot_rect_.y + plot_rect_.height;
    for (std::size_t index = 0U; index < lane_tops_.size(); ++index) {
        const double y = plot_rect_.y + lane_tops_[index] - row_scroll_;
        if (y > bottom) break;
        if (y + lane_header_height < top) continue;
        const ThreadLane& lane = model.threads()[index];
        painter.fill_rect(gf::Rect{plot_rect_.x, y, plot_rect_.width, lane_header_height}, lane_header_fill);
        FormattedNumber spans{};
        format_count(static_cast<std::uint64_t>(lane.spans.size()), spans);
        label_.assign(model.names().text(lane.label));
        label_.append("  (");
        label_.append(spans.text());
        label_.append(" spans");
        if (static_cast<std::size_t>(lane.max_depth) + 1U > maximum_lane_rows) label_.append(", deeper rows hidden");
        label_.append(")");
        painter.draw_text_utf8(gf::Point{plot_rect_.x + 6.0, y + 13.0}, label_, font, label_ink);
    }
}

void TimelineView::paint_outlines_and_labels(gf::Painter& painter) {
    const TraceModel& model = *inputs_.model;
    const ProfileSelection& selection = *inputs_.selection;
    const gf::FontSpec font = label_font();
    if (ellipsis_width_ < 0.0) ellipsis_width_ = painter.measure_text_utf8(label_ellipsis, font).width;
    std::size_t related = 0U;
    for (std::size_t bar = 0U; bar < bar_rects_.size(); ++bar) {
        const BarSource& source = bar_sources_[bar];
        if (!source.span.has_value()) continue;
        const TraceSpan& span = model.threads()[source.lane].spans[*source.span];
        const gf::Rect& rect = bar_rects_[bar];
        if (selection.node.has_value() && span.node == *selection.node) {
            painter.stroke_rect(rect, selection_ink, 2.0);
        } else if (selection.name.has_value() && span.name == *selection.name && related < maximum_outlined_related) {
            painter.stroke_rect(rect, related_ink, 1.0);
            ++related;
        }
        if (rect.width < label_minimum_width || span.name.value >= label_widths_.size()) continue;
        const std::string_view text = model.names().text(span.name);
        float& measured = label_widths_[span.name.value];
        if (measured < 0.0F) {
            measured = static_cast<float>(painter.measure_text_utf8(text, font).width);
            ++statistics_.label_measures;
        }
        const double start = std::max(rect.x, plot_rect_.x) + 3.0;
        const double available = rect.x + rect.width - start - 3.0;
        const gf::Point origin{start, rect.y + row_height * 0.5 + font.size * 0.35};
        if (static_cast<double>(measured) <= available) {
            painter.draw_text_utf8(origin, text, font, label_ink);
        } else if (cut_label(text, static_cast<double>(measured), available - font.size * 0.5, ellipsis_width_, label_)) {
            painter.draw_text_utf8(origin, label_, font, label_ink);
        } else {
            continue;
        }
        ++statistics_.labels_drawn;
    }
}

void TimelineView::on_paint(gf::Painter& painter, gf::Rect local_damage) {
    static_cast<void>(local_damage);
    const gf::Rect bounds = client_rectangle();
    painter.fill_rect(bounds, view_background);
    ++statistics_.frames_painted;
    statistics_.last_frame_visits = 0U;
    statistics_.last_frame_bars = 0U;
    statistics_.last_frame_rows = 0U;
    if (!inputs_.ready() || !laid_out_ || lane_tops_.empty()) return;
    paint_ruler(painter);
    const TraceModel& model = *inputs_.model;
    const double top = plot_rect_.y;
    const double bottom = plot_rect_.y + plot_rect_.height;
    bar_rects_.clear();
    bar_colors_.clear();
    bar_sources_.clear();
    frame_visits_ = 0U;
    std::uint64_t rows = 0U;
    for (std::size_t index = 0U; index < lane_tops_.size(); ++index) {
        const double lane_y = plot_rect_.y + lane_tops_[index] - row_scroll_;
        if (lane_y > bottom) break;
        const ThreadLane& lane = model.threads()[index];
        const std::size_t lane_row_count = lane_rows(lane);
        for (std::size_t depth = 0U; depth < lane_row_count; ++depth) {
            const double y = lane_y + lane_header_height + static_cast<double>(depth) * row_height;
            if (y > bottom) break;
            if (y + row_height < top) continue;
            paint_row(lane, static_cast<std::uint32_t>(index), depth, y);
            ++rows;
        }
    }
    painter.save();
    painter.clip_rect(plot_rect_);
    paint_lane_headers(painter);
    painter.fill_rects_each(std::span<const gf::Rect>(bar_rects_.data(), bar_rects_.size()),
                            std::span<const gf::Color>(bar_colors_.data(), bar_colors_.size()));
    paint_outlines_and_labels(painter);
    painter.restore();
    statistics_.spans_visited += frame_visits_;
    statistics_.bars_drawn += bar_rects_.size();
    statistics_.last_frame_visits = frame_visits_;
    statistics_.last_frame_bars = bar_rects_.size();
    statistics_.last_frame_rows = rows;
}

// -- input -------------------------------------------------------------------------------------

void TimelineView::take_change(const gp::PlotViewChange& change) {
    if (!change.view && !change.transient) return;
    if (change.view) {
        static_cast<void>(clamp_range());
        ++statistics_.view_changes;
        view_changed_.emit();
    }
    invalidate(gf::Dirty::paint);
}

void TimelineView::on_mouse_down(gf::MouseEventArgs& arguments) {
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

void TimelineView::on_mouse_move(gf::MouseEventArgs& arguments) {
    if (!pressed_) return;
    take_change(range_model_.pointer_moved(arguments.location));
    arguments.handled = true;
}

void TimelineView::on_mouse_up(gf::MouseEventArgs& arguments) {
    if (!pressed_) return;
    pressed_ = false;
    set_pointer_capture(false);
    take_change(range_model_.pointer_released(arguments.location));
    arguments.handled = true;
    const double moved_x = arguments.location.x - press_point_.x;
    const double moved_y = arguments.location.y - press_point_.y;
    if (moved_x * moved_x + moved_y * moved_y > gp::drag_threshold * gp::drag_threshold) return;
    const std::optional<SpanHit> hit = span_at(arguments.location);
    if (!hit.has_value()) return;
    ++statistics_.picks;
    NodePick pick{};
    pick.node = (*hit).node;
    pick.name = (*hit).name;
    node_picked_.emit(pick);
}

void TimelineView::on_mouse_wheel(gf::MouseEventArgs& arguments) {
    if (!laid_out_) return;
    double notches = arguments.wheel_delta.y;
    if (arguments.wheel_unit == gf::WheelDeltaUnit::pixels) notches /= 40.0;
    if (gf::has_modifier(arguments.modifiers, gf::Modifier::shift)) {
        scroll_rows(-notches);
    } else {
        take_change(range_model_.wheel(arguments.location, notches, horizontal_zone()));
    }
    arguments.handled = true;
}

void TimelineView::on_mouse_double_click(gf::MouseEventArgs& arguments) {
    reset_view();
    view_changed_.emit();
    arguments.handled = true;
}

void TimelineView::on_key_down(gf::KeyEventArgs& arguments) {
    gf::Control::on_key_down(arguments);
    if (arguments.handled) return;
    const std::uint32_t key = arguments.physical_key;
    const gp::PlotModifiers modifiers = modifiers_of(arguments.modifiers);
    const double page = std::max(1.0, std::floor(plot_rect_.height / row_height) * 0.5);
    if (key == gf::PhysicalKey::home || key == gf::PhysicalKey::escape) {
        reset_view();
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
        scroll_rows(page);
    } else if (key == gf::PhysicalKey::page_up) {
        scroll_rows(-page);
    } else {
        return;
    }
    arguments.handled = true;
}

} // namespace flame_graph
