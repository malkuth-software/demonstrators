#pragma once

// The canvas's geometry, kept apart from the control so that tests can
// check it without a window (NODE_GRAPH_SPEC sections 3 to 5):
// - ViewTransform: world (float64 graph units) to view (logical units),
//   pan and zoom about a point, zoom held to 1/64..8;
// - LevelOfDetail with hysteresis (study 2.6);
// - soft link curves: a cubic with horizontal tangents whose length is
//   clamped by distance, with a vertical term for back-links (spec 4.2),
//   flattened into caller storage;
// - distances and crossings for link hit testing and the knife;
// - SpatialIndex: a uniform grid over node bounds, the app's own because
//   GUI.Forms publishes none (GAPS.md G8), with its visits counted.

#include <gui_forms/controls/node_graph.hpp>
#include <gui_forms/types/geometry/geometry.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace node_graph {

inline constexpr double minimum_zoom = 1.0 / 64.0;
inline constexpr double maximum_zoom = 8.0;

// view = (world - origin) * zoom, where origin is the world point at the
// view's top-left. Doubles throughout; the painter receives view units.
struct ViewTransform final {
    gui_forms::WorldPoint origin{};
    double zoom{1.0};

    [[nodiscard]] gui_forms::Point to_view(gui_forms::WorldPoint world) const noexcept;
    [[nodiscard]] gui_forms::WorldPoint to_world(gui_forms::Point view) const noexcept;
    [[nodiscard]] gui_forms::Rect rect_to_view(const gui_forms::WorldRect& world) const noexcept;
    [[nodiscard]] gui_forms::WorldRect rect_to_world(const gui_forms::Rect& view) const noexcept;
    // Zooms by `factor` keeping the world point under `anchor` fixed; the
    // zoom is held to [minimum_zoom, maximum_zoom].
    void zoom_about(gui_forms::Point anchor, double factor) noexcept;
    // Moves the content by `delta` view units (the content follows the
    // pointer: a positive x moves it right).
    void pan_by(gui_forms::Point delta) noexcept;
    // Frames `world` in a view of `size`, with `margin` view units around.
    void frame(const gui_forms::WorldRect& world, gui_forms::Size size, double margin) noexcept;
};

enum class LevelOfDetail : std::uint8_t { overview, silhouette, compact, full };

// The zooms at which the level changes, and the hysteresis factor: a level
// is entered upward only above threshold * hysteresis and left downward
// only below threshold / hysteresis.
struct LodThresholds final {
    double silhouette{0.1};
    double compact{0.3};
    double full{0.6};
    double hysteresis{1.1};
};

class LodSelector final {
public:
    // The level for `zoom`, given the current one; counts transitions.
    LevelOfDetail update(double zoom) noexcept;
    [[nodiscard]] LevelOfDetail level() const noexcept { return level_; }
    [[nodiscard]] std::uint64_t transitions() const noexcept { return transitions_; }
    void set_thresholds(const LodThresholds& thresholds) noexcept { thresholds_ = thresholds; }

private:
    LodThresholds thresholds_{};
    LevelOfDetail level_{LevelOfDetail::full};
    std::uint64_t transitions_{};
};

[[nodiscard]] const char* lod_name(LevelOfDetail level) noexcept;

// A cubic Bezier's four control points.
struct CubicCurve final {
    gui_forms::Point p0{};
    gui_forms::Point p1{};
    gui_forms::Point p2{};
    gui_forms::Point p3{};
};

// The soft link from an output anchor (leaving along +x) to an input anchor
// (entering along +x), in view units. The tangent length is half the
// horizontal distance, held to [24, 160] view units scaled by `zoom`; a
// back-link (the input left of the output) adds a quarter of the vertical
// distance so that the loop clears both nodes.
[[nodiscard]] CubicCurve soft_link_curve(gui_forms::Point from, gui_forms::Point to, double zoom) noexcept;

// Writes segments + 1 points along the curve into `out` and returns how
// many it wrote (fewer when `out` is shorter: it stops at out.size()).
std::size_t flatten_curve(const CubicCurve& curve, std::size_t segments, std::span<gui_forms::Point> out) noexcept;

// The curve's control-point box (contains the curve).
[[nodiscard]] gui_forms::Rect curve_bounds(const CubicCurve& curve) noexcept;

// The distance from `point` to the polyline through `points` (at least one
// point; infinity for none).
[[nodiscard]] double distance_to_polyline(std::span<const gui_forms::Point> points, gui_forms::Point point) noexcept;

// Whether segment a-b crosses segment c-d (touching counts).
[[nodiscard]] bool segments_cross(gui_forms::Point a, gui_forms::Point b, gui_forms::Point c, gui_forms::Point d) noexcept;
// Whether the polyline crosses segment c-d.
[[nodiscard]] bool polyline_crosses(std::span<const gui_forms::Point> points, gui_forms::Point c, gui_forms::Point d) noexcept;

[[nodiscard]] bool rect_contains_rect(const gui_forms::WorldRect& outer, const gui_forms::WorldRect& inner) noexcept;
[[nodiscard]] bool rects_intersect(const gui_forms::WorldRect& left, const gui_forms::WorldRect& right) noexcept;

struct SpatialIndexStatistics final {
    std::uint64_t builds{};
    std::uint64_t queries{};
    // Cells and node entries visited by queries (the hit-test budget's
    // "one bucket plus its neighbours" is read from these).
    std::uint64_t cells_visited{};
    std::uint64_t entries_visited{};
    std::size_t columns{};
    std::size_t rows{};
    double cell_size{};
    std::size_t entries{};
    std::size_t oversize{};
};

// A uniform grid of node slots by their bounds. Rebuilt from the graph
// (build: O(N) plus the cells the nodes cover); queries return node slot
// indices, each once, in no particular order. Storage is reused across
// builds and queries. The cell count is capped at maximum_index_cells (the
// cell grows instead), and a node covering more than maximum_cells_per_node
// cells is kept in an oversize list every query visits.
class SpatialIndex final {
public:
    static constexpr std::size_t maximum_index_cells = 1U << 20U;
    static constexpr std::size_t maximum_cells_per_node = 64U;
    static constexpr double default_cell_size = 512.0;

    void build(const gui_forms::NodeGraph& graph);
    // Slots of live nodes whose bounds intersect `area` (or, with
    // `containment`, lie wholly inside it), appended to `out` after it is
    // cleared.
    void query(const gui_forms::NodeGraph& graph, const gui_forms::WorldRect& area, bool containment,
               std::vector<std::uint32_t>& out);
    [[nodiscard]] std::uint64_t built_revision() const noexcept { return built_revision_; }
    [[nodiscard]] const SpatialIndexStatistics& statistics() const noexcept { return statistics_; }

private:
    [[nodiscard]] std::size_t column_of(double x) const noexcept;
    [[nodiscard]] std::size_t row_of(double y) const noexcept;

    double left_{};
    double top_{};
    double cell_{default_cell_size};
    std::size_t columns_{};
    std::size_t rows_{};
    // Compressed rows: the slots in cell c are entries_[cell_start_[c] ..
    // cell_start_[c + 1]).
    std::vector<std::uint32_t> cell_start_{};
    std::vector<std::uint32_t> entries_{};
    std::vector<std::uint32_t> oversize_{};
    // Per slot: the stamp of the last query that returned it (each slot once).
    std::vector<std::uint32_t> marks_{};
    std::vector<std::uint32_t> fill_{};
    std::uint32_t stamp_{};
    std::uint64_t built_revision_{};
    SpatialIndexStatistics statistics_{};
};

} // namespace node_graph
