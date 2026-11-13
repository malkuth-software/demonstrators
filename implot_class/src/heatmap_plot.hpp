#pragma once

// HeatmapPlot: a grid of values coloured through a colormap, with its
// colour scale beside the plot (ImPlot's heatmap and colormap scale,
// behaviour only). PlotView has no heatmap kind (GAPS.md P2), so this is a
// PlotFrame kind over SeriesMarkPainter::paint_heatmap, which draws one
// fill_rects_each batch per frame.
//
// Data: `rows` x `columns` values, row-major, row 0 drawn at the top, over
// the data rectangle x [0, columns], y [0, rows]. The values are owned
// (copied in by set_values). The colour domain is the values' finite
// extent unless set_domain fixes it. The hover readout names the cell
// under the pointer.

#include "plot_frame.hpp"

#include <gui_forms/plots/colormap/colormap.hpp>
#include <gui_forms/plots/scale/scale.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace implot_class {

class HeatmapPlot final : public PlotFrame {
public:
    static constexpr double scale_margin = 56.0;
    static constexpr std::size_t scale_slices = 64U;

    explicit HeatmapPlot(gui_forms::StableId stable_id);

    // Copies `values` (rows * columns; std::invalid_argument otherwise).
    // Allocates; nothing changes on a throw.
    void set_values(std::span<const double> values, std::size_t rows, std::size_t columns);
    void set_colormap(gui_forms::plots::BuiltInColormap which);
    // Fixes the colour domain; none returns to the data's extent.
    void set_domain(std::optional<gui_forms::plots::PlotRange> domain);
    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }
    [[nodiscard]] gui_forms::plots::PlotRange colour_domain() const noexcept { return domain_; }
    // The cell the pointer last hovered, if any (row, column).
    [[nodiscard]] std::optional<std::size_t> hovered_cell() const noexcept { return hovered_; }

protected:
    void paint_content(gui_forms::Painter& painter, const gui_forms::plots::PlotTransform& transform) override;
    [[nodiscard]] PlotFrameView data_extent() const override;
    [[nodiscard]] std::size_t maximum_points() const noexcept override;
    [[nodiscard]] double right_margin() const noexcept override { return scale_margin; }
    void paint_margin(gui_forms::Painter& painter, gui_forms::Rect margin) override;
    void hover_content(gui_forms::Point local, const gui_forms::plots::PlotTransform& transform) override;
    [[nodiscard]] std::string readout() const override;

private:
    void update_domain() noexcept;

    std::vector<double> values_{};
    std::size_t rows_{};
    std::size_t columns_{};
    gui_forms::plots::BuiltInColormap colormap_{gui_forms::plots::BuiltInColormap::viridis};
    std::optional<gui_forms::plots::PlotRange> fixed_domain_{};
    gui_forms::plots::PlotRange domain_{};
    std::optional<std::size_t> hovered_{};
    // The colour scale's slices, reused each paint.
    std::vector<gui_forms::Rect> slice_rects_{};
    std::vector<gui_forms::Color> slice_colors_{};
    std::vector<double> slice_values_{};
};

} // namespace implot_class
