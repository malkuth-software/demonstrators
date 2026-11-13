#pragma once

// The demonstrator's data: deterministic generators for every plot it
// shows (IMGUI_IMPLOT_STUDY B.2's demo series, behaviour only). Each fills
// caller-owned storage; none allocates except make_* which return owned
// series. The generators use no host randomness and, apart from the sine
// family (std::sin, whose last bit may differ between hosts' libm), no
// libm: the same seed gives the same bits everywhere.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace implot_class {

// SplitMix64: a 64-bit counter-based generator. Deterministic, trivially
// copyable, one state word.
struct SplitMix64 final {
    std::uint64_t state{};

    std::uint64_t next() noexcept;
    // A uniform value in [0, 1) from the top 53 bits.
    double uniform() noexcept;
    // An approximately normal value (mean 0, deviation 1): the sum of
    // twelve uniforms less six (Irwin-Hall), so no libm is needed. Its
    // tails end at +-6.
    double normal() noexcept;
};

// Two equal-length columns, x and y, owned.
struct XYSeries final {
    std::vector<double> xs{};
    std::vector<double> ys{};

    [[nodiscard]] std::size_t size() const noexcept { return xs.size(); }
};

// x = 0, 1, 2, ... and a random walk in y from 0 with unit-deviation
// steps. `xs` and `ys` have equal lengths (nothing is written otherwise).
void fill_random_walk(std::span<double> xs, std::span<double> ys, std::uint64_t seed) noexcept;
// x evenly from `x_start` by `x_step`, y = amplitude * sin(frequency * x + phase).
void fill_sine(std::span<double> xs, std::span<double> ys, double x_start, double x_step, double frequency,
               double amplitude, double phase) noexcept;
// A normal cloud centred on (cx, cy) with deviations (sx, sy).
void fill_normal_cloud(std::span<double> xs, std::span<double> ys, double cx, double cy, double sx, double sy,
                       std::uint64_t seed) noexcept;
// `values` (rows * columns, row-major, row 0 the top row) filled with a
// smooth field in [0, 1]: three bumps 1 / (1 + d^2 / r^2), normalised by
// the largest. Nothing is written when the size does not match.
void fill_heatmap(std::span<double> values, std::size_t rows, std::size_t columns, std::uint64_t seed) noexcept;
// Samples of a two-peaked mixture: normal(-1, 0.6) with weight 0.6, normal(1.5, 0.4)
// with weight 0.4.
void fill_mixture(std::span<double> samples, std::uint64_t seed) noexcept;

[[nodiscard]] XYSeries make_random_walk(std::size_t count, std::uint64_t seed);
[[nodiscard]] XYSeries make_sine(std::size_t count, double x_step, double frequency, double amplitude, double phase);
[[nodiscard]] XYSeries make_normal_cloud(std::size_t count, double cx, double cy, double sx, double sy,
                                         std::uint64_t seed);

// The extremes of finite values in `values`; {0, 1} when none is finite or
// they are equal (so the range is always valid).
struct ValueExtent final {
    double minimum{};
    double maximum{1.0};
};
[[nodiscard]] ValueExtent finite_extent(std::span<const double> values) noexcept;

} // namespace implot_class
