#pragma once

// LinkedPlotGrid: subplots with linked axes (ImPlot's subplots and linked
// rows and columns, behaviour only). A GridPage of PlotViews whose first x
// axes share one plots::AxisLink, and whose first y axes share another
// while `link_y` is on.
//
// PlotView takes no AxisLink (GAPS.md P7): the link primitive exists, but
// the control neither joins one nor reads one, so the page does the
// joining by hand: each plot's view_changed writes the link, and the page
// sets the new range on every other plot. PlotView::set_x_range raises
// view_changed again, so `propagating_` keeps that from recursing.
//
// Order: a gesture on plot k raises view_changed(k) after its own range
// moved; the page then sets the others in child order, each raising its own
// view_changed, which the guard ignores. One UI thread.

#include "grid_page.hpp"

#include <gui_forms/controls/plot_view.hpp>
#include <gui_forms/plots/axis/axis.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace implot_class {

class LinkedPlotGrid final : public GridPage {
public:
    static constexpr bool initialize_tree_after_construction = true;
    static constexpr std::size_t plot_count = 4U;

    explicit LinkedPlotGrid(gui_forms::StableId stable_id);
    void initialize_control_tree();

    [[nodiscard]] gui_forms::PlotView& plot(std::size_t index) const { return *plots_.at(index); }
    void set_link_y(bool linked);
    [[nodiscard]] bool link_y() const noexcept { return link_y_; }
    [[nodiscard]] const gui_forms::plots::AxisLink& x_link() const noexcept { return x_link_; }
    [[nodiscard]] const gui_forms::plots::AxisLink& y_link() const noexcept { return y_link_; }
    // Ranges the page set on peers, and view changes it ignored as echoes.
    [[nodiscard]] std::uint64_t propagations() const noexcept { return propagations_; }
    [[nodiscard]] std::uint64_t echoes() const noexcept { return echoes_; }

private:
    void on_view_changed(const std::size_t& index, const gui_forms::PlotViewRanges& ranges);

    std::array<std::shared_ptr<gui_forms::PlotView>, plot_count> plots_{};
    gui_forms::plots::AxisLink x_link_{gui_forms::plots::PlotRange{0.0, 10.0}};
    gui_forms::plots::AxisLink y_link_{gui_forms::plots::PlotRange{-1.5, 1.5}};
    bool link_y_{};
    bool propagating_{};
    std::uint64_t propagations_{};
    std::uint64_t echoes_{};
};

} // namespace implot_class
