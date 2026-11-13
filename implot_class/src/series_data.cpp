#include "series_data.hpp"

#include <cmath>

namespace implot_class {

std::uint64_t SplitMix64::next() noexcept {
    state = state + 0x9E3779B97F4A7C15ULL;
    std::uint64_t mixed = state;
    mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
    const std::uint64_t result = mixed ^ (mixed >> 31U);
    return result;
}

double SplitMix64::uniform() noexcept {
    const std::uint64_t bits = next() >> 11U;
    const double result = static_cast<double>(bits) * (1.0 / 9007199254740992.0);
    return result;
}

double SplitMix64::normal() noexcept {
    double sum = 0.0;
    for (std::size_t index = 0U; index < 12U; ++index) sum += uniform();
    const double result = sum - 6.0;
    return result;
}

void fill_random_walk(std::span<double> xs, std::span<double> ys, std::uint64_t seed) noexcept {
    if (xs.size() != ys.size()) return;
    SplitMix64 random{seed};
    double y = 0.0;
    for (std::size_t index = 0U; index < xs.size(); ++index) {
        xs[index] = static_cast<double>(index);
        ys[index] = y;
        y += random.normal();
    }
}

void fill_sine(std::span<double> xs, std::span<double> ys, double x_start, double x_step, double frequency,
               double amplitude, double phase) noexcept {
    if (xs.size() != ys.size()) return;
    for (std::size_t index = 0U; index < xs.size(); ++index) {
        const double x = x_start + static_cast<double>(index) * x_step;
        xs[index] = x;
        ys[index] = amplitude * std::sin(frequency * x + phase);
    }
}

void fill_normal_cloud(std::span<double> xs, std::span<double> ys, double cx, double cy, double sx, double sy,
                       std::uint64_t seed) noexcept {
    if (xs.size() != ys.size()) return;
    SplitMix64 random{seed};
    for (std::size_t index = 0U; index < xs.size(); ++index) {
        xs[index] = cx + sx * random.normal();
        ys[index] = cy + sy * random.normal();
    }
}

void fill_heatmap(std::span<double> values, std::size_t rows, std::size_t columns, std::uint64_t seed) noexcept {
    if (rows == 0U || columns == 0U || columns > values.size() / rows || values.size() != rows * columns) return;
    SplitMix64 random{seed};
    struct Bump final {
        double x{};
        double y{};
        double radius_squared{};
        double weight{};
    };
    Bump bumps[3]{};
    for (Bump& bump : bumps) {
        bump.x = random.uniform();
        bump.y = random.uniform();
        const double radius = 0.08 + 0.2 * random.uniform();
        bump.radius_squared = radius * radius;
        bump.weight = 0.5 + random.uniform();
    }
    double largest = 0.0;
    for (std::size_t row = 0U; row < rows; ++row) {
        const double y = (static_cast<double>(row) + 0.5) / static_cast<double>(rows);
        for (std::size_t column = 0U; column < columns; ++column) {
            const double x = (static_cast<double>(column) + 0.5) / static_cast<double>(columns);
            double value = 0.0;
            for (const Bump& bump : bumps) {
                const double dx = x - bump.x;
                const double dy = y - bump.y;
                const double distance_squared = dx * dx + dy * dy;
                value += bump.weight / (1.0 + distance_squared / bump.radius_squared);
            }
            values[row * columns + column] = value;
            if (value > largest) largest = value;
        }
    }
    if (!(largest > 0.0)) return;
    for (double& value : values) value = value / largest;
}

void fill_mixture(std::span<double> samples, std::uint64_t seed) noexcept {
    SplitMix64 random{seed};
    for (double& sample : samples) {
        const bool first = random.uniform() < 0.6;
        const double deviate = random.normal();
        sample = first ? -1.0 + 0.6 * deviate : 1.5 + 0.4 * deviate;
    }
}

XYSeries make_random_walk(std::size_t count, std::uint64_t seed) {
    XYSeries series{};
    series.xs.resize(count);
    series.ys.resize(count);
    fill_random_walk(series.xs, series.ys, seed);
    return series;
}

XYSeries make_sine(std::size_t count, double x_step, double frequency, double amplitude, double phase) {
    XYSeries series{};
    series.xs.resize(count);
    series.ys.resize(count);
    fill_sine(series.xs, series.ys, 0.0, x_step, frequency, amplitude, phase);
    return series;
}

XYSeries make_normal_cloud(std::size_t count, double cx, double cy, double sx, double sy, std::uint64_t seed) {
    XYSeries series{};
    series.xs.resize(count);
    series.ys.resize(count);
    fill_normal_cloud(series.xs, series.ys, cx, cy, sx, sy, seed);
    return series;
}

ValueExtent finite_extent(std::span<const double> values) noexcept {
    ValueExtent extent{};
    bool found = false;
    double minimum = 0.0;
    double maximum = 0.0;
    for (const double value : values) {
        if (!std::isfinite(value)) continue;
        if (!found) {
            minimum = value;
            maximum = value;
            found = true;
            continue;
        }
        if (value < minimum) minimum = value;
        if (value > maximum) maximum = value;
    }
    if (found && minimum < maximum) {
        extent.minimum = minimum;
        extent.maximum = maximum;
    }
    return extent;
}

} // namespace implot_class
