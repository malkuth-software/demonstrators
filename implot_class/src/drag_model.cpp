#include "drag_model.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace implot_class {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

void DragModel::set_points(std::span<const DataPoint> points) {
    std::vector<DataPoint> copy(points.begin(), points.end());
    points_ = std::move(copy);
}

DragTarget DragModel::hit(gf::Point device, const gp::PlotTransform& transform, double radius) const noexcept {
    DragTarget best{};
    double best_distance = radius;
    for (std::size_t index = 0U; index < points_.size(); ++index) {
        const gf::Point at = transform.to_device(points_[index].x, points_[index].y);
        if (!std::isfinite(at.x) || !std::isfinite(at.y)) continue;
        const double distance = std::hypot(at.x - device.x, at.y - device.y);
        if (distance > best_distance) continue;
        best_distance = distance;
        best.kind = DragTargetKind::point;
        best.index = index;
    }
    if (best.kind != DragTargetKind::none) return best;
    const gf::Point line = transform.to_device(vertical_x_, horizontal_y_);
    if (std::isfinite(line.x) && std::fabs(line.x - device.x) <= radius) {
        best.kind = DragTargetKind::vertical_line;
        return best;
    }
    if (std::isfinite(line.y) && std::fabs(line.y - device.y) <= radius) {
        best.kind = DragTargetKind::horizontal_line;
        return best;
    }
    return best;
}

bool DragModel::move(DragTarget target, gf::Point device, const gp::PlotTransform& transform) noexcept {
    const double x = transform.x_from_device(device.x);
    const double y = transform.y_from_device(device.y);
    switch (target.kind) {
    case DragTargetKind::point: {
        if (target.index >= points_.size() || !std::isfinite(x) || !std::isfinite(y)) return false;
        double new_x = x;
        if (keep_x_order_) {
            if (target.index > 0U) new_x = std::max(new_x, points_[target.index - 1U].x);
            if (target.index + 1U < points_.size()) new_x = std::min(new_x, points_[target.index + 1U].x);
        }
        points_[target.index].x = new_x;
        points_[target.index].y = y;
        break;
    }
    case DragTargetKind::vertical_line:
        if (!std::isfinite(x)) return false;
        vertical_x_ = x;
        break;
    case DragTargetKind::horizontal_line:
        if (!std::isfinite(y)) return false;
        horizontal_y_ = y;
        break;
    case DragTargetKind::none:
    default:
        return false;
    }
    ++moves_;
    return true;
}

void DragModel::clamp_to(gp::PlotRange x, gp::PlotRange y) noexcept {
    if (!x.valid() || !y.valid()) return;
    for (DataPoint& point : points_) {
        point.x = std::clamp(point.x, x.minimum, x.maximum);
        point.y = std::clamp(point.y, y.minimum, y.maximum);
    }
    vertical_x_ = std::clamp(vertical_x_, x.minimum, x.maximum);
    horizontal_y_ = std::clamp(horizontal_y_, y.minimum, y.maximum);
}

gp::PlotRange DragModel::x_extent() const noexcept {
    double minimum = vertical_x_;
    double maximum = vertical_x_;
    for (const DataPoint& point : points_) {
        minimum = std::min(minimum, point.x);
        maximum = std::max(maximum, point.x);
    }
    if (!(minimum < maximum)) maximum = minimum + 1.0;
    const gp::PlotRange range{minimum, maximum};
    return range;
}

gp::PlotRange DragModel::y_extent() const noexcept {
    double minimum = horizontal_y_;
    double maximum = horizontal_y_;
    for (const DataPoint& point : points_) {
        minimum = std::min(minimum, point.y);
        maximum = std::max(maximum, point.y);
    }
    if (!(minimum < maximum)) maximum = minimum + 1.0;
    const gp::PlotRange range{minimum, maximum};
    return range;
}

} // namespace implot_class
