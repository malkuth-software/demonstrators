#include "canvas_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace node_graph {

namespace gf = gui_forms;

namespace {

double clamp_zoom(double zoom) noexcept {
    if (!(zoom > minimum_zoom)) return minimum_zoom;
    if (zoom > maximum_zoom) return maximum_zoom;
    return zoom;
}

double distance_to_segment(gf::Point a, gf::Point b, gf::Point p) noexcept {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length_squared = dx * dx + dy * dy;
    double t = 0.0;
    if (length_squared > 0.0) t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / length_squared;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const double nearest_x = a.x + dx * t;
    const double nearest_y = a.y + dy * t;
    const double distance = std::hypot(p.x - nearest_x, p.y - nearest_y);
    return distance;
}

// The sign of the turn a -> b -> c: positive counter-clockwise (y down:
// clockwise on screen), zero when collinear.
double turn(gf::Point a, gf::Point b, gf::Point c) noexcept {
    const double value = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    return value;
}

bool within(double value, double low, double high) noexcept {
    const double lower = std::min(low, high);
    const double upper = std::max(low, high);
    const bool inside = value >= lower && value <= upper;
    return inside;
}

} // namespace

// -- ViewTransform -----------------------------------------------------------------------

gf::Point ViewTransform::to_view(gf::WorldPoint world) const noexcept {
    const gf::Point view{(world.x - origin.x) * zoom, (world.y - origin.y) * zoom};
    return view;
}

gf::WorldPoint ViewTransform::to_world(gf::Point view) const noexcept {
    const gf::WorldPoint world{origin.x + view.x / zoom, origin.y + view.y / zoom};
    return world;
}

gf::Rect ViewTransform::rect_to_view(const gf::WorldRect& world) const noexcept {
    const gf::Point top_left = to_view(gf::WorldPoint{world.x, world.y});
    const gf::Rect view{top_left.x, top_left.y, world.width * zoom, world.height * zoom};
    return view;
}

gf::WorldRect ViewTransform::rect_to_world(const gf::Rect& view) const noexcept {
    const gf::WorldPoint top_left = to_world(gf::Point{view.x, view.y});
    const gf::WorldRect world{top_left.x, top_left.y, view.width / zoom, view.height / zoom};
    return world;
}

void ViewTransform::zoom_about(gf::Point anchor, double factor) noexcept {
    if (!std::isfinite(factor) || factor <= 0.0) return;
    const gf::WorldPoint fixed = to_world(anchor);
    zoom = clamp_zoom(zoom * factor);
    origin.x = fixed.x - anchor.x / zoom;
    origin.y = fixed.y - anchor.y / zoom;
}

void ViewTransform::pan_by(gf::Point delta) noexcept {
    origin.x -= delta.x / zoom;
    origin.y -= delta.y / zoom;
}

void ViewTransform::frame(const gf::WorldRect& world, gf::Size size, double margin) noexcept {
    const double usable_width = size.width - 2.0 * margin;
    const double usable_height = size.height - 2.0 * margin;
    if (usable_width <= 0.0 || usable_height <= 0.0 || world.width <= 0.0 || world.height <= 0.0) return;
    const double fit = std::min(usable_width / world.width, usable_height / world.height);
    zoom = clamp_zoom(fit);
    const double centre_x = world.x + world.width / 2.0;
    const double centre_y = world.y + world.height / 2.0;
    origin.x = centre_x - size.width / (2.0 * zoom);
    origin.y = centre_y - size.height / (2.0 * zoom);
}

// -- level of detail ------------------------------------------------------------------------

LevelOfDetail LodSelector::update(double zoom) noexcept {
    const double h = thresholds_.hysteresis;
    // The level the zoom gives without hysteresis, then hysteresis applied
    // around the current level's two thresholds.
    LevelOfDetail next = level_;
    const double up_compact = thresholds_.compact * h;
    const double up_full = thresholds_.full * h;
    const double up_silhouette = thresholds_.silhouette * h;
    switch (level_) {
    case LevelOfDetail::overview:
        if (zoom >= up_silhouette) next = LevelOfDetail::silhouette;
        if (zoom >= up_compact) next = LevelOfDetail::compact;
        if (zoom >= up_full) next = LevelOfDetail::full;
        break;
    case LevelOfDetail::silhouette:
        if (zoom < thresholds_.silhouette / h) next = LevelOfDetail::overview;
        if (zoom >= up_compact) next = LevelOfDetail::compact;
        if (zoom >= up_full) next = LevelOfDetail::full;
        break;
    case LevelOfDetail::compact:
        if (zoom < thresholds_.compact / h) next = LevelOfDetail::silhouette;
        if (zoom < thresholds_.silhouette / h) next = LevelOfDetail::overview;
        if (zoom >= up_full) next = LevelOfDetail::full;
        break;
    case LevelOfDetail::full:
        if (zoom < thresholds_.full / h) next = LevelOfDetail::compact;
        if (zoom < thresholds_.compact / h) next = LevelOfDetail::silhouette;
        if (zoom < thresholds_.silhouette / h) next = LevelOfDetail::overview;
        break;
    default:
        // An unknown level: recompute from the plain thresholds.
        next = LevelOfDetail::full;
        break;
    }
    if (next != level_) {
        level_ = next;
        ++transitions_;
    }
    return level_;
}

const char* lod_name(LevelOfDetail level) noexcept {
    switch (level) {
    case LevelOfDetail::overview: return "overview";
    case LevelOfDetail::silhouette: return "silhouette";
    case LevelOfDetail::compact: return "compact";
    case LevelOfDetail::full: return "full";
    }
    return "unknown";
}

// -- curves --------------------------------------------------------------------------------

CubicCurve soft_link_curve(gf::Point from, gf::Point to, double zoom) noexcept {
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    double tangent = std::fabs(dx) * 0.5;
    const double shortest = 24.0 * zoom;
    const double longest = 160.0 * zoom;
    if (tangent < shortest) tangent = shortest;
    if (tangent > longest) tangent = longest;
    if (dx < 0.0) tangent += std::fabs(dy) * 0.25;
    const CubicCurve curve{from, gf::Point{from.x + tangent, from.y}, gf::Point{to.x - tangent, to.y}, to};
    return curve;
}

std::size_t flatten_curve(const CubicCurve& curve, std::size_t segments, std::span<gf::Point> out) noexcept {
    if (segments == 0U) segments = 1U;
    std::size_t count = segments + 1U;
    if (count > out.size()) count = out.size();
    const double step = 1.0 / static_cast<double>(segments);
    for (std::size_t index = 0U; index < count; ++index) {
        const double t = static_cast<double>(index) * step;
        const double u = 1.0 - t;
        const double b0 = u * u * u;
        const double b1 = 3.0 * u * u * t;
        const double b2 = 3.0 * u * t * t;
        const double b3 = t * t * t;
        out[index].x = b0 * curve.p0.x + b1 * curve.p1.x + b2 * curve.p2.x + b3 * curve.p3.x;
        out[index].y = b0 * curve.p0.y + b1 * curve.p1.y + b2 * curve.p2.y + b3 * curve.p3.y;
    }
    return count;
}

gf::Rect curve_bounds(const CubicCurve& curve) noexcept {
    const double left = std::min(std::min(curve.p0.x, curve.p1.x), std::min(curve.p2.x, curve.p3.x));
    const double right = std::max(std::max(curve.p0.x, curve.p1.x), std::max(curve.p2.x, curve.p3.x));
    const double top = std::min(std::min(curve.p0.y, curve.p1.y), std::min(curve.p2.y, curve.p3.y));
    const double bottom = std::max(std::max(curve.p0.y, curve.p1.y), std::max(curve.p2.y, curve.p3.y));
    const gf::Rect bounds{left, top, right - left, bottom - top};
    return bounds;
}

double distance_to_polyline(std::span<const gf::Point> points, gf::Point point) noexcept {
    if (points.empty()) return std::numeric_limits<double>::infinity();
    if (points.size() == 1U) return std::hypot(point.x - points[0].x, point.y - points[0].y);
    double nearest = std::numeric_limits<double>::infinity();
    for (std::size_t index = 1U; index < points.size(); ++index) {
        const double distance = distance_to_segment(points[index - 1U], points[index], point);
        if (distance < nearest) nearest = distance;
    }
    return nearest;
}

bool segments_cross(gf::Point a, gf::Point b, gf::Point c, gf::Point d) noexcept {
    const double d1 = turn(c, d, a);
    const double d2 = turn(c, d, b);
    const double d3 = turn(a, b, c);
    const double d4 = turn(a, b, d);
    const bool straddle_one = (d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0);
    const bool straddle_two = (d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0);
    if (straddle_one && straddle_two) return true;
    // Collinear touching cases.
    if (d1 == 0.0 && within(a.x, c.x, d.x) && within(a.y, c.y, d.y)) return true;
    if (d2 == 0.0 && within(b.x, c.x, d.x) && within(b.y, c.y, d.y)) return true;
    if (d3 == 0.0 && within(c.x, a.x, b.x) && within(c.y, a.y, b.y)) return true;
    if (d4 == 0.0 && within(d.x, a.x, b.x) && within(d.y, a.y, b.y)) return true;
    return false;
}

bool polyline_crosses(std::span<const gf::Point> points, gf::Point c, gf::Point d) noexcept {
    for (std::size_t index = 1U; index < points.size(); ++index) {
        if (segments_cross(points[index - 1U], points[index], c, d)) return true;
    }
    return false;
}

bool rect_contains_rect(const gf::WorldRect& outer, const gf::WorldRect& inner) noexcept {
    const bool inside = inner.x >= outer.x && inner.y >= outer.y && inner.right() <= outer.right() &&
                        inner.bottom() <= outer.bottom();
    return inside;
}

bool rects_intersect(const gf::WorldRect& left, const gf::WorldRect& right) noexcept {
    const bool apart = left.right() < right.x || right.right() < left.x || left.bottom() < right.y ||
                       right.bottom() < left.y;
    return !apart;
}

// -- SpatialIndex ---------------------------------------------------------------------------

std::size_t SpatialIndex::column_of(double x) const noexcept {
    double cell = std::floor((x - left_) / cell_);
    if (!(cell > 0.0)) cell = 0.0;
    const double last = static_cast<double>(columns_ - 1U);
    if (cell > last) cell = last;
    return static_cast<std::size_t>(cell);
}

std::size_t SpatialIndex::row_of(double y) const noexcept {
    double cell = std::floor((y - top_) / cell_);
    if (!(cell > 0.0)) cell = 0.0;
    const double last = static_cast<double>(rows_ - 1U);
    if (cell > last) cell = last;
    return static_cast<std::size_t>(cell);
}

void SpatialIndex::build(const gf::NodeGraph& graph) {
    ++statistics_.builds;
    const std::span<const gf::NodeRecord> nodes = graph.node_slots();
    double left = std::numeric_limits<double>::infinity();
    double top = std::numeric_limits<double>::infinity();
    double right = -std::numeric_limits<double>::infinity();
    double bottom = -std::numeric_limits<double>::infinity();
    for (const gf::NodeRecord& node : nodes) {
        if (!node.live) continue;
        left = std::min(left, node.bounds.x);
        top = std::min(top, node.bounds.y);
        right = std::max(right, node.bounds.right());
        bottom = std::max(bottom, node.bounds.bottom());
    }
    if (!(left <= right)) {
        left = 0.0;
        top = 0.0;
        right = 1.0;
        bottom = 1.0;
    }
    left_ = left;
    top_ = top;
    cell_ = default_cell_size;
    // Grow the cell until the grid fits the cap.
    while (true) {
        const double columns = std::floor((right - left) / cell_) + 1.0;
        const double rows = std::floor((bottom - top) / cell_) + 1.0;
        if (columns * rows <= static_cast<double>(maximum_index_cells)) {
            columns_ = static_cast<std::size_t>(columns);
            rows_ = static_cast<std::size_t>(rows);
            break;
        }
        cell_ *= 2.0;
    }
    const std::size_t cells = columns_ * rows_;
    cell_start_.assign(cells + 1U, 0U);
    oversize_.clear();
    // Count, prefix, place (the CSR fill).
    for (const gf::NodeRecord& node : nodes) {
        if (!node.live) continue;
        const std::size_t c0 = column_of(node.bounds.x);
        const std::size_t c1 = column_of(node.bounds.right());
        const std::size_t r0 = row_of(node.bounds.y);
        const std::size_t r1 = row_of(node.bounds.bottom());
        if ((c1 - c0 + 1U) * (r1 - r0 + 1U) > maximum_cells_per_node) continue;
        for (std::size_t row = r0; row <= r1; ++row) {
            for (std::size_t column = c0; column <= c1; ++column) ++cell_start_[row * columns_ + column + 1U];
        }
    }
    for (std::size_t cell = 0U; cell < cells; ++cell) cell_start_[cell + 1U] += cell_start_[cell];
    entries_.resize(cell_start_[cells]);
    fill_.assign(cells, 0U);
    for (std::size_t slot = 0U; slot < nodes.size(); ++slot) {
        const gf::NodeRecord& node = nodes[slot];
        if (!node.live) continue;
        const std::size_t c0 = column_of(node.bounds.x);
        const std::size_t c1 = column_of(node.bounds.right());
        const std::size_t r0 = row_of(node.bounds.y);
        const std::size_t r1 = row_of(node.bounds.bottom());
        if ((c1 - c0 + 1U) * (r1 - r0 + 1U) > maximum_cells_per_node) {
            oversize_.push_back(static_cast<std::uint32_t>(slot));
            continue;
        }
        for (std::size_t row = r0; row <= r1; ++row) {
            for (std::size_t column = c0; column <= c1; ++column) {
                const std::size_t cell = row * columns_ + column;
                entries_[cell_start_[cell] + fill_[cell]] = static_cast<std::uint32_t>(slot);
                ++fill_[cell];
            }
        }
    }
    marks_.assign(nodes.size(), 0U);
    stamp_ = 0U;
    built_revision_ = graph.revision();
    statistics_.columns = columns_;
    statistics_.rows = rows_;
    statistics_.cell_size = cell_;
    statistics_.entries = entries_.size();
    statistics_.oversize = oversize_.size();
}

void SpatialIndex::query(const gf::NodeGraph& graph, const gf::WorldRect& area, bool containment,
                         std::vector<std::uint32_t>& out) {
    out.clear();
    ++statistics_.queries;
    const std::span<const gf::NodeRecord> nodes = graph.node_slots();
    if (columns_ == 0U || rows_ == 0U || marks_.size() < nodes.size()) return;
    if (stamp_ == std::numeric_limits<std::uint32_t>::max()) {
        marks_.assign(marks_.size(), 0U);
        stamp_ = 0U;
    }
    ++stamp_;
    // A query wholly outside the indexed box visits no cell.
    const bool outside = area.right() < left_ || area.bottom() < top_ ||
                         area.x > left_ + cell_ * static_cast<double>(columns_) ||
                         area.y > top_ + cell_ * static_cast<double>(rows_);
    if (!outside) {
        const std::size_t c0 = column_of(area.x);
        const std::size_t c1 = column_of(area.right());
        const std::size_t r0 = row_of(area.y);
        const std::size_t r1 = row_of(area.bottom());
        for (std::size_t row = r0; row <= r1; ++row) {
            for (std::size_t column = c0; column <= c1; ++column) {
                const std::size_t cell = row * columns_ + column;
                ++statistics_.cells_visited;
                for (std::uint32_t at = cell_start_[cell]; at < cell_start_[cell + 1U]; ++at) {
                    const std::uint32_t slot = entries_[at];
                    ++statistics_.entries_visited;
                    if (marks_[slot] == stamp_) continue;
                    marks_[slot] = stamp_;
                    if (slot >= nodes.size() || !nodes[slot].live) continue;
                    const gf::WorldRect& bounds = nodes[slot].bounds;
                    const bool hit = containment ? rect_contains_rect(area, bounds) : rects_intersect(area, bounds);
                    if (hit) out.push_back(slot);
                }
            }
        }
    }
    for (const std::uint32_t slot : oversize_) {
        ++statistics_.entries_visited;
        if (slot >= nodes.size() || !nodes[slot].live || marks_[slot] == stamp_) continue;
        marks_[slot] = stamp_;
        const gf::WorldRect& bounds = nodes[slot].bounds;
        const bool hit = containment ? rect_contains_rect(area, bounds) : rects_intersect(area, bounds);
        if (hit) out.push_back(slot);
    }
}

} // namespace node_graph
