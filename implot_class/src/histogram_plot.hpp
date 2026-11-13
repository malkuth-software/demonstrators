#pragma once

// HistogramPlot: samples binned into equal bins and drawn as bars
// (ImPlot's histogram, behaviour only). PlotView has no histogram kind
// (GAPS.md P2), so this is a PlotFrame kind over plots::bin_values and
// SeriesMarkPainter::paint_histogram. The bin count is the user's (a
// TrackBar on the page); rebinning reuses the counts' storage up to the
// largest count seen.
//
// Samples are owned (copied in). Out-of-range and NaN samples are counted
// apart, as the binning kernel tallies them.

#include "plot_frame.hpp"

#include <gui_forms/plots/decimation/decimation.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace implot_class {

class HistogramPlot final : public PlotFrame {
public:
    static constexpr std::size_t minimum_bins = 2U;
    static constexpr std::size_t maximum_bins = 512U;

    explicit HistogramPlot(gui_forms::StableId stable_id);

    // Copies `samples` and bins them over their finite extent. Allocates.
    void set_samples(std::span<const double> samples);
    // Rebins (clamped to minimum_bins..maximum_bins).
    void set_bin_count(std::size_t count);
    [[nodiscard]] std::size_t bin_count() const noexcept { return spec_.count; }
    [[nodiscard]] std::span<const std::uint64_t> counts() const& noexcept {
        const std::span<const std::uint64_t> result(counts_.data(), spec_.count);
        return result;
    }
    std::span<const std::uint64_t> counts() const&& = delete;
    [[nodiscard]] const gui_forms::plots::BinTally& tally() const& noexcept { return tally_; }
    const gui_forms::plots::BinTally& tally() const&& = delete;
    [[nodiscard]] std::uint64_t rebins() const noexcept { return rebins_; }

protected:
    void paint_content(gui_forms::Painter& painter, const gui_forms::plots::PlotTransform& transform) override;
    [[nodiscard]] PlotFrameView data_extent() const override;
    [[nodiscard]] std::size_t maximum_points() const noexcept override { return maximum_bins; }
    [[nodiscard]] std::string readout() const override;

private:
    void rebin();

    std::vector<double> samples_{};
    // maximum_bins entries, reserved once; the first spec_.count are live.
    std::vector<std::uint64_t> counts_{};
    gui_forms::plots::BinSpec spec_{};
    gui_forms::plots::BinTally tally_{};
    std::uint64_t largest_count_{};
    std::uint64_t rebins_{};
};

} // namespace implot_class
