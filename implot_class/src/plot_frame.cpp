#include "plot_frame.hpp"

#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/plots/scale/scale.hpp>
#include <gui_forms/surface_root/surface_root.hpp>
#include <gui_forms/types/painter/painter.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>

namespace implot_class {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

// The frame's colours. The plots package's theme roles (axis, tick,
// grid_major, ...) are resolved only inside PlotView; a consumer's own plot
// kind cannot read them (GAPS.md P5), so these are constants.
constexpr gf::Color plot_background = gf::Color::rgba(252, 252, 250);
constexpr gf::Color frame_background = gf::Color::rgba(238, 238, 234);
constexpr gf::Color frame_edge = gf::Color::rgba(120, 120, 116);
constexpr gf::Color grid_major = gf::Color::rgba(214, 214, 208);
constexpr gf::Color grid_minor = gf::Color::rgba(232, 232, 228);
constexpr gf::Color label_ink = gf::Color::rgba(48, 48, 46);
constexpr gf::Color box_ink = gf::Color::rgba(40, 90, 180);

// Measures tick labels with the painter's text metrics, for thinning. The
// interface is the primitives' (runtime variation per host and font).
class PainterLabelMetrics final : public gp::TickLabelMetrics {
public:
    PainterLabelMetrics(gf::Painter& painter, gf::FontSpec font, bool horizontal) noexcept
        : painter_(&painter), font_(font), horizontal_(horizontal) {}

    [[nodiscard]] double label_extent(std::string_view label) const override {
        const gf::Size size = (*painter_).measure_text_utf8(label, font_);
        const double extent = horizontal_ ? size.width : size.height;
        return extent;
    }

private:
    gf::Painter* painter_{};
    gf::FontSpec font_{};
    bool horizontal_{};
};

gf::FontSpec label_font() {
    gf::FontSpec font{};
    font.size = 11.0;
    return font;
}

gp::PlotModifiers modifiers_of(gf::Modifier modifiers) noexcept {
    gp::PlotModifiers result{};
    result.shift = gf::has_modifier(modifiers, gf::Modifier::shift);
    result.control = gf::has_modifier(modifiers, gf::Modifier::control);
    result.alt = gf::has_modifier(modifiers, gf::Modifier::alt);
    return result;
}

gp::Axis make_axis(gp::AxisSide side, gp::PlotRange range) noexcept {
    gp::Axis axis{};
    axis.kind = gp::ScaleKind::linear;
    axis.range = range;
    axis.side = side;
    return axis;
}

} // namespace

PlotFrame::PlotFrame(gf::StableId stable_id) : gf::Control(std::move(stable_id)) {
    focusable = true;
    const gp::Axis x_axes[1]{make_axis(gp::AxisSide::bottom, gp::PlotRange{0.0, 1.0})};
    const gp::Axis y_axes[1]{make_axis(gp::AxisSide::left, gp::PlotRange{0.0, 1.0})};
    model_.set_axes(std::span<const gp::Axis>(x_axes), std::span<const gp::Axis>(y_axes));
    title_.reserve(64U);
}

PlotFrame::~PlotFrame() {}

void PlotFrame::set_title(std::string title) {
    title_ = std::move(title);
    invalidate(gf::Dirty::paint);
}

bool PlotFrame::set_ranges(gp::PlotRange x, gp::PlotRange y) {
    const bool x_set = model_.set_x_range(0U, x);
    const bool y_set = model_.set_y_range(0U, y);
    if (!x_set && !y_set) return false;
    fit_pending_ = false;
    gp::PlotViewChange change{};
    change.view = true;
    take_change(change);
    return true;
}

gp::PlotRange PlotFrame::x_range() const noexcept {
    const std::span<const gp::Axis> axes = model_.x_axes();
    if (axes.empty()) return gp::PlotRange{};
    return axes[0].range;
}

gp::PlotRange PlotFrame::y_range() const noexcept {
    const std::span<const gp::Axis> axes = model_.y_axes();
    if (axes.empty()) return gp::PlotRange{};
    return axes[0].range;
}

gp::PlotDataBounds PlotFrame::data_bounds() const {
    const PlotFrameView extent = data_extent();
    gp::PlotDataBounds bounds{};
    bounds.x[0] = extent.x;
    bounds.y[0] = extent.y;
    return bounds;
}

void PlotFrame::fit() {
    fit_pending_ = false;
    const gp::PlotViewChange change = model_.fit(data_bounds());
    take_change(change);
}

// -- layout ---------------------------------------------------------------------------------

gf::Size PlotFrame::measure(gf::Size available) {
    return available;
}

void PlotFrame::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    const double width = std::max(0.0, final_bounds.width - left_margin - right_margin());
    const double height = std::max(0.0, final_bounds.height - title_height - bottom_margin);
    plot_rect_ = gf::Rect{left_margin, title_height, width, height};
    model_.set_plot(plot_rect_);
    // The painter's scratch: one reservation per size change, never while
    // painting.
    const std::size_t columns = static_cast<std::size_t>(std::ceil(width)) + 1U;
    const std::size_t points = maximum_points();
    if (columns > reserved_columns_ || points > reserved_points_) {
        reserved_columns_ = std::max(columns, reserved_columns_);
        reserved_points_ = std::max(points, reserved_points_);
        marks_.reserve(reserved_columns_, reserved_points_);
    }
    if (fit_pending_ && width > 0.0 && height > 0.0) {
        fit_pending_ = false;
        static_cast<void>(model_.fit(data_bounds()));
    }
    static_cast<void>(update_transform());
}

bool PlotFrame::update_transform() {
    const std::span<const gp::Axis> x_axes = model_.x_axes();
    const std::span<const gp::Axis> y_axes = model_.y_axes();
    transform_valid_ = false;
    if (x_axes.empty() || y_axes.empty()) return false;
    const gp::PlotTransformResult made = gp::PlotTransform::make(x_axes[0], y_axes[0], plot_rect_, 1.0);
    if (made.check != gp::PlotTransformCheck::valid) return false;
    transform_ = made.transform;
    transform_valid_ = true;
    return true;
}

void PlotFrame::take_change(const gp::PlotViewChange& change) {
    if (!change.view && !change.selection && !change.transient) return;
    if (change.view) {
        static_cast<void>(update_transform());
        ++statistics_.view_changes;
        PlotFrameView view{};
        view.x = x_range();
        view.y = y_range();
        view_changed_.emit(view);
    }
    invalidate(gf::Dirty::paint);
}

// -- painting --------------------------------------------------------------------------------

void PlotFrame::paint_margin(gf::Painter& painter, gf::Rect margin) {
    static_cast<void>(painter);
    static_cast<void>(margin);
}

void PlotFrame::note_marks(const gp::MarkStatistics& marks) noexcept {
    ++statistics_.mark_calls;
    statistics_.points_in += marks.points_in;
    statistics_.points_drawn += marks.points_drawn;
    frame_points_drawn_ += marks.points_drawn;
    if (marks.truncated) ++statistics_.truncated_calls;
}

void PlotFrame::paint_axis_labels(gf::Painter& painter, const gp::TickSet& ticks, bool x_axis) {
    const gf::FontSpec font = label_font();
    for (const gp::MajorTick& tick : ticks.majors()) {
        if (!tick.labelled) continue;
        const std::string_view text = tick.label.text();
        const gf::Size size = painter.measure_text_utf8(text, font);
        gf::Point origin{};
        if (x_axis) {
            origin = gf::Point{tick.device - size.width / 2.0, plot_rect_.y + plot_rect_.height + tick_length + font.size + 2.0};
        } else {
            origin = gf::Point{plot_rect_.x - tick_length - 3.0 - size.width, tick.device + font.size * 0.35};
        }
        painter.draw_text_utf8(origin, text, font, label_ink);
    }
}

void PlotFrame::on_paint(gf::Painter& painter, gf::Rect local_damage) {
    static_cast<void>(local_damage);
    const gf::Rect bounds = client_rectangle();
    painter.fill_rect(bounds, frame_background);
    ++statistics_.frames_painted;
    frame_points_drawn_ = 0U;
    const gf::FontSpec font = label_font();
    gf::FontSpec title_font = font;
    title_font.size = 12.0;
    title_font.weight = 600U;
    painter.draw_text_utf8(gf::Point{left_margin, 14.0}, title_, title_font, label_ink);
    const std::string reading = readout();
    if (!reading.empty()) {
        const gf::Size size = painter.measure_text_utf8(reading, font);
        painter.draw_text_utf8(gf::Point{bounds.width - right_margin() - size.width, 14.0}, reading, font, label_ink);
    }
    if (plot_rect_.width <= 0.0 || plot_rect_.height <= 0.0) return;
    painter.fill_rect(plot_rect_, plot_background);
    if (!update_transform()) {
        painter.stroke_rect(plot_rect_, frame_edge, 1.0);
        return;
    }
    // Ticks: generated per paint, against the painter's label metrics.
    const std::span<const gp::Axis> x_axes = model_.x_axes();
    const std::span<const gp::Axis> y_axes = model_.y_axes();
    const gp::ScaleMappingResult x_mapping =
        gp::ScaleMapping::make(gp::axis_scale(x_axes[0], plot_rect_.x, plot_rect_.x + plot_rect_.width));
    const gp::ScaleMappingResult y_mapping =
        gp::ScaleMapping::make(gp::axis_scale(y_axes[0], plot_rect_.y, plot_rect_.y + plot_rect_.height));
    gp::TickRequest request{};
    const PainterLabelMetrics x_metrics(painter, font, true);
    const PainterLabelMetrics y_metrics(painter, font, false);
    bool ticks_ok = x_mapping.check == gp::ScaleCheck::valid && y_mapping.check == gp::ScaleCheck::valid;
    if (ticks_ok) {
        const gp::TickOutcome x_outcome = gp::generate_ticks(x_mapping.mapping, request, &x_metrics, x_ticks_);
        request.minimum_spacing = 40.0;
        const gp::TickOutcome y_outcome = gp::generate_ticks(y_mapping.mapping, request, &y_metrics, y_ticks_);
        statistics_.tick_generations += 2U;
        if (x_outcome == gp::TickOutcome::truncated) ++statistics_.ticks_truncated;
        if (y_outcome == gp::TickOutcome::truncated) ++statistics_.ticks_truncated;
        ticks_ok = x_outcome != gp::TickOutcome::too_small && y_outcome != gp::TickOutcome::too_small;
    }
    gf::StrokeStyle major{};
    major.color = grid_major;
    major.width = 1.0;
    gf::StrokeStyle minor{};
    minor.color = grid_minor;
    minor.width = 1.0;
    if (ticks_ok) {
        note_marks(marks_.paint_grid(painter, x_ticks_, true, transform_, major, minor));
        note_marks(marks_.paint_grid(painter, y_ticks_, false, transform_, major, minor));
    }
    painter.save();
    painter.clip_rect(plot_rect_);
    paint_content(painter, transform_);
    const std::optional<gf::Rect> box = model_.drag_box();
    if (box.has_value()) painter.stroke_rect(*box, box_ink, 1.0);
    painter.restore();
    painter.stroke_rect(plot_rect_, frame_edge, 1.0);
    if (ticks_ok) {
        gf::StrokeStyle tick_stroke{};
        tick_stroke.color = frame_edge;
        note_marks(marks_.paint_ticks(painter, x_ticks_, gp::AxisSide::bottom, transform_, tick_length, tick_stroke));
        note_marks(marks_.paint_ticks(painter, y_ticks_, gp::AxisSide::left, transform_, tick_length, tick_stroke));
        paint_axis_labels(painter, x_ticks_, true);
        paint_axis_labels(painter, y_ticks_, false);
    }
    const double margin_x = plot_rect_.x + plot_rect_.width;
    paint_margin(painter, gf::Rect{margin_x, plot_rect_.y, std::max(0.0, bounds.width - margin_x), plot_rect_.height});
    statistics_.last_frame_points_drawn = frame_points_drawn_;
}

// -- input -----------------------------------------------------------------------------------

gp::PlotZone PlotFrame::zone_at(gf::Point local) const noexcept {
    gp::PlotZone zone{};
    const bool inside_x = local.x >= plot_rect_.x && local.x <= plot_rect_.x + plot_rect_.width;
    const bool inside_y = local.y >= plot_rect_.y && local.y <= plot_rect_.y + plot_rect_.height;
    if (inside_x && inside_y) {
        zone.kind = gp::PlotZoneKind::plot;
    } else if (inside_x && local.y > plot_rect_.y + plot_rect_.height) {
        zone.kind = gp::PlotZoneKind::x_axis;
    } else if (inside_y && local.x < plot_rect_.x) {
        zone.kind = gp::PlotZoneKind::y_axis;
    }
    return zone;
}

bool PlotFrame::press_content(gf::Point local, const gp::PlotTransform& transform) {
    static_cast<void>(local);
    static_cast<void>(transform);
    return false;
}

void PlotFrame::drag_content(gf::Point local, const gp::PlotTransform& transform) {
    static_cast<void>(local);
    static_cast<void>(transform);
}

void PlotFrame::release_content() {}

void PlotFrame::hover_content(gf::Point local, const gp::PlotTransform& transform) {
    static_cast<void>(local);
    static_cast<void>(transform);
}

std::string PlotFrame::readout() const {
    return std::string{};
}

void PlotFrame::on_mouse_down(gf::MouseEventArgs& arguments) {
    gf::SurfaceRoot* root = attached_surface_root();
    if (root != nullptr) static_cast<void>((*root).request_focus(shared_from_this()));
    const gp::PlotZone zone = zone_at(arguments.location);
    if (zone.kind == gp::PlotZoneKind::outside) return;
    if (arguments.button == gf::PointerButton::primary && zone.kind == gp::PlotZoneKind::plot && transform_valid_ &&
        press_content(arguments.location, transform_)) {
        content_gesture_ = true;
        ++statistics_.content_drags;
        set_pointer_capture(true);
        invalidate(gf::Dirty::paint);
        arguments.handled = true;
        return;
    }
    gp::PlotPointerButton button = gp::PlotPointerButton::primary;
    if (arguments.button == gf::PointerButton::secondary) {
        button = gp::PlotPointerButton::secondary;
    } else if (arguments.button == gf::PointerButton::middle) {
        button = gp::PlotPointerButton::middle;
    }
    const gp::PlotViewChange change =
        model_.pointer_pressed(arguments.location, button, modifiers_of(arguments.modifiers), zone);
    model_gesture_ = model_.gesture() != gp::PlotGesture::none;
    if (model_gesture_) set_pointer_capture(true);
    take_change(change);
    arguments.handled = true;
}

void PlotFrame::on_mouse_move(gf::MouseEventArgs& arguments) {
    if (content_gesture_) {
        drag_content(arguments.location, transform_);
        invalidate(gf::Dirty::paint);
        arguments.handled = true;
        return;
    }
    if (model_gesture_) {
        take_change(model_.pointer_moved(arguments.location));
        arguments.handled = true;
        return;
    }
    if (transform_valid_ && zone_at(arguments.location).kind == gp::PlotZoneKind::plot) {
        hover_content(arguments.location, transform_);
    }
}

void PlotFrame::on_mouse_up(gf::MouseEventArgs& arguments) {
    if (content_gesture_) {
        content_gesture_ = false;
        release_content();
        set_pointer_capture(false);
        invalidate(gf::Dirty::paint);
        arguments.handled = true;
        return;
    }
    if (model_gesture_) {
        model_gesture_ = false;
        set_pointer_capture(false);
        take_change(model_.pointer_released(arguments.location));
        arguments.handled = true;
    }
}

void PlotFrame::on_mouse_wheel(gf::MouseEventArgs& arguments) {
    const gp::PlotZone zone = zone_at(arguments.location);
    if (zone.kind == gp::PlotZoneKind::outside) return;
    double notches = arguments.wheel_delta.y;
    if (arguments.wheel_unit == gf::WheelDeltaUnit::pixels) notches /= 40.0;
    take_change(model_.wheel(arguments.location, notches, zone));
    arguments.handled = true;
}

void PlotFrame::on_mouse_double_click(gf::MouseEventArgs& arguments) {
    if (zone_at(arguments.location).kind == gp::PlotZoneKind::outside) return;
    fit();
    arguments.handled = true;
}

void PlotFrame::on_key_down(gf::KeyEventArgs& arguments) {
    gf::Control::on_key_down(arguments);
    if (arguments.handled) return;
    const std::uint32_t key = arguments.physical_key;
    const gp::PlotModifiers modifiers = modifiers_of(arguments.modifiers);
    gp::PlotViewChange change{};
    bool consumed = true;
    if (key == gf::PhysicalKey::home) {
        change = model_.fit(data_bounds());
    } else if (key == gf::PhysicalKey::equal || key == gf::PhysicalKey::keypad_plus) {
        change = model_.key(gp::PlotKey::zoom_in, modifiers, data_bounds());
    } else if (key == gf::PhysicalKey::minus || key == gf::PhysicalKey::keypad_minus) {
        change = model_.key(gp::PlotKey::zoom_out, modifiers, data_bounds());
    } else if (key == gf::PhysicalKey::left) {
        change = model_.key(gp::PlotKey::left, modifiers, data_bounds());
    } else if (key == gf::PhysicalKey::right) {
        change = model_.key(gp::PlotKey::right, modifiers, data_bounds());
    } else if (key == gf::PhysicalKey::up) {
        change = model_.key(gp::PlotKey::up, modifiers, data_bounds());
    } else if (key == gf::PhysicalKey::down) {
        change = model_.key(gp::PlotKey::down, modifiers, data_bounds());
    } else {
        consumed = false;
    }
    if (!consumed) return;
    take_change(change);
    arguments.handled = true;
}

} // namespace implot_class
