#pragma once

// DragModel: draggable plot handles in data units (ImPlot's drag points
// and drag lines, behaviour only): a polyline through movable points, one
// vertical line and one horizontal line. PlotView declares handles for
// later (IMGUI_IMPLOT_STUDY P10) but has none at cp29 (GAPS.md P3), so the
// model is the app's: hit testing against device positions through a
// plots::PlotTransform, and moves written back in data units. Pure: no
// control, no painter, so it is tested headless.
//
// Hit order: points first (the nearest within the radius), then the
// vertical line, then the horizontal line. A move of a point keeps its x
// between its neighbours' (the polyline stays a function of x) when
// `keep_x_order` is set.

#include <gui_forms/plots/plot_transform/plot_transform.hpp>
#include <gui_forms/types.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace implot_class {

enum class DragTargetKind : std::uint8_t { none, point, vertical_line, horizontal_line };

struct DragTarget final {
    DragTargetKind kind{DragTargetKind::none};
    // The point's index for DragTargetKind::point.
    std::size_t index{};

    friend bool operator==(const DragTarget& left, const DragTarget& right) noexcept {
        return left.kind == right.kind && left.index == right.index;
    }
};

struct DataPoint final {
    double x{};
    double y{};
};

class DragModel final {
public:
    static constexpr double default_radius = 8.0;

    // Replaces the points (copied).
    void set_points(std::span<const DataPoint> points);
    void set_vertical_line(double x) noexcept { vertical_x_ = x; }
    void set_horizontal_line(double y) noexcept { horizontal_y_ = y; }
    void set_keep_x_order(bool keep) noexcept { keep_x_order_ = keep; }

    [[nodiscard]] std::span<const DataPoint> points() const& noexcept { return points_; }
    std::span<const DataPoint> points() const&& = delete;
    [[nodiscard]] double vertical_line() const noexcept { return vertical_x_; }
    [[nodiscard]] double horizontal_line() const noexcept { return horizontal_y_; }

    // What lies within `radius` logical units of `device`.
    [[nodiscard]] DragTarget hit(gui_forms::Point device, const gui_forms::plots::PlotTransform& transform,
                                 double radius) const noexcept;
    // Moves `target` to the data position at `device`. False (nothing
    // changed) for none, a stale index or a non-finite position.
    bool move(DragTarget target, gui_forms::Point device, const gui_forms::plots::PlotTransform& transform) noexcept;
    // Moves every handle into view: each coordinate clamped to `x` and `y`.
    void clamp_to(gui_forms::plots::PlotRange x, gui_forms::plots::PlotRange y) noexcept;

    // The data extent of every handle (points and lines).
    [[nodiscard]] gui_forms::plots::PlotRange x_extent() const noexcept;
    [[nodiscard]] gui_forms::plots::PlotRange y_extent() const noexcept;
    [[nodiscard]] std::uint64_t moves() const noexcept { return moves_; }

private:
    std::vector<DataPoint> points_{};
    double vertical_x_{};
    double horizontal_y_{};
    bool keep_x_order_{true};
    std::uint64_t moves_{};
};

} // namespace implot_class
