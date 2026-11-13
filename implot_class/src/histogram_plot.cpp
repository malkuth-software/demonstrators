#include "histogram_plot.hpp"

#include "series_data.hpp"

#include <gui_forms/plots/series_column/series_column.hpp>
#include <gui_forms/types/painter/painter.hpp>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace implot_class {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

constexpr gf::Color bar_fill = gf::Color::rgba(70, 120, 190);

} // namespace

HistogramPlot::HistogramPlot(gf::StableId stable_id) : PlotFrame(std::move(stable_id)) {
    counts_.resize(maximum_bins);
    spec_.count = 40U;
}

void HistogramPlot::set_samples(std::span<const double> samples) {
    std::vector<double> copy(samples.begin(), samples.end());
    samples_ = std::move(copy);
    const ValueExtent extent = finite_extent(samples_);
    spec_.minimum = extent.minimum;
    spec_.maximum = extent.maximum;
    rebin();
    fit_on_next_layout();
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}

void HistogramPlot::set_bin_count(std::size_t count) {
    const std::size_t clamped = std::clamp(count, minimum_bins, maximum_bins);
    if (clamped == spec_.count) return;
    spec_.count = clamped;
    rebin();
    fit();
}

void HistogramPlot::rebin() {
    std::fill(counts_.begin(), counts_.end(), std::uint64_t{0});
    gp::SeriesSpans<double> spans{};
    spans.first = std::span<const double>(samples_.data(), samples_.size());
    tally_ = gp::bin_values(spans, spec_, std::span<std::uint64_t>(counts_.data(), spec_.count));
    largest_count_ = 0U;
    for (std::size_t bin = 0U; bin < spec_.count; ++bin) {
        if (counts_[bin] > largest_count_) largest_count_ = counts_[bin];
    }
    ++rebins_;
    invalidate(gf::Dirty::paint);
}

PlotFrameView HistogramPlot::data_extent() const {
    PlotFrameView view{};
    view.x = gp::PlotRange{spec_.minimum, spec_.maximum};
    const double top = largest_count_ > 0U ? static_cast<double>(largest_count_) * 1.05 : 1.0;
    view.y = gp::PlotRange{0.0, top};
    return view;
}

void HistogramPlot::paint_content(gf::Painter& painter, const gp::PlotTransform& transform) {
    if (samples_.empty()) return;
    const std::span<const std::uint64_t> live(counts_.data(), spec_.count);
    note_marks(marks().paint_histogram(painter, live, spec_, transform, bar_fill));
}

std::string HistogramPlot::readout() const {
    char text[96]{};
    const int written = std::snprintf(text, sizeof(text), "%zu bins, %llu binned, %llu outside", spec_.count,
                                      static_cast<unsigned long long>(tally_.binned),
                                      static_cast<unsigned long long>(tally_.below + tally_.above + tally_.gaps));
    std::string result{};
    if (written > 0) result.assign(text, static_cast<std::size_t>(written));
    return result;
}

} // namespace implot_class
