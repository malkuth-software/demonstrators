#include "heatmap_plot.hpp"

#include "series_data.hpp"

#include <gui_forms/types/painter/painter.hpp>

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace implot_class {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

constexpr gf::Color scale_edge = gf::Color::rgba(120, 120, 116);
constexpr gf::Color scale_ink = gf::Color::rgba(48, 48, 46);

} // namespace

HeatmapPlot::HeatmapPlot(gf::StableId stable_id) : PlotFrame(std::move(stable_id)) {
    slice_rects_.resize(scale_slices);
    slice_colors_.resize(scale_slices);
    slice_values_.resize(scale_slices);
}

void HeatmapPlot::set_values(std::span<const double> values, std::size_t rows, std::size_t columns) {
    if (rows == 0U || columns == 0U || columns > values.size() / rows || values.size() != rows * columns) {
        throw std::invalid_argument("HeatmapPlot::set_values: values do not hold rows * columns");
    }
    std::vector<double> copy(values.begin(), values.end());
    values_ = std::move(copy);
    rows_ = rows;
    columns_ = columns;
    hovered_.reset();
    update_domain();
    fit_on_next_layout();
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}

void HeatmapPlot::set_colormap(gp::BuiltInColormap which) {
    colormap_ = which;
    invalidate(gf::Dirty::paint);
}

void HeatmapPlot::set_domain(std::optional<gp::PlotRange> domain) {
    fixed_domain_ = domain;
    update_domain();
    invalidate(gf::Dirty::paint);
}

void HeatmapPlot::update_domain() noexcept {
    if (fixed_domain_.has_value() && (*fixed_domain_).valid()) {
        domain_ = *fixed_domain_;
        return;
    }
    const ValueExtent extent = finite_extent(values_);
    domain_ = gp::PlotRange{extent.minimum, extent.maximum};
}

PlotFrameView HeatmapPlot::data_extent() const {
    PlotFrameView view{};
    if (rows_ == 0U) return view;
    view.x = gp::PlotRange{0.0, static_cast<double>(columns_)};
    view.y = gp::PlotRange{0.0, static_cast<double>(rows_)};
    return view;
}

std::size_t HeatmapPlot::maximum_points() const noexcept {
    // paint_heatmap writes one rectangle and colour per cell.
    const std::size_t cells = rows_ * columns_;
    return cells > 4096U ? cells : 4096U;
}

void HeatmapPlot::paint_content(gf::Painter& painter, const gp::PlotTransform& transform) {
    if (values_.empty()) return;
    gp::Scale scale{};
    scale.domain = domain_;
    const gp::PlotRange area_x{0.0, static_cast<double>(columns_)};
    const gp::PlotRange area_y{0.0, static_cast<double>(rows_)};
    const gp::Colormap& map = gp::built_in_colormap(colormap_);
    note_marks(marks().paint_heatmap(painter, values_, rows_, columns_, area_x, area_y, map, scale, transform));
}

void HeatmapPlot::paint_margin(gf::Painter& painter, gf::Rect margin) {
    if (margin.height <= 0.0) return;
    const gf::Rect bar{margin.x + 10.0, margin.y, 14.0, margin.height};
    const double slice_height = bar.height / static_cast<double>(scale_slices);
    // The top slice is the domain's maximum.
    for (std::size_t index = 0U; index < scale_slices; ++index) {
        const double share = (static_cast<double>(scale_slices - 1U - index) + 0.5) / static_cast<double>(scale_slices);
        slice_values_[index] = domain_.minimum + share * domain_.extent();
        slice_rects_[index] = gf::Rect{bar.x, bar.y + slice_height * static_cast<double>(index), bar.width, slice_height + 0.5};
    }
    gp::Scale scale{};
    scale.domain = domain_;
    const bool sampled = gp::sample_colormap_span(gp::built_in_colormap(colormap_), scale, slice_values_, slice_colors_);
    if (sampled) painter.fill_rects_each(slice_rects_, slice_colors_);
    painter.stroke_rect(bar, scale_edge, 1.0);
    gf::FontSpec font{};
    font.size = 10.0;
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.2g", domain_.maximum);
    painter.draw_text_utf8(gf::Point{bar.x + bar.width + 3.0, bar.y + 9.0}, text, font, scale_ink);
    std::snprintf(text, sizeof(text), "%.2g", domain_.minimum);
    painter.draw_text_utf8(gf::Point{bar.x + bar.width + 3.0, bar.y + bar.height}, text, font, scale_ink);
}

void HeatmapPlot::hover_content(gf::Point local, const gp::PlotTransform& transform) {
    const double x = transform.x_from_device(local.x);
    const double y = transform.y_from_device(local.y);
    std::optional<std::size_t> cell{};
    if (x >= 0.0 && y >= 0.0 && x < static_cast<double>(columns_) && y < static_cast<double>(rows_)) {
        const std::size_t column = static_cast<std::size_t>(x);
        // Row 0 is the top row: data y counts up from the bottom.
        const std::size_t row = rows_ - 1U - static_cast<std::size_t>(y);
        cell = row * columns_ + column;
    }
    if (cell == hovered_) return;
    hovered_ = cell;
    invalidate(gf::Dirty::paint);
}

std::string HeatmapPlot::readout() const {
    if (!hovered_.has_value() || *hovered_ >= values_.size()) return std::string{};
    const std::size_t row = *hovered_ / columns_;
    const std::size_t column = *hovered_ % columns_;
    char text[64]{};
    const int written = std::snprintf(text, sizeof(text), "row %zu, column %zu: %.3f", row, column, values_[*hovered_]);
    std::string result{};
    if (written > 0) result.assign(text, static_cast<std::size_t>(written));
    return result;
}

} // namespace implot_class
