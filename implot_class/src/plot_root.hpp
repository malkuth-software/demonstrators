#pragma once

// The ImPlot-class window's content (D4): a TabControl of six pages, each
// a GridPage of plots:
//
//   Basics     PlotView kinds: line (with an annotation and crosshair),
//              scatter, bars with stems, shaded, stairs, a log10 axis
//   Large      a 1,000,000-point random walk copied in (envelope pyramid)
//              beside a 1,000,000-point sine read in place under a
//              SeriesLease; a 1,000,000-point scatter (occupancy)
//   Heatmap    HeatmapPlot (200 x 200, colormap chooser) and HistogramPlot
//              (100,000 samples, bin count on a TrackBar)
//   Drag       DragPlot: drag points along a curve and two lines
//   Live       a streaming PlotView fed by LiveFeed's producer thread
//              through a ring channel, beside two signal-program series;
//              the sub-surface (`live`) switched by a CheckBox
//   Subplots   LinkedPlotGrid: four PlotViews with linked x (and, on
//              request, y) axes
//
// Ownership: PlotRoot is a control (shared, owned by its window). It owns
// the large series' storage and the lease that lends it, declared in that
// order so the lease is released (destroyed) first; the feed, declared
// after the controls, is stopped in the destructor before anything else
// goes. The pages and plots are children.
//
// Hidden pages: a PlotView with a channel or a signal program keeps the
// motion clock running while attached, whether or not its tab shows
// (GAPS.md P8), so the Live page withdraws its sources when another page
// is selected and restores them when it is shown again.
//
// One UI thread, except the feed's producer (live_feed.hpp).

#include "drag_plot.hpp"
#include "grid_page.hpp"
#include "heatmap_plot.hpp"
#include "histogram_plot.hpp"
#include "linked_axes.hpp"
#include "live_feed.hpp"

#include <gui_forms/control.hpp>
#include <gui_forms/controls/button_base.hpp>
#include <gui_forms/controls/check_box.hpp>
#include <gui_forms/controls/combo_box.hpp>
#include <gui_forms/controls/label.hpp>
#include <gui_forms/controls/plot_series_item.hpp>
#include <gui_forms/controls/plot_view.hpp>
#include <gui_forms/controls/tab_control.hpp>
#include <gui_forms/controls/track_bar.hpp>
#include <gui_forms/plots/series_view/series_view.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace implot_class {

enum class PlotPage : std::uint8_t { basics, large, heatmap, drag, live, subplots };
inline constexpr std::size_t plot_page_count = 6U;

struct PlotRootOptions final {
    // Points in each large series.
    std::size_t large_points{1'000'000U};
    // Start LiveFeed's thread when the Live page is first shown (off for
    // headless workloads, which write blocks themselves).
    bool start_feed{true};
};

struct PlotRootCounters final {
    std::uint64_t page_switches{};
    std::uint64_t colormap_changes{};
    std::uint64_t bin_changes{};
    std::uint64_t live_toggles{};
    std::uint64_t sources_withdrawn{};
    std::uint64_t sources_restored{};
    std::uint64_t handle_moves{};
    std::uint64_t status_updates{};
};

class PlotRoot final : public gui_forms::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    static constexpr double status_height = 22.0;
    static constexpr std::size_t live_capacity = 65536U;

    PlotRoot(gui_forms::StableId stable_id, PlotRootOptions options);
    explicit PlotRoot(gui_forms::StableId stable_id);
    ~PlotRoot() override;
    PlotRoot(const PlotRoot&) = delete;
    PlotRoot& operator=(const PlotRoot&) = delete;
    void initialize_control_tree();

    // -- parts --------------------------------------------------------------------------
    [[nodiscard]] gui_forms::TabControl& tabs() const noexcept { return *tabs_; }
    void select_page(PlotPage page);
    [[nodiscard]] std::optional<PlotPage> selected_page() const;
    [[nodiscard]] gui_forms::PlotView& basics(std::size_t index) const { return *basics_.at(index); }
    [[nodiscard]] gui_forms::PlotView& large_line() const noexcept { return *large_line_; }
    [[nodiscard]] gui_forms::PlotView& large_scatter() const noexcept { return *large_scatter_; }
    [[nodiscard]] HeatmapPlot& heatmap() const noexcept { return *heatmap_; }
    [[nodiscard]] HistogramPlot& histogram() const noexcept { return *histogram_; }
    [[nodiscard]] DragPlot& drag() const noexcept { return *drag_; }
    [[nodiscard]] gui_forms::PlotView& live_plot() const noexcept { return *live_plot_; }
    [[nodiscard]] LinkedPlotGrid& subplots() const noexcept { return *subplots_; }
    [[nodiscard]] LiveFeed& feed() noexcept { return feed_; }
    [[nodiscard]] const std::shared_ptr<gui_forms::plots::SeriesRingChannel>& live_channel() const& noexcept {
        return channel_;
    }
    const std::shared_ptr<gui_forms::plots::SeriesRingChannel>& live_channel() const&& = delete;
    [[nodiscard]] PlotRootCounters counters() const noexcept { return counters_; }
    [[nodiscard]] std::size_t large_points() const noexcept { return options_.large_points; }

    // -- operations (the toolbars, tests and workloads) -----------------------------------
    void set_colormap(gui_forms::plots::BuiltInColormap which);
    void set_bin_count(std::size_t count);
    void set_live(bool live);
    void reset_drag();
    void refresh_status();

    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;

private:
    void build_basics();
    void build_large();
    void build_heatmap();
    void build_drag();
    void build_live();
    void build_subplots();
    std::shared_ptr<GridPage> add_page(const char* id, const char* title, std::size_t columns);
    void withdraw_live_sources();
    void restore_live_sources();

    void on_page_changed(const gui_forms::TabSelectionChange& change);
    void on_colormap_chosen(std::optional<std::size_t> index);
    void on_bins_changed(double value);
    void on_live_toggled(bool checked);
    void on_link_y_toggled(bool checked);
    void on_handles_moved();
    void on_reset_clicked(gui_forms::ButtonBase& button);
    void on_pause_clicked(gui_forms::ButtonBase& button);

    PlotRootOptions options_{};
    // The borrowed sine's storage, then its lease: the lease is destroyed
    // (released) first, so no plot reads the arrays after they go.
    std::vector<double> borrowed_xs_{};
    std::vector<double> borrowed_ys_{};
    gui_forms::plots::SeriesLease borrowed_lease_{};
    std::shared_ptr<gui_forms::TabControl> tabs_{};
    std::shared_ptr<gui_forms::Label> status_{};
    std::array<std::shared_ptr<gui_forms::PlotView>, 6> basics_{};
    std::shared_ptr<gui_forms::PlotView> large_line_{};
    std::shared_ptr<gui_forms::PlotView> large_scatter_{};
    std::shared_ptr<HeatmapPlot> heatmap_{};
    std::shared_ptr<HistogramPlot> histogram_{};
    std::shared_ptr<gui_forms::ComboBox> colormap_box_{};
    std::shared_ptr<gui_forms::TrackBar> bins_bar_{};
    std::shared_ptr<DragPlot> drag_{};
    std::shared_ptr<gui_forms::Label> drag_readout_{};
    std::shared_ptr<gui_forms::PlotView> live_plot_{};
    std::shared_ptr<gui_forms::PlotSeriesItem> live_feed_series_{};
    std::shared_ptr<gui_forms::PlotSeriesItem> live_reference_{};
    std::shared_ptr<gui_forms::PlotSeriesItem> live_noise_{};
    std::shared_ptr<gui_forms::CheckBox> live_box_{};
    std::shared_ptr<LinkedPlotGrid> subplots_{};
    std::shared_ptr<gui_forms::CheckBox> link_y_box_{};
    std::shared_ptr<gui_forms::plots::SeriesRingChannel> channel_{};
    bool live_sources_withdrawn_{};
    std::string status_text_{};
    PlotRootCounters counters_{};
    // Declared last, stopped first (in the destructor).
    LiveFeed feed_{};
};

} // namespace implot_class
