// The drag model through a real PlotTransform: hit testing picks the
// nearest handle within the radius (points before lines), moves land at
// the data position under the pointer, x order is kept, and stale or
// non-finite moves change nothing. Headless.

#include "../src/drag_model.hpp"
#include "test_support.hpp"

#include <gui_forms/plots/plot_transform/plot_transform.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <exception>

namespace gf = gui_forms;
namespace gp = gui_forms::plots;
using implot_class::DataPoint;
using implot_class::DragModel;
using implot_class::DragTarget;
using implot_class::DragTargetKind;
using implot_class_tests::require;

namespace {

gp::Axis make_axis(gp::AxisSide side, double minimum, double maximum) {
    gp::Axis axis{};
    axis.kind = gp::ScaleKind::linear;
    axis.range = gp::PlotRange{minimum, maximum};
    axis.side = side;
    return axis;
}

// x 0..10 over 400 device units, y 0..10 over 300 (y up).
gp::PlotTransform make_transform() {
    const gp::Axis x = make_axis(gp::AxisSide::bottom, 0.0, 10.0);
    const gp::Axis y = make_axis(gp::AxisSide::left, 0.0, 10.0);
    const gp::PlotTransformResult made = gp::PlotTransform::make(x, y, gf::Rect{50.0, 20.0, 400.0, 300.0}, 1.0);
    require(made.check == gp::PlotTransformCheck::valid, "the transform is valid");
    return made.transform;
}

DragModel make_model() {
    DragModel model{};
    const std::array<DataPoint, 4> points{{{1.0, 2.0}, {3.0, 5.0}, {6.0, 4.0}, {9.0, 8.0}}};
    model.set_points(points);
    model.set_vertical_line(7.5);
    model.set_horizontal_line(1.0);
    return model;
}

void test_hit() {
    const gp::PlotTransform transform = make_transform();
    const DragModel model = make_model();
    const gf::Point second = transform.to_device(3.0, 5.0);
    const DragTarget hit = model.hit(gf::Point{second.x + 3.0, second.y - 2.0}, transform, 8.0);
    require(hit.kind == DragTargetKind::point && hit.index == 1U, "a press near a point takes it");
    const gf::Point line = transform.to_device(7.5, 9.0);
    const DragTarget vertical = model.hit(gf::Point{line.x + 4.0, line.y}, transform, 8.0);
    require(vertical.kind == DragTargetKind::vertical_line, "a press near the vertical line takes it");
    const gf::Point floor = transform.to_device(4.5, 1.0);
    const DragTarget horizontal = model.hit(floor, transform, 8.0);
    require(horizontal.kind == DragTargetKind::horizontal_line, "a press on the horizontal line takes it");
    const gf::Point empty = transform.to_device(4.5, 7.0);
    require(model.hit(empty, transform, 8.0).kind == DragTargetKind::none, "empty space takes nothing");
}

void test_move() {
    const gp::PlotTransform transform = make_transform();
    DragModel model = make_model();
    const DragTarget point{DragTargetKind::point, 1U};
    require(model.move(point, transform.to_device(3.5, 7.0), transform), "a point moves");
    require(std::fabs(model.points()[1].x - 3.5) < 1e-9 && std::fabs(model.points()[1].y - 7.0) < 1e-9,
            "the point lands under the pointer");
    // Past its right neighbour (x 6): kept in x order.
    require(model.move(point, transform.to_device(8.0, 7.0), transform), "the move applies");
    require(model.points()[1].x <= model.points()[2].x, "x order is kept");
    const DragTarget line{DragTargetKind::vertical_line, 0U};
    require(model.move(line, transform.to_device(2.0, 0.0), transform), "the vertical line moves");
    require(std::fabs(model.vertical_line() - 2.0) < 1e-9, "the line lands under the pointer");
    const std::uint64_t moves = model.moves();
    require(!model.move(DragTarget{DragTargetKind::point, 99U}, gf::Point{60.0, 60.0}, transform),
            "a stale index changes nothing");
    require(!model.move(point, gf::Point{NAN, 60.0}, transform), "a non-finite position changes nothing");
    require(!model.move(DragTarget{}, gf::Point{60.0, 60.0}, transform), "none changes nothing");
    require(model.moves() == moves, "refused moves are not counted");
}

void test_extent_and_clamp() {
    DragModel model = make_model();
    const gp::PlotRange x = model.x_extent();
    require(x.minimum == 1.0 && x.maximum == 9.0, "the x extent spans points and the line");
    model.clamp_to(gp::PlotRange{2.0, 8.0}, gp::PlotRange{3.0, 6.0});
    for (const DataPoint& point : model.points()) {
        require(point.x >= 2.0 && point.x <= 8.0 && point.y >= 3.0 && point.y <= 6.0, "every point is clamped");
    }
    require(model.horizontal_line() == 3.0, "the horizontal line is clamped");
}

} // namespace

int main() {
    try {
        test_hit();
        test_move();
        test_extent_and_clamp();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "drag_model_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("drag_model_tests: passed");
    return 0;
}
