// The generated data: the same seed gives the same values, every column
// has its stated length and range, and binning the mixture through the
// plot primitives' bin_values accounts for every sample. Headless.

#include "../src/series_data.hpp"
#include "test_support.hpp"

#include <gui_forms/plots/decimation/decimation.hpp>
#include <gui_forms/plots/series_column/series_column.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <span>
#include <vector>

namespace gp = gui_forms::plots;
using implot_class_tests::require;

namespace {

void test_generator_repeats() {
    implot_class::SplitMix64 first{42U};
    implot_class::SplitMix64 second{42U};
    for (std::size_t index = 0U; index < 1000U; ++index) {
        require(first.next() == second.next(), "the same seed gives the same sequence");
    }
    implot_class::SplitMix64 uniform{7U};
    for (std::size_t index = 0U; index < 10000U; ++index) {
        const double value = uniform.uniform();
        require(value >= 0.0 && value < 1.0, "uniform values lie in [0, 1)");
    }
    implot_class::SplitMix64 normal{9U};
    double sum = 0.0;
    for (std::size_t index = 0U; index < 10000U; ++index) {
        const double value = normal.normal();
        require(value >= -6.0 && value <= 6.0, "Irwin-Hall normals end at +-6");
        sum += value;
    }
    require(std::fabs(sum / 10000.0) < 0.05, "the normals' mean is near 0");
}

void test_series_shapes() {
    const implot_class::XYSeries walk = implot_class::make_random_walk(5000U, 3U);
    const implot_class::XYSeries again = implot_class::make_random_walk(5000U, 3U);
    require(walk.size() == 5000U && walk.ys.size() == 5000U, "the walk has its length");
    require(walk.ys == again.ys, "the walk repeats for its seed");
    require(walk.xs[0] == 0.0 && walk.xs[4999] == 4999.0, "the walk's x counts samples");
    const implot_class::XYSeries sine = implot_class::make_sine(1000U, 0.01, 2.0, 3.0, 0.0);
    for (const double value : sine.ys) require(std::fabs(value) <= 3.0 + 1e-12, "the sine stays within its amplitude");
    std::vector<double> heatmap(32U * 48U);
    implot_class::fill_heatmap(heatmap, 32U, 48U, 5U);
    double largest = 0.0;
    for (const double value : heatmap) {
        require(value >= 0.0 && value <= 1.0, "heatmap values lie in [0, 1]");
        if (value > largest) largest = value;
    }
    require(largest == 1.0, "the heatmap is normalised by its largest value");
    std::vector<double> wrong(10U, -1.0);
    implot_class::fill_heatmap(wrong, 32U, 48U, 5U);
    require(wrong[0] == -1.0, "a mismatched size writes nothing");
}

void test_extent() {
    const std::vector<double> values{3.0, NAN, -2.0, INFINITY, 7.5};
    const implot_class::ValueExtent extent = implot_class::finite_extent(values);
    require(extent.minimum == -2.0 && extent.maximum == 7.5, "the extent skips non-finite values");
    const std::vector<double> flat{4.0, 4.0};
    const implot_class::ValueExtent fallback = implot_class::finite_extent(flat);
    require(fallback.minimum < fallback.maximum, "a flat series still gives a valid range");
}

void test_binning_accounts_for_every_sample() {
    std::vector<double> samples(100000U);
    implot_class::fill_mixture(samples, 11U);
    const implot_class::ValueExtent extent = implot_class::finite_extent(samples);
    gp::BinSpec spec{};
    spec.minimum = extent.minimum;
    spec.maximum = extent.maximum;
    spec.count = 64U;
    // bin_values adds to `counts`; it does not clear them (GAPS.md).
    std::vector<std::uint64_t> counts(64U, 0U);
    gp::SeriesSpans<double> spans{};
    spans.first = std::span<const double>(samples);
    const gp::BinTally tally = gp::bin_values(spans, spec, counts);
    require(!tally.refused, "the spec is accepted");
    std::uint64_t total = 0U;
    for (const std::uint64_t count : counts) total += count;
    require(total == samples.size(), "every sample lands in a bin (the maximum in the last)");
    require(tally.binned == samples.size(), "the tally counts every sample");
    // Two peaks: the bins near -1 and near 1.5 outweigh the trough at 0.6.
    const double width = (spec.maximum - spec.minimum) / 64.0;
    const std::size_t left = static_cast<std::size_t>((-1.0 - spec.minimum) / width);
    const std::size_t trough = static_cast<std::size_t>((0.6 - spec.minimum) / width);
    const std::size_t right = static_cast<std::size_t>((1.5 - spec.minimum) / width);
    require(counts[left] > counts[trough] && counts[right] > counts[trough], "the mixture has two peaks");
}

} // namespace

int main() {
    try {
        test_generator_repeats();
        test_series_shapes();
        test_extent();
        test_binning_accounts_for_every_sample();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "series_data_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("series_data_tests: passed");
    return 0;
}
