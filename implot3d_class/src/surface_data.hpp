#pragma once

// The demonstrator's data: gridded surfaces, a helix and a scatter cloud,
// all deterministic (a fixed-seed generator), so records repeat. A grid
// holds its axes once (x per column, y per row) and its heights row-major,
// which is the layout Plot3DSurfaceSpec borrows.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace implot3d_class {

enum class SurfaceKind : std::uint8_t {
    // MATLAB-style peaks: three Gaussian bumps and a dip, on [-3, 3]^2.
    peaks,
    // sin(r) / r ripples on [-8, 8]^2.
    sinc,
    // x^2 - y^2 on [-1, 1]^2 with a disc of NaN cut out (a hole).
    saddle_with_hole,
};

struct SurfaceGrid final {
    std::vector<double> xs{};    // columns values
    std::vector<double> ys{};    // rows values
    std::vector<double> zs{};    // rows * columns heights, [row][column]
    std::size_t rows{};
    std::size_t columns{};
};

struct PointCloud final {
    std::vector<double> xs{};
    std::vector<double> ys{};
    std::vector<double> zs{};
};

inline constexpr std::size_t minimum_grid_side = 2U;
inline constexpr std::size_t maximum_grid_side = 2048U;

// Fills `grid` (its storage reused) with `side` x `side` samples of `kind`.
// `side` is clamped to [minimum_grid_side, maximum_grid_side].
void fill_surface(SurfaceGrid& grid, SurfaceKind kind, std::size_t side);
// A helix of `turns` turns, `points` points, radius 1, rising from z = -1
// to 1. `points` is at least 2.
void fill_helix(PointCloud& cloud, std::size_t points, double turns);
// `points` points of a normal cloud (standard deviation `spread` per axis)
// about the origin, from `seed`.
void fill_normal_cloud(PointCloud& cloud, std::size_t points, double spread, std::uint64_t seed);

// The surface's value at (x, y), for tests and picking checks; NaN in the
// hole of saddle_with_hole.
[[nodiscard]] double surface_value(SurfaceKind kind, double x, double y) noexcept;
// The surface's domain: [-extent, extent] on both axes.
[[nodiscard]] double surface_extent(SurfaceKind kind) noexcept;

} // namespace implot3d_class
