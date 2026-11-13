#include "drag_plot.hpp"

#include <gui_forms/types/paint_marks/paint_marks.hpp>
#include <gui_forms/types/painter/painter.hpp>

#include <cstdio>
#include <span>
#include <utility>

namespace implot_class {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

constexpr gf::Color curve_ink = gf::Color::rgba(200, 80, 40);
constexpr gf::Color handle_fill = gf::Color::rgba(255, 255, 255);
constexpr gf::Color handle_hot = gf::Color::rgba(255, 210, 120);
constexpr gf::Color line_ink = gf::Color::rgba(40, 110, 170);

} // namespace

DragPlot::DragPlot(gf::StableId stable_id) : PlotFrame(std::move(stable_id)) {
    device_points_.reserve(64U);
}

void DragPlot::model_changed() {
    invalidate(gf::Dirty::paint);
}

PlotFrameView DragPlot::data_extent() const {
    PlotFrameView view{};
    view.x = model_.x_extent();
    view.y = model_.y_extent();
    return view;
}

void DragPlot::paint_content(gf::Painter& painter, const gp::PlotTransform& transform) {
    const std::span<const DataPoint> points = model_.points();
    device_points_.clear();
    for (const DataPoint& point : points) device_points_.push_back(transform.to_device(point.x, point.y));
    gf::StrokeStyle curve{};
    curve.color = curve_ink;
    curve.width = 2.0;
    curve.join = gf::StrokeJoin::round;
    if (device_points_.size() >= 2U) {
        painter.stroke_polyline(device_points_, curve);
        gp::MarkStatistics statistics{};
        statistics.points_in = device_points_.size();
        statistics.points_drawn = device_points_.size();
        note_marks(statistics);
    }
    gf::StrokeStyle lines{};
    lines.color = line_ink;
    lines.width = 1.0;
    lines.dashes[0] = 6.0;
    lines.dashes[1] = 4.0;
    lines.dash_count = 2U;
    const double vertical[1]{model_.vertical_line()};
    const double horizontal[1]{model_.horizontal_line()};
    note_marks(marks().paint_infinite_lines(painter, vertical, gp::InfiniteLineOrientation::vertical, transform, lines));
    note_marks(marks().paint_infinite_lines(painter, horizontal, gp::InfiniteLineOrientation::horizontal, transform, lines));
    gf::MarkerStyle marker{};
    marker.shape = gf::MarkerShape::circle;
    marker.size = 9.0;
    marker.fill = handle_fill;
    marker.edge = curve_ink;
    marker.edge_width = 1.5;
    painter.draw_markers(device_points_, marker);
    // The hot handle drawn again over the rest: one more batch of one.
    const DragTarget hot = dragging_.kind != DragTargetKind::none ? dragging_ : hovered_;
    if (hot.kind == DragTargetKind::point && hot.index < device_points_.size()) {
        marker.fill = handle_hot;
        marker.size = 12.0;
        painter.draw_markers(std::span<const gf::Point>(&device_points_[hot.index], 1U), marker);
    }
}

bool DragPlot::press_content(gf::Point local, const gp::PlotTransform& transform) {
    const DragTarget target = model_.hit(local, transform, DragModel::default_radius);
    if (target.kind == DragTargetKind::none) return false;
    dragging_ = target;
    return true;
}

void DragPlot::drag_content(gf::Point local, const gp::PlotTransform& transform) {
    if (!model_.move(dragging_, local, transform)) return;
    handles_moved_.emit();
}

void DragPlot::release_content() {
    dragging_ = DragTarget{};
}

void DragPlot::hover_content(gf::Point local, const gp::PlotTransform& transform) {
    const DragTarget target = model_.hit(local, transform, DragModel::default_radius);
    if (target == hovered_) return;
    hovered_ = target;
    // No public cursor-per-region request for a plain control beyond the
    // `cursor` property, which is the whole control's (GAPS.md P6).
    invalidate(gf::Dirty::paint);
}

std::string DragPlot::readout() const {
    char text[96]{};
    const int written = std::snprintf(text, sizeof(text), "x line %.3f, y line %.3f, %zu points",
                                      model_.vertical_line(), model_.horizontal_line(), model_.points().size());
    std::string result{};
    if (written > 0) result.assign(text, static_cast<std::size_t>(written));
    return result;
}

} // namespace implot_class
