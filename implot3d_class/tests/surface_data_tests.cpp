// The demonstrator's data and command line, without GUI.Forms: grids with
// exact edges and known values, a NaN hole where the saddle is cut, the
// helix's ends, a cloud that repeats per seed, and the options' bounds.

#include "../src/options.hpp"
#include "../src/surface_data.hpp"
#include "test_support.hpp"

#include <cmath>
#include <cstdio>
#include <exception>

using implot3d_class::PointCloud;
using implot3d_class::SurfaceGrid;
using implot3d_class::SurfaceKind;
using implot3d_class_tests::require;

namespace {

bool near(double left, double right, double tolerance) {
    const bool close = std::fabs(left - right) <= tolerance;
    return close;
}

void test_grids() {
    SurfaceGrid grid{};
    implot3d_class::fill_surface(grid, SurfaceKind::peaks, 33U);
    require(grid.rows == 33U && grid.columns == 33U, "the grid has the side asked for");
    require(grid.xs.size() == 33U && grid.ys.size() == 33U && grid.zs.size() == 33U * 33U, "the arrays fit the grid");
    require(grid.xs.front() == -3.0 && grid.xs.back() == 3.0, "x spans the peaks domain exactly");
    require(grid.ys.front() == -3.0 && grid.ys.back() == 3.0, "y spans the peaks domain exactly");
    // The centre sample (16, 16) is the origin: 3/e - 1/(3e).
    const double centre = grid.zs[16U * 33U + 16U];
    require(near(centre, (8.0 / 3.0) * std::exp(-1.0), 1e-12), "peaks at the origin");
    require(near(grid.zs[0], implot3d_class::surface_value(SurfaceKind::peaks, -3.0, -3.0), 0.0),
            "the grid holds the function's values");

    implot3d_class::fill_surface(grid, SurfaceKind::sinc, 3U);
    require(grid.zs[4] == 1.0, "sinc is 1 at the origin");

    implot3d_class::fill_surface(grid, SurfaceKind::saddle_with_hole, 21U);
    require(std::isnan(grid.zs[10U * 21U + 10U]), "the saddle's centre is a hole");
    require(near(grid.zs[0], 0.0, 1e-12), "the saddle is 0 at a corner");
    require(near(grid.zs[10U], -1.0, 1e-12), "the saddle is -1 at (0, -1)");

    implot3d_class::fill_surface(grid, SurfaceKind::peaks, 0U);
    require(grid.rows == implot3d_class::minimum_grid_side, "a side below the minimum is clamped");
}

void test_helix_and_cloud() {
    PointCloud helix{};
    implot3d_class::fill_helix(helix, 101U, 5.0);
    require(helix.xs.size() == 101U && helix.zs.size() == 101U, "the helix has the points asked for");
    require(near(helix.xs.front(), 1.0, 1e-12) && near(helix.zs.front(), -1.0, 1e-12), "the helix starts at (1, 0, -1)");
    require(near(helix.xs.back(), 1.0, 1e-9) && near(helix.ys.back(), 0.0, 1e-9) && near(helix.zs.back(), 1.0, 1e-12),
            "five whole turns end at (1, 0, 1)");

    PointCloud first{};
    PointCloud second{};
    implot3d_class::fill_normal_cloud(first, 20000U, 0.5, 7U);
    implot3d_class::fill_normal_cloud(second, 20000U, 0.5, 7U);
    require(first.xs == second.xs && first.zs == second.zs, "a seed repeats its cloud");
    double sum = 0.0;
    double squares = 0.0;
    for (const double value : first.xs) {
        require(std::isfinite(value), "every cloud value is finite");
        sum += value;
        squares += value * value;
    }
    const double mean = sum / 20000.0;
    const double spread = std::sqrt(squares / 20000.0 - mean * mean);
    require(near(mean, 0.0, 0.02), "the cloud is centred");
    require(near(spread, 0.5, 0.02), "the cloud has the spread asked for");
    implot3d_class::fill_normal_cloud(second, 20000U, 0.5, 8U);
    require(first.xs != second.xs, "another seed gives another cloud");
}

void test_options() {
    const char* const accepted[] = {"implot3d-class", "--measure", "orbit", "--grid", "256", "--width", "900"};
    const implot3d_class::OptionsResult good = implot3d_class::parse_options(7, accepted);
    require(good.error.empty(), "a good command line is accepted");
    require(good.options.grid_side == 256U && good.options.width == 900.0, "the values are taken");
    require(good.options.measure.has_value() && *good.options.measure == "orbit", "the workload is named");
    const char* const small[] = {"implot3d-class", "--grid", "3"};
    require(!implot3d_class::parse_options(3, small).error.empty(), "a grid below 4 is refused");
    const char* const missing[] = {"implot3d-class", "--grid"};
    require(!implot3d_class::parse_options(2, missing).error.empty(), "a missing value is refused");
    const char* const unknown[] = {"implot3d-class", "--colour", "red"};
    require(!implot3d_class::parse_options(3, unknown).error.empty(), "an unknown option is refused");
}

} // namespace

int main() {
    try {
        test_grids();
        test_helix_and_cloud();
        test_options();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "surface_data_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("surface_data_tests: passed");
    return 0;
}
