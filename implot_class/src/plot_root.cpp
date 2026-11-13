#include "plot_root.hpp"

#include "series_data.hpp"

#include <gui_forms/controls/button.hpp>
#include <gui_forms/controls/plot_annotation_item.hpp>
#include <gui_forms/controls/plot_axis_item.hpp>
#include <gui_forms/controls/tab_page.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/plots/scale/scale.hpp>

#include <algorithm>
#include <cstdio>
#include <span>
#include <string_view>
#include <utility>

namespace implot_class {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

constexpr std::array<std::string_view, 5> colormap_names{{"viridis", "magma", "inferno", "plasma", "default"}};
constexpr std::array<gp::BuiltInColormap, 5> colormap_values{{
    gp::BuiltInColormap::viridis,
    gp::BuiltInColormap::magma,
    gp::BuiltInColormap::inferno,
    gp::BuiltInColormap::plasma,
    gp::BuiltInColormap::gui_forms_default,
}};

constexpr std::size_t heatmap_side = 200U;
constexpr std::size_t histogram_samples = 100'000U;
// The signal programs of the Live page (a form names data, not code).
constexpr std::string_view reference_source = "signal(sine, frequency 0.5Hz, amplitude 1)";
constexpr std::string_view noise_source = "signal(noise, amplitude 0.2)";

void append_count(std::string& out, std::uint64_t value) {
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    if (written > 0) out.append(buffer, static_cast<std::size_t>(written));
}

std::shared_ptr<gf::PlotAxisItem> make_axis(const std::string& id, gf::PlotAxisDirection direction,
                                            std::string_view label) {
    std::shared_ptr<gf::PlotAxisItem> axis = gf::make_control<gf::PlotAxisItem>(gf::StableId(id));
    (*axis).direction = direction;
    (*axis).label = std::string(label);
    (*axis).auto_fit = true;
    return axis;
}

std::shared_ptr<gf::PlotSeriesItem> make_series(const std::string& id, gf::PlotSeriesKind kind, std::string_view label) {
    std::shared_ptr<gf::PlotSeriesItem> series = gf::make_control<gf::PlotSeriesItem>(gf::StableId(id));
    (*series).kind = kind;
    (*series).label = std::string(label);
    return series;
}

// A plot with a horizontal and a vertical axis item, both fitting.
std::shared_ptr<gf::PlotView> make_plot(const std::string& id, std::string_view title) {
    std::shared_ptr<gf::PlotView> plot = gf::make_control<gf::PlotView>(gf::StableId(id));
    (*plot).title = std::string(title);
    (*plot).add_child(make_axis(id + ".x", gf::PlotAxisDirection::horizontal, "x"));
    (*plot).add_child(make_axis(id + ".y", gf::PlotAxisDirection::vertical, "y"));
    return plot;
}

} // namespace

PlotRoot::PlotRoot(gf::StableId stable_id, PlotRootOptions options)
    : gf::Control(std::move(stable_id)), options_(options) {
    status_text_.reserve(256U);
}

PlotRoot::PlotRoot(gf::StableId stable_id) : PlotRoot(std::move(stable_id), PlotRootOptions{}) {}

PlotRoot::~PlotRoot() {
    // The producer first: no write may be in flight when the channel's
    // last owner goes.
    feed_.stop();
    // Then the lease, before the arrays it lends (the member order also
    // ensures this; stated here for the reader).
    borrowed_lease_.release();
}

void PlotRoot::initialize_control_tree() {
    tabs_ = gf::make_control<gf::TabControl>(gf::StableId("implot.tabs"));
    status_ = gf::make_control<gf::Label>(gf::StableId("implot.status"));
    add_child(tabs_);
    add_child(status_);
    build_basics();
    build_large();
    build_heatmap();
    build_drag();
    build_live();
    build_subplots();
    gf::on((*tabs_).selected_index_changed(), *this, &PlotRoot::on_page_changed);
    // The Live page starts hidden: its sources wait until it is shown.
    withdraw_live_sources();
    refresh_status();
}

std::shared_ptr<GridPage> PlotRoot::add_page(const char* id, const char* title, std::size_t columns) {
    const std::string base(id);
    std::shared_ptr<gf::TabPage> page = gf::make_control<gf::TabPage>(gf::StableId(base + ".page"), std::string(title));
    std::shared_ptr<GridPage> grid = gf::make_control<GridPage>(gf::StableId(base), columns);
    (*grid).dock = gf::DockStyle::fill;
    (*page).add_child(grid);
    (*tabs_).add_page(page);
    return grid;
}

// -- pages ----------------------------------------------------------------------------------

void PlotRoot::build_basics() {
    std::shared_ptr<GridPage> grid = add_page("implot.basics", "Basics", 3U);

    // Line: two tones, a crosshair and an annotation at the first peak.
    std::shared_ptr<gf::PlotView> line = make_plot("implot.basics.line", "Line");
    (*line).crosshair = true;
    const XYSeries sine = make_sine(500U, 0.02, 1.0, 1.0, 0.0);
    const XYSeries cosine = make_sine(500U, 0.02, 1.0, 0.7, 1.5707963267948966);
    std::shared_ptr<gf::PlotSeriesItem> sine_series = make_series("implot.basics.line.sin", gf::PlotSeriesKind::line, "sin x");
    (*sine_series).set_points(sine.xs, sine.ys);
    std::shared_ptr<gf::PlotSeriesItem> cosine_series = make_series("implot.basics.line.cos", gf::PlotSeriesKind::line, "0.7 cos x");
    (*cosine_series).set_points(cosine.xs, cosine.ys);
    std::shared_ptr<gf::PlotAnnotationItem> peak = gf::make_control<gf::PlotAnnotationItem>(gf::StableId("implot.basics.line.peak"));
    (*peak).text = std::string("first peak");
    (*peak).x = 1.5707963267948966;
    (*peak).y = 1.0;
    (*line).add_child(sine_series);
    (*line).add_child(cosine_series);
    (*line).add_child(peak);

    // Scatter: two clouds.
    std::shared_ptr<gf::PlotView> scatter = make_plot("implot.basics.scatter", "Scatter");
    const XYSeries cloud_a = make_normal_cloud(2000U, 0.0, 0.0, 1.0, 0.6, 3U);
    const XYSeries cloud_b = make_normal_cloud(2000U, 2.5, 1.5, 0.5, 0.9, 4U);
    std::shared_ptr<gf::PlotSeriesItem> a = make_series("implot.basics.scatter.a", gf::PlotSeriesKind::scatter, "cloud A");
    (*a).set_points(cloud_a.xs, cloud_a.ys);
    std::shared_ptr<gf::PlotSeriesItem> b = make_series("implot.basics.scatter.b", gf::PlotSeriesKind::scatter, "cloud B");
    (*b).set_points(cloud_b.xs, cloud_b.ys);
    (*scatter).add_child(a);
    (*scatter).add_child(b);

    // Bars with stems over them.
    std::shared_ptr<gf::PlotView> bars = make_plot("implot.basics.bars", "Bars and stems");
    const std::array<double, 12> monthly{{3.0, 4.5, 6.1, 7.2, 8.8, 9.4, 9.9, 9.1, 7.7, 6.0, 4.2, 3.1}};
    std::array<double, 12> positions{};
    std::array<double, 12> targets{};
    for (std::size_t index = 0U; index < monthly.size(); ++index) {
        positions[index] = static_cast<double>(index);
        targets[index] = monthly[index] * 0.85 + 1.0;
    }
    std::shared_ptr<gf::PlotSeriesItem> bar_series = make_series("implot.basics.bars.values", gf::PlotSeriesKind::bars, "values");
    (*bar_series).set_values(monthly);
    std::shared_ptr<gf::PlotSeriesItem> stem_series = make_series("implot.basics.bars.targets", gf::PlotSeriesKind::stems, "targets");
    (*stem_series).set_points(positions, targets);
    (*bars).add_child(bar_series);
    (*bars).add_child(stem_series);

    // Shaded: the band under a walk, with the walk over it.
    std::shared_ptr<gf::PlotView> shaded = make_plot("implot.basics.shaded", "Shaded");
    const XYSeries walk = make_random_walk(400U, 11U);
    std::shared_ptr<gf::PlotSeriesItem> band = make_series("implot.basics.shaded.band", gf::PlotSeriesKind::shaded, "band");
    (*band).set_points(walk.xs, walk.ys);
    std::shared_ptr<gf::PlotSeriesItem> edge = make_series("implot.basics.shaded.edge", gf::PlotSeriesKind::line, "walk");
    (*edge).set_points(walk.xs, walk.ys);
    (*shaded).add_child(band);
    (*shaded).add_child(edge);

    // Stairs.
    std::shared_ptr<gf::PlotView> stairs = make_plot("implot.basics.stairs", "Stairs");
    const XYSeries steps = make_random_walk(60U, 21U);
    std::shared_ptr<gf::PlotSeriesItem> stair_series = make_series("implot.basics.stairs.walk", gf::PlotSeriesKind::stairs, "steps");
    (*stair_series).set_points(steps.xs, steps.ys);
    (*stairs).add_child(stair_series);

    // A log10 y axis: y = 10^(x / 2) through the primitives' engine_exp10,
    // the same bits on every host.
    std::shared_ptr<gf::PlotView> logarithmic = gf::make_control<gf::PlotView>(gf::StableId("implot.basics.log"));
    (*logarithmic).title = std::string("Log10 axis");
    (*logarithmic).add_child(make_axis("implot.basics.log.x", gf::PlotAxisDirection::horizontal, "x"));
    std::shared_ptr<gf::PlotAxisItem> log_axis = make_axis("implot.basics.log.y", gf::PlotAxisDirection::vertical, "10^(x/2)");
    (*log_axis).scale = gf::PlotAxisScale::log10;
    (*logarithmic).add_child(log_axis);
    std::array<double, 64> log_xs{};
    std::array<double, 64> log_ys{};
    for (std::size_t index = 0U; index < log_xs.size(); ++index) {
        log_xs[index] = static_cast<double>(index) * 0.25;
        log_ys[index] = gp::engine_exp10(log_xs[index] / 2.0);
    }
    std::shared_ptr<gf::PlotSeriesItem> growth = make_series("implot.basics.log.growth", gf::PlotSeriesKind::line, "growth");
    (*growth).set_points(log_xs, log_ys);
    (*logarithmic).add_child(growth);

    basics_ = {line, scatter, bars, shaded, stairs, logarithmic};
    for (const std::shared_ptr<gf::PlotView>& plot : basics_) (*grid).add_cell(plot);
}

void PlotRoot::build_large() {
    std::shared_ptr<GridPage> grid = add_page("implot.large", "Large", 1U);
    const std::size_t count = options_.large_points;

    large_line_ = make_plot("implot.large.line", "Large series (decimated)");
    {
        // Copied in: the item keeps owned columns and, past
        // pyramid_threshold points, an envelope pyramid.
        const XYSeries walk = make_random_walk(count, 7U);
        std::shared_ptr<gf::PlotSeriesItem> owned = make_series("implot.large.line.walk", gf::PlotSeriesKind::line, "random walk (copied)");
        (*owned).set_points(walk.xs, walk.ys);
        (*large_line_).add_child(owned);
    }
    // Read in place: the arrays stay ours, lent under borrowed_lease_.
    borrowed_xs_.resize(count);
    borrowed_ys_.resize(count);
    fill_sine(borrowed_xs_, borrowed_ys_, 0.0, 1.0, 0.0005, 400.0, 0.0);
    gp::SeriesView<double> x_view{};
    x_view.data = reinterpret_cast<const std::byte*>(borrowed_xs_.data());
    x_view.count = borrowed_xs_.size();
    gp::SeriesView<double> y_view{};
    y_view.data = reinterpret_cast<const std::byte*>(borrowed_ys_.data());
    y_view.count = borrowed_ys_.size();
    std::shared_ptr<gf::PlotSeriesItem> lent = make_series("implot.large.line.sine", gf::PlotSeriesKind::line, "sine (borrowed)");
    (*lent).borrow_points(gp::BorrowedSeries<double>(x_view, borrowed_lease_), gp::BorrowedSeries<double>(y_view, borrowed_lease_),
                          gp::XOrder::monotone);
    (*large_line_).add_child(lent);

    large_scatter_ = make_plot("implot.large.scatter", "Large scatter (occupancy)");
    {
        const XYSeries cloud = make_normal_cloud(count, 0.0, 0.0, 1.0, 1.0, 9U);
        std::shared_ptr<gf::PlotSeriesItem> points = make_series("implot.large.scatter.cloud", gf::PlotSeriesKind::scatter, "cloud");
        (*points).set_points(cloud.xs, cloud.ys);
        (*large_scatter_).add_child(points);
    }
    (*grid).add_cell(large_line_);
    (*grid).add_cell(large_scatter_);
}

void PlotRoot::build_heatmap() {
    std::shared_ptr<GridPage> grid = add_page("implot.heatmap", "Heatmap", 2U);
    colormap_box_ = gf::make_control<gf::ComboBox>(gf::StableId("implot.heatmap.colormap"));
    std::vector<std::string> names{};
    names.reserve(colormap_names.size());
    for (const std::string_view name : colormap_names) names.emplace_back(name);
    (*colormap_box_).items = std::move(names);
    (*colormap_box_).selected_index = std::optional<std::size_t>{0U};
    (*colormap_box_).accessible_name = std::string("Colormap");
    bins_bar_ = gf::make_control<gf::TrackBar>(gf::StableId("implot.heatmap.bins"));
    (*bins_bar_).set_range(static_cast<double>(HistogramPlot::minimum_bins), 200.0);
    (*bins_bar_).value = 40.0;
    (*bins_bar_).accessible_name = std::string("Histogram bins");
    (*grid).add_tool(colormap_box_, 140.0);
    (*grid).add_tool(bins_bar_, 220.0);

    heatmap_ = gf::make_control<HeatmapPlot>(gf::StableId("implot.heatmap.map"));
    (*heatmap_).set_title("Heatmap 200 x 200");
    std::vector<double> field(heatmap_side * heatmap_side);
    fill_heatmap(field, heatmap_side, heatmap_side, 5U);
    (*heatmap_).set_values(field, heatmap_side, heatmap_side);

    histogram_ = gf::make_control<HistogramPlot>(gf::StableId("implot.heatmap.histogram"));
    (*histogram_).set_title("Histogram of 100,000 samples");
    std::vector<double> samples(histogram_samples);
    fill_mixture(samples, 13U);
    (*histogram_).set_samples(samples);

    (*grid).add_cell(heatmap_);
    (*grid).add_cell(histogram_);
    gf::on((*colormap_box_).selected_index_changed(), *this, &PlotRoot::on_colormap_chosen);
    gf::on((*bins_bar_).value_changed(), *this, &PlotRoot::on_bins_changed);
}

void PlotRoot::build_drag() {
    std::shared_ptr<GridPage> grid = add_page("implot.drag", "Drag", 1U);
    std::shared_ptr<gf::Button> reset = gf::make_control<gf::Button>(gf::StableId("implot.drag.reset"), "Reset");
    drag_readout_ = gf::make_control<gf::Label>(gf::StableId("implot.drag.readout"));
    (*grid).add_tool(reset, 80.0);
    (*grid).add_tool(drag_readout_, 420.0);
    drag_ = gf::make_control<DragPlot>(gf::StableId("implot.drag.plot"));
    (*drag_).set_title("Drag the points and the dashed lines");
    reset_drag();
    (*grid).add_cell(drag_);
    gf::on((*reset).clicked(), *this, &PlotRoot::on_reset_clicked);
    gf::on((*drag_).handles_moved(), *this, &PlotRoot::on_handles_moved);
}

void PlotRoot::build_live() {
    std::shared_ptr<GridPage> grid = add_page("implot.live", "Live", 1U);
    live_box_ = gf::make_control<gf::CheckBox>(gf::StableId("implot.live.subsurface"), "Draw on a sub-surface");
    std::shared_ptr<gf::Button> pause = gf::make_control<gf::Button>(gf::StableId("implot.live.pause"), "Pause feed");
    (*grid).add_tool(live_box_, 200.0);
    (*grid).add_tool(pause, 110.0);

    live_plot_ = gf::make_control<gf::PlotView>(gf::StableId("implot.live.plot"));
    (*live_plot_).title = std::string("Streaming (follows the newest 10 s)");
    std::shared_ptr<gf::PlotAxisItem> time = make_axis("implot.live.plot.time", gf::PlotAxisDirection::horizontal, "seconds");
    (*time).auto_fit = false;
    (*time).follow_span = 10.0;
    (*time).minimum = 0.0;
    (*time).maximum = 10.0;
    std::shared_ptr<gf::PlotAxisItem> value = make_axis("implot.live.plot.value", gf::PlotAxisDirection::vertical, "value");
    (*value).auto_fit = false;
    (*value).minimum = -1.6;
    (*value).maximum = 1.6;
    (*live_plot_).add_child(time);
    (*live_plot_).add_child(value);
    live_feed_series_ = make_series("implot.live.plot.feed", gf::PlotSeriesKind::line, "feed (producer thread)");
    channel_ = (*live_feed_series_).live_channel(live_capacity);
    live_reference_ = make_series("implot.live.plot.reference", gf::PlotSeriesKind::line, "sine program");
    (*live_reference_).source = std::string(reference_source);
    live_noise_ = make_series("implot.live.plot.noise", gf::PlotSeriesKind::scatter, "noise program");
    (*live_noise_).source = std::string(noise_source);
    (*live_plot_).add_child(live_feed_series_);
    (*live_plot_).add_child(live_reference_);
    (*live_plot_).add_child(live_noise_);
    (*grid).add_cell(live_plot_);
    gf::on((*live_box_).checked_changed(), *this, &PlotRoot::on_live_toggled);
    gf::on((*pause).clicked(), *this, &PlotRoot::on_pause_clicked);
}

void PlotRoot::build_subplots() {
    const std::string base("implot.subplots");
    std::shared_ptr<gf::TabPage> page = gf::make_control<gf::TabPage>(gf::StableId(base + ".page"), std::string("Subplots"));
    subplots_ = gf::make_control<LinkedPlotGrid>(gf::StableId(base));
    (*subplots_).dock = gf::DockStyle::fill;
    link_y_box_ = gf::make_control<gf::CheckBox>(gf::StableId(base + ".link-y"), "Link y axes too");
    (*subplots_).add_tool(link_y_box_, 180.0);
    (*page).add_child(subplots_);
    (*tabs_).add_page(page);
    gf::on((*link_y_box_).checked_changed(), *this, &PlotRoot::on_link_y_toggled);
}

// -- operations ------------------------------------------------------------------------------

void PlotRoot::select_page(PlotPage page) {
    (*tabs_).selected_index = std::optional<std::size_t>{static_cast<std::size_t>(page)};
}

std::optional<PlotPage> PlotRoot::selected_page() const {
    const std::optional<std::size_t> index = (*tabs_).selected_index.read();
    if (!index.has_value() || *index >= plot_page_count) return std::nullopt;
    return static_cast<PlotPage>(*index);
}

void PlotRoot::set_colormap(gp::BuiltInColormap which) {
    (*heatmap_).set_colormap(which);
    ++counters_.colormap_changes;
}

void PlotRoot::set_bin_count(std::size_t count) {
    (*histogram_).set_bin_count(count);
    ++counters_.bin_changes;
}

void PlotRoot::set_live(bool live) {
    if ((*live_plot_).live.read() == live) return;
    (*live_plot_).live = live;
    ++counters_.live_toggles;
    refresh_status();
}

void PlotRoot::reset_drag() {
    const std::array<DataPoint, 6> points{{
        {0.0, 0.2}, {1.0, 0.9}, {2.0, 0.4}, {3.0, 1.3}, {4.0, 0.8}, {5.0, 1.1},
    }};
    DragModel& model = (*drag_).model();
    model.set_points(points);
    model.set_vertical_line(2.5);
    model.set_horizontal_line(0.6);
    (*drag_).model_changed();
    static_cast<void>((*drag_).set_ranges(gp::PlotRange{-0.5, 5.5}, gp::PlotRange{-0.2, 1.6}));
    on_handles_moved();
}

void PlotRoot::withdraw_live_sources() {
    if (live_sources_withdrawn_) return;
    live_sources_withdrawn_ = true;
    // The producer is stopped (joined, its lease released), not only
    // paused: restoring makes a new channel, which needs a new lease.
    feed_.pause(true);
    feed_.stop();
    // Each data call is a whole replacement (plot_series_item.hpp): an
    // empty copied-in series stops the channel's consumption; an empty
    // source stops the program. Whether the plot then leaves the motion
    // clock is what GAPS.md P8 asks to be guaranteed (*unverified*).
    (*live_feed_series_).set_values(std::span<const double>{});
    (*live_reference_).source = std::string{};
    (*live_noise_).source = std::string{};
    ++counters_.sources_withdrawn;
}

void PlotRoot::restore_live_sources() {
    if (!live_sources_withdrawn_) return;
    live_sources_withdrawn_ = false;
    channel_ = (*live_feed_series_).live_channel(live_capacity);
    (*live_reference_).source = std::string(reference_source);
    (*live_noise_).source = std::string(noise_source);
    feed_.pause(false);
    if (options_.start_feed) static_cast<void>(feed_.start(channel_));
    ++counters_.sources_restored;
}

void PlotRoot::refresh_status() {
    status_text_.clear();
    status_text_.append("large series ");
    append_count(status_text_, options_.large_points);
    status_text_.append(" points; feed ");
    const LiveFeedCounters feed = feed_.counters();
    append_count(status_text_, feed.samples_written);
    status_text_.append(" samples written");
    if (channel_ != nullptr) {
        const gp::SeriesChannelStatistics channel = (*channel_).statistics();
        status_text_.append(", ");
        append_count(status_text_, channel.consumed);
        status_text_.append(" consumed, ");
        append_count(status_text_, channel.overrun);
        status_text_.append(" overrun");
    }
    status_text_.append((*live_plot_).live.read() ? "; live sub-surface on" : "; live sub-surface off");
    (*status_).text = status_text_;
    ++counters_.status_updates;
}

// -- events ----------------------------------------------------------------------------------

void PlotRoot::on_page_changed(const gf::TabSelectionChange& change) {
    ++counters_.page_switches;
    const bool live_shown = change.new_index.has_value() && *change.new_index == static_cast<std::size_t>(PlotPage::live);
    if (live_shown) {
        restore_live_sources();
    } else {
        withdraw_live_sources();
    }
    refresh_status();
}

void PlotRoot::on_colormap_chosen(std::optional<std::size_t> index) {
    if (!index.has_value() || *index >= colormap_values.size()) return;
    set_colormap(colormap_values[*index]);
}

void PlotRoot::on_bins_changed(double value) {
    const double rounded = std::clamp(value, 2.0, 512.0);
    set_bin_count(static_cast<std::size_t>(rounded + 0.5));
}

void PlotRoot::on_live_toggled(bool checked) {
    set_live(checked);
}

void PlotRoot::on_link_y_toggled(bool checked) {
    (*subplots_).set_link_y(checked);
}

void PlotRoot::on_reset_clicked(gf::ButtonBase& button) {
    static_cast<void>(button);
    reset_drag();
}

void PlotRoot::on_pause_clicked(gf::ButtonBase& button) {
    static_cast<void>(button);
    feed_.pause(!feed_.paused());
    refresh_status();
}

void PlotRoot::on_handles_moved() {
    ++counters_.handle_moves;
    if (drag_readout_ == nullptr) return;
    const DragModel& model = (*drag_).model();
    char text[96]{};
    const int written = std::snprintf(text, sizeof(text), "vertical line at x = %.3f, horizontal at y = %.3f",
                                      model.vertical_line(), model.horizontal_line());
    if (written > 0) (*drag_readout_).text = std::string(text, static_cast<std::size_t>(written));
}

// -- layout ----------------------------------------------------------------------------------

gf::Size PlotRoot::measure(gf::Size available) {
    return available;
}

void PlotRoot::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    const double tabs_height = std::max(0.0, final_bounds.height - status_height);
    set_child_layout(tabs_, gf::Rect{0.0, 0.0, final_bounds.width, tabs_height});
    set_child_layout(status_, gf::Rect{6.0, tabs_height, std::max(0.0, final_bounds.width - 12.0), status_height});
}

} // namespace implot_class
