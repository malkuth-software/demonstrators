#include "linked_axes.hpp"

#include "series_data.hpp"

#include <gui_forms/controls/plot_axis_item.hpp>
#include <gui_forms/controls/plot_series_item.hpp>
#include <gui_forms/event.hpp>

#include <cstdio>
#include <string>
#include <utility>

namespace implot_class {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

constexpr std::size_t subplot_points = 2000U;

std::shared_ptr<gf::PlotAxisItem> make_axis(const std::string& id, gf::PlotAxisDirection direction, double minimum,
                                            double maximum) {
    std::shared_ptr<gf::PlotAxisItem> axis = gf::make_control<gf::PlotAxisItem>(gf::StableId(id));
    (*axis).direction = direction;
    (*axis).minimum = minimum;
    (*axis).maximum = maximum;
    return axis;
}

} // namespace

LinkedPlotGrid::LinkedPlotGrid(gf::StableId stable_id) : GridPage(std::move(stable_id), 2U) {}

void LinkedPlotGrid::initialize_control_tree() {
    for (std::size_t index = 0U; index < plot_count; ++index) {
        char id[48]{};
        std::snprintf(id, sizeof(id), "implot.subplots.plot-%zu", index);
        const std::string base(id);
        std::shared_ptr<gf::PlotView> plot = gf::make_control<gf::PlotView>(gf::StableId(base));
        char title[32]{};
        std::snprintf(title, sizeof(title), "Subplot %zu", index + 1U);
        (*plot).title = std::string(title);
        (*plot).show_legend = false;
        (*plot).add_child(make_axis(base + ".x", gf::PlotAxisDirection::horizontal, 0.0, 10.0));
        (*plot).add_child(make_axis(base + ".y", gf::PlotAxisDirection::vertical, -1.5, 1.5));
        std::shared_ptr<gf::PlotSeriesItem> series = gf::make_control<gf::PlotSeriesItem>(gf::StableId(base + ".series"));
        (*series).kind = index % 2U == 0U ? gf::PlotSeriesKind::line : gf::PlotSeriesKind::scatter;
        const XYSeries data = make_sine(subplot_points, 10.0 / static_cast<double>(subplot_points),
                                        1.0 + static_cast<double>(index), 1.0, 0.3 * static_cast<double>(index));
        (*series).set_points(data.xs, data.ys);
        (*plot).add_child(series);
        gf::on((*plot).view_changed(), *this, &LinkedPlotGrid::on_view_changed, index);
        plots_[index] = plot;
        add_cell(plot);
    }
}

void LinkedPlotGrid::set_link_y(bool linked) {
    link_y_ = linked;
    if (!linked) return;
    // Joining takes the first plot's range.
    const gp::PlotRange range = (*plots_[0]).y_range(0U);
    static_cast<void>(y_link_.set_range(range));
    propagating_ = true;
    for (std::size_t index = 1U; index < plot_count; ++index) static_cast<void>((*plots_[index]).set_y_range(0U, range));
    propagating_ = false;
}

void LinkedPlotGrid::on_view_changed(const std::size_t& index, const gf::PlotViewRanges& ranges) {
    if (propagating_) {
        ++echoes_;
        return;
    }
    const gp::PlotRange x{ranges.x_minimum, ranges.x_maximum};
    const gp::PlotRange y{ranges.y_minimum, ranges.y_maximum};
    const bool x_moved = x_link_.set_range(x);
    const bool y_moved = link_y_ && y_link_.set_range(y);
    if (!x_moved && !y_moved) return;
    propagating_ = true;
    for (std::size_t peer = 0U; peer < plot_count; ++peer) {
        if (peer == index) continue;
        if (x_moved && (*plots_[peer]).set_x_range(0U, x)) ++propagations_;
        if (y_moved && (*plots_[peer]).set_y_range(0U, y)) ++propagations_;
    }
    propagating_ = false;
}

} // namespace implot_class
