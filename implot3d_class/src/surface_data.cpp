#include "surface_data.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace implot3d_class {

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double saddle_hole_radius = 0.3;

double peaks_value(double x, double y) noexcept {
    const double first = 3.0 * (1.0 - x) * (1.0 - x) * std::exp(-x * x - (y + 1.0) * (y + 1.0));
    const double second = -10.0 * (x / 5.0 - x * x * x - std::pow(y, 5.0)) * std::exp(-x * x - y * y);
    const double third = -std::exp(-(x + 1.0) * (x + 1.0) - y * y) / 3.0;
    const double value = first + second + third;
    return value;
}

double sinc_value(double x, double y) noexcept {
    const double radius = std::sqrt(x * x + y * y);
    // The limit at the origin.
    if (radius < 1e-12) return 1.0;
    const double value = std::sin(radius) / radius;
    return value;
}

double saddle_value(double x, double y) noexcept {
    const double radius = std::sqrt(x * x + y * y);
    if (radius < saddle_hole_radius) return std::numeric_limits<double>::quiet_NaN();
    const double value = x * x - y * y;
    return value;
}

// SplitMix64: a small fixed generator, so clouds repeat on every host.
std::uint64_t next_random(std::uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t mixed = state;
    mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
    const std::uint64_t result = mixed ^ (mixed >> 31U);
    return result;
}

// A uniform value in (0, 1): the top 53 bits, offset by half a step so it
// is never 0 (the logarithm below).
double next_unit(std::uint64_t& state) noexcept {
    const std::uint64_t bits = next_random(state) >> 11U;
    const double unit = (static_cast<double>(bits) + 0.5) / 9007199254740992.0;
    return unit;
}

// One standard normal value (Box-Muller, cosine branch only).
double next_normal(std::uint64_t& state) noexcept {
    const double first = next_unit(state);
    const double second = next_unit(state);
    const double value = std::sqrt(-2.0 * std::log(first)) * std::cos(2.0 * pi * second);
    return value;
}

} // namespace

double surface_extent(SurfaceKind kind) noexcept {
    switch (kind) {
    case SurfaceKind::peaks:
        return 3.0;
    case SurfaceKind::sinc:
        return 8.0;
    case SurfaceKind::saddle_with_hole:
    default:
        return 1.0;
    }
}

double surface_value(SurfaceKind kind, double x, double y) noexcept {
    switch (kind) {
    case SurfaceKind::peaks:
        return peaks_value(x, y);
    case SurfaceKind::sinc:
        return sinc_value(x, y);
    case SurfaceKind::saddle_with_hole:
    default:
        return saddle_value(x, y);
    }
}

void fill_surface(SurfaceGrid& grid, SurfaceKind kind, std::size_t side) {
    const std::size_t clamped = std::clamp(side, minimum_grid_side, maximum_grid_side);
    const double extent = surface_extent(kind);
    const double step = 2.0 * extent / static_cast<double>(clamped - 1U);
    grid.rows = clamped;
    grid.columns = clamped;
    grid.xs.resize(clamped);
    grid.ys.resize(clamped);
    // clamped <= 2048, so the product cannot overflow.
    grid.zs.resize(clamped * clamped);
    for (std::size_t index = 0U; index < clamped; ++index) {
        const double value = -extent + step * static_cast<double>(index);
        grid.xs[index] = value;
        grid.ys[index] = value;
    }
    // The last sample sits exactly on the edge (no rounding drift).
    grid.xs[clamped - 1U] = extent;
    grid.ys[clamped - 1U] = extent;
    for (std::size_t row = 0U; row < clamped; ++row) {
        const double y = grid.ys[row];
        for (std::size_t column = 0U; column < clamped; ++column) {
            grid.zs[row * clamped + column] = surface_value(kind, grid.xs[column], y);
        }
    }
}

void fill_helix(PointCloud& cloud, std::size_t points, double turns) {
    const std::size_t count = std::max<std::size_t>(points, 2U);
    cloud.xs.resize(count);
    cloud.ys.resize(count);
    cloud.zs.resize(count);
    const double last = static_cast<double>(count - 1U);
    for (std::size_t index = 0U; index < count; ++index) {
        const double share = static_cast<double>(index) / last;
        const double angle = 2.0 * pi * turns * share;
        cloud.xs[index] = std::cos(angle);
        cloud.ys[index] = std::sin(angle);
        cloud.zs[index] = -1.0 + 2.0 * share;
    }
}

void fill_normal_cloud(PointCloud& cloud, std::size_t points, double spread, std::uint64_t seed) {
    cloud.xs.resize(points);
    cloud.ys.resize(points);
    cloud.zs.resize(points);
    std::uint64_t state = seed;
    for (std::size_t index = 0U; index < points; ++index) {
        cloud.xs[index] = spread * next_normal(state);
        cloud.ys[index] = spread * next_normal(state);
        cloud.zs[index] = spread * next_normal(state);
    }
}

} // namespace implot3d_class
