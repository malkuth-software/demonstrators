// The canvas geometry without a window: the view transform, zoom about a
// point, levels of detail with hysteresis, soft link curves, distances and
// crossings, and the spatial index against brute force.

#include "../src/canvas_geometry.hpp"
#include "../src/generator.hpp"
#include "../src/graph_document.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <span>
#include <vector>

namespace gf = gui_forms;
using node_graph_tests::require;

namespace {

bool near(double left, double right) noexcept {
    return std::fabs(left - right) <= 1e-9 * std::max(1.0, std::fabs(left) + std::fabs(right));
}

void transform_round_trips() {
    node_graph::ViewTransform view{};
    view.origin = gf::WorldPoint{-100.0, 50.0};
    view.zoom = 2.5;
    const gf::WorldPoint world{37.25, -12.5};
    const gf::Point local = view.to_view(world);
    const gf::WorldPoint back = view.to_world(local);
    require(near(back.x, world.x) && near(back.y, world.y), "world to view to world returns the point");
    const gf::Point anchor{300.0, 200.0};
    const gf::WorldPoint under = view.to_world(anchor);
    view.zoom_about(anchor, 1.7);
    const gf::WorldPoint after = view.to_world(anchor);
    require(near(under.x, after.x) && near(under.y, after.y), "zooming keeps the point under the anchor");
    for (std::size_t step = 0U; step < 200U; ++step) view.zoom_about(anchor, 2.0);
    require(view.zoom == node_graph::maximum_zoom, "zoom is held to its maximum");
    for (std::size_t step = 0U; step < 400U; ++step) view.zoom_about(anchor, 0.5);
    require(view.zoom == node_graph::minimum_zoom, "zoom is held to its minimum");
    node_graph::ViewTransform framed{};
    framed.frame(gf::WorldRect{1000.0, 1000.0, 400.0, 200.0}, gf::Size{800.0, 600.0}, 20.0);
    const gf::Rect shown = framed.rect_to_view(gf::WorldRect{1000.0, 1000.0, 400.0, 200.0});
    require(shown.x >= 19.0 && shown.y >= 19.0 && shown.x + shown.width <= 781.0 && shown.y + shown.height <= 581.0,
            "frame fits the area inside the margin");
}

void levels_of_detail_have_hysteresis() {
    node_graph::LodSelector selector{};
    require(selector.update(1.0) == node_graph::LevelOfDetail::full, "zoom 1 is full detail");
    require(selector.update(0.58) == node_graph::LevelOfDetail::full, "just under the threshold stays full");
    require(selector.update(0.5) == node_graph::LevelOfDetail::compact, "well under it drops to compact");
    require(selector.update(0.62) == node_graph::LevelOfDetail::compact, "just over it stays compact");
    require(selector.update(0.7) == node_graph::LevelOfDetail::full, "well over it returns to full");
    require(selector.update(0.05) == node_graph::LevelOfDetail::overview, "far out is the overview");
    require(selector.transitions() == 3U, "each change is counted once");
}

void curves_and_crossings() {
    const gf::Point from{0.0, 0.0};
    const gf::Point to{200.0, 100.0};
    const node_graph::CubicCurve curve = node_graph::soft_link_curve(from, to, 1.0);
    require(curve.p1.y == from.y && curve.p2.y == to.y, "tangents leave and enter horizontally");
    std::array<gf::Point, 17> points{};
    const std::size_t count = node_graph::flatten_curve(curve, 16U, std::span<gf::Point>(points.data(), points.size()));
    require(count == 17U, "segments + 1 points");
    require(points[0].x == from.x && points[0].y == from.y && near(points[16].x, to.x) && near(points[16].y, to.y),
            "the flattened curve starts and ends at the anchors");
    std::array<gf::Point, 4> short_out{};
    require(node_graph::flatten_curve(curve, 16U, std::span<gf::Point>(short_out.data(), short_out.size())) == 4U,
            "flattening stops at the storage's end");
    const gf::Rect box = node_graph::curve_bounds(curve);
    for (const gf::Point point : points) {
        require(point.x >= box.x - 1e-9 && point.x <= box.x + box.width + 1e-9, "the curve lies in its bounds");
    }
    const std::span<const gf::Point> path(points.data(), count);
    require(node_graph::distance_to_polyline(path, points[8]) < 1e-9, "a point on the path is at distance 0");
    require(node_graph::polyline_crosses(path, gf::Point{100.0, -50.0}, gf::Point{100.0, 150.0}),
            "a vertical cut through the middle crosses");
    require(!node_graph::polyline_crosses(path, gf::Point{-50.0, 120.0}, gf::Point{-10.0, 120.0}),
            "a cut beside it does not");
    require(node_graph::segments_cross(gf::Point{0.0, 0.0}, gf::Point{10.0, 10.0}, gf::Point{0.0, 10.0},
                                       gf::Point{10.0, 0.0}),
            "an X crosses");
    require(node_graph::segments_cross(gf::Point{0.0, 0.0}, gf::Point{10.0, 0.0}, gf::Point{10.0, 0.0},
                                       gf::Point{10.0, 5.0}),
            "touching counts");
    const node_graph::CubicCurve back = node_graph::soft_link_curve(gf::Point{200.0, 0.0}, gf::Point{0.0, 100.0}, 1.0);
    require(back.p1.x > 200.0 && back.p2.x < 0.0, "a back-link loops out past both ends");
}

void spatial_index_matches_brute_force() {
    node_graph::GraphDocument document{};
    node_graph::GeneratorOptions options{};
    options.nodes = 3000U;
    options.seed = 21U;
    static_cast<void>(node_graph::generate_stress_graph(document, options));
    const gf::NodeGraph& graph = document.graph();
    node_graph::SpatialIndex index{};
    index.build(graph);
    require(index.built_revision() == graph.revision(), "the index records the revision it was built at");
    std::vector<std::uint32_t> found{};
    std::vector<std::uint32_t> expected{};
    node_graph::SeededRandom random(5U);
    const std::span<const gf::NodeRecord> slots = graph.node_slots();
    for (std::size_t query = 0U; query < 200U; ++query) {
        const gf::WorldRect area{random.unit() * 20000.0 - 2000.0, random.unit() * 20000.0 - 2000.0,
                                 random.unit() * 4000.0, random.unit() * 3000.0};
        const bool containment = (query % 3U) == 0U;
        index.query(graph, area, containment, found);
        expected.clear();
        for (std::uint32_t slot = 0U; slot < slots.size(); ++slot) {
            if (!slots[slot].live) continue;
            const bool hit = containment ? node_graph::rect_contains_rect(area, slots[slot].bounds)
                                         : node_graph::rects_intersect(area, slots[slot].bounds);
            if (hit) expected.push_back(slot);
        }
        std::sort(found.begin(), found.end());
        require(found == expected, "every query returns exactly the brute-force set, each once");
    }
    require(index.statistics().queries == 200U, "queries are counted");
}

} // namespace

int main() {
    try {
        transform_round_trips();
        levels_of_detail_have_hysteresis();
        curves_and_crossings();
        spatial_index_matches_brute_force();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "geometry_tests: %s\n", failure.what());
        return 1;
    }
    std::printf("geometry_tests: ok\n");
    return 0;
}
