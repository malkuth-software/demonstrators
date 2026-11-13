#include "workloads.hpp"

#include "json_writer.hpp"
#include "measurement.hpp"
#include "plot_root.hpp"

#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>
#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/window/window.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace implot_class {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

struct WorkloadName final {
    Workload workload{};
    std::string_view name{};
};

constexpr std::array<WorkloadName, 7> workload_names{{
    {Workload::tour, "tour"},
    {Workload::large_pan, "large-pan"},
    {Workload::heatmap, "heatmap"},
    {Workload::drag, "drag"},
    {Workload::live, "live"},
    {Workload::link, "link"},
    {Workload::idle, "idle"},
}};

constexpr std::size_t maximum_frames = 512U;

// One frame's deltas of the window's public metrics and the counted
// allocations, and its interval.
struct FrameSample final {
    std::uint64_t interval_us{};
    std::uint64_t paint_passes{};
    std::uint64_t controls_painted{};
    std::uint64_t damage_rectangles{};
    std::uint64_t partial_paints{};
    std::uint64_t full_window_paints{};
    double requested_damage_area{};
    double painted_damage_area{};
    std::uint64_t gui_forms_allocations{};
    std::uint64_t gui_forms_allocated_bytes{};
    bool captured{};
};

struct Invariant final {
    std::string_view name{};
    bool held{};
};

// One run: the root, its window, the raster attachment and what the run
// records. Declared in that order so the attachment ends before the
// window, and the window before the root it holds.
class WorkloadRun final {
public:
    WorkloadRun(gf::Application& application, gf::OffscreenRaster& raster, const AppOptions& options)
        : options_(options), root_(make_root(options)),
          window_(application, root_, gf::Size{options.width, options.height}), attached_(raster, window_) {
        frames_.reserve(maximum_frames);
        invariants_.reserve(16U);
        window_.perform_layout();
        previous_metrics_ = window_.metrics_snapshot();
        previous_allocations_ = gf::allocation_statistics();
    }

    [[nodiscard]] PlotRoot& root() noexcept { return *root_; }
    [[nodiscard]] gf::Window& window() noexcept { return window_; }

    void frame() {
        window_.perform_layout();
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        gf::OffscreenCaptureOptions capture_options{};
        const std::optional<gf::WindowCapture> capture = attached_.capture_window(capture_options);
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        const gf::MetricsSnapshot metrics = window_.metrics_snapshot();
        const gf::AllocationStatistics allocations = gf::allocation_statistics();
        if (frames_.size() < maximum_frames) {
            FrameSample sample{};
            const std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            sample.interval_us = static_cast<std::uint64_t>(elapsed.count());
            sample.paint_passes = metrics.paint_passes - previous_metrics_.paint_passes;
            sample.controls_painted = metrics.controls_painted - previous_metrics_.controls_painted;
            sample.damage_rectangles = metrics.damage_rectangles_painted - previous_metrics_.damage_rectangles_painted;
            sample.partial_paints = metrics.partial_paints - previous_metrics_.partial_paints;
            sample.full_window_paints = metrics.full_window_paints - previous_metrics_.full_window_paints;
            sample.requested_damage_area = metrics.requested_damage_area - previous_metrics_.requested_damage_area;
            sample.painted_damage_area = metrics.painted_damage_area - previous_metrics_.painted_damage_area;
            const gf::AllocationCounts now = allocations.total();
            const gf::AllocationCounts before = previous_allocations_.total();
            sample.gui_forms_allocations = now.allocations - before.allocations;
            sample.gui_forms_allocated_bytes = now.allocated_bytes - before.allocated_bytes;
            sample.captured = capture.has_value();
            frames_.push_back(sample);
        }
        previous_metrics_ = metrics;
        previous_allocations_ = allocations;
    }

    void pointer(gf::PointerAction action, gf::Point position, gf::PointerButton button) {
        gf::PointerEvent event{};
        event.action = action;
        event.position = position;
        if (action != gf::PointerAction::move) {
            event.button = button;
            event.click_count = 1U;
        }
        static_cast<void>(window_.dispatch_pointer(event));
    }

    void drag(gf::Point from, gf::Point to, std::size_t steps) {
        pointer(gf::PointerAction::down, from, gf::PointerButton::primary);
        for (std::size_t step = 1U; step <= steps; ++step) {
            const double share = static_cast<double>(step) / static_cast<double>(steps);
            const gf::Point at{from.x + (to.x - from.x) * share, from.y + (to.y - from.y) * share};
            pointer(gf::PointerAction::move, at, gf::PointerButton::primary);
            if (step % 4U == 0U) frame();
        }
        pointer(gf::PointerAction::up, to, gf::PointerButton::primary);
        frame();
    }

    void check(std::string_view name, bool held) {
        invariants_.push_back(Invariant{name, held});
        if (!held) {
            std::fprintf(stderr, "implot-class: invariant failed: %.*s\n", static_cast<int>(name.size()), name.data());
        }
    }

    [[nodiscard]] bool all_held() const noexcept {
        for (const Invariant& invariant : invariants_) {
            if (!invariant.held) return false;
        }
        return true;
    }

    // The frames recorded from `first` on.
    [[nodiscard]] double painted_area_since(std::size_t first) const noexcept {
        double area = 0.0;
        for (std::size_t index = first; index < frames_.size(); ++index) area += frames_[index].painted_damage_area;
        return area;
    }
    [[nodiscard]] double requested_area_since(std::size_t first) const noexcept {
        double area = 0.0;
        for (std::size_t index = first; index < frames_.size(); ++index) area += frames_[index].requested_damage_area;
        return area;
    }
    [[nodiscard]] std::size_t frame_count() const noexcept { return frames_.size(); }

    void write_record(JsonWriter& json, Workload workload) {
        json.begin_object();
        json.key("demonstrator");
        json.value("implot_class");
        json.key("workload");
        json.value(workload_name(workload));
        json.key("inputs");
        json.begin_object();
        json.key("large_points");
        json.value(static_cast<std::uint64_t>(options_.large_points));
        json.key("width");
        json.value(options_.width);
        json.key("height");
        json.value(options_.height);
        json.end_object();
        json.key("counters");
        write_counters(json, read_counters(window_, *root_));
        json.key("frames");
        json.begin_array();
        for (const FrameSample& sample : frames_) {
            json.begin_object();
            json.key("interval_us");
            json.value(sample.interval_us);
            json.key("paint_passes");
            json.value(sample.paint_passes);
            json.key("controls_painted");
            json.value(sample.controls_painted);
            json.key("damage_rectangles");
            json.value(sample.damage_rectangles);
            json.key("partial_paints");
            json.value(sample.partial_paints);
            json.key("full_window_paints");
            json.value(sample.full_window_paints);
            json.key("requested_damage_area");
            json.value(sample.requested_damage_area);
            json.key("painted_damage_area");
            json.value(sample.painted_damage_area);
            json.key("gui_forms_allocations");
            json.value(sample.gui_forms_allocations);
            json.key("gui_forms_allocated_bytes");
            json.value(sample.gui_forms_allocated_bytes);
            json.key("captured");
            json.value(sample.captured);
            json.end_object();
        }
        json.end_array();
        json.key("invariants");
        json.begin_array();
        for (const Invariant& invariant : invariants_) {
            json.begin_object();
            json.key("name");
            json.value(invariant.name);
            json.key("held");
            json.value(invariant.held);
            json.end_object();
        }
        json.end_array();
        json.key("build");
        write_build(json);
        json.end_object();
    }

private:
    static std::shared_ptr<PlotRoot> make_root(const AppOptions& options) {
        PlotRootOptions root_options{};
        root_options.large_points = options.large_points;
        root_options.start_feed = false;
        std::shared_ptr<PlotRoot> root = gf::make_control<PlotRoot>(gf::StableId("implot"), root_options);
        return root;
    }

    const AppOptions& options_;
    std::shared_ptr<PlotRoot> root_;
    gf::Window window_;
    gf::OffscreenAttachment attached_;
    gf::MetricsSnapshot previous_metrics_{};
    gf::AllocationStatistics previous_allocations_{};
    std::vector<FrameSample> frames_{};
    std::vector<Invariant> invariants_{};
};

bool ranges_equal(gp::PlotRange left, gp::PlotRange right) noexcept {
    const double tolerance = 1e-9 * (std::fabs(left.extent()) + 1.0);
    const bool equal = std::fabs(left.minimum - right.minimum) <= tolerance &&
                       std::fabs(left.maximum - right.maximum) <= tolerance;
    return equal;
}

void run_tour(WorkloadRun& run) {
    for (std::size_t page = 0U; page < plot_page_count; ++page) {
        run.root().select_page(static_cast<PlotPage>(page));
        run.frame();
        run.frame();
    }
    run.check("every page was shown", run.root().counters().page_switches >= plot_page_count - 1U);
    run.check("the heatmap painted its cells", run.root().heatmap().statistics().points_drawn > 0U);
    run.check("the histogram painted its bins", run.root().histogram().statistics().points_drawn > 0U);
    run.check("the drag plot painted", run.root().drag().statistics().frames_painted > 0U);
    run.check("leaving the Live page withdrew its sources", run.root().counters().sources_withdrawn >= 2U);
}

void run_large_pan(WorkloadRun& run) {
    run.root().select_page(PlotPage::large);
    run.frame();
    gf::PlotView& plot = run.root().large_line();
    const double count = static_cast<double>(run.root().large_points());
    // Zoom to a tenth, then pan across in 120 steps.
    const double span = count / 10.0;
    std::size_t refused = 0U;
    for (std::size_t step = 0U; step < 120U; ++step) {
        const double start = (count - span) * static_cast<double>(step) / 119.0;
        if (!plot.set_x_range(0U, gp::PlotRange{start, start + span})) ++refused;
        run.frame();
    }
    run.check("every pan range was taken", refused == 0U);
    run.check("the last range is the end of the series", ranges_equal(plot.x_range(0U), gp::PlotRange{count - span, count}));
    // Damage: each pan repaints the plot, not the whole window (the tab
    // strip and the status line stay).
    run.check("pan frames repainted less than the whole window",
              run.painted_area_since(1U) < 120.0 * run.window().client_size().width * run.window().client_size().height);
}

void run_heatmap(WorkloadRun& run) {
    run.root().select_page(PlotPage::heatmap);
    run.frame();
    const std::array<gp::BuiltInColormap, 4> maps{{gp::BuiltInColormap::magma, gp::BuiltInColormap::inferno,
                                                   gp::BuiltInColormap::plasma, gp::BuiltInColormap::viridis}};
    for (const gp::BuiltInColormap map : maps) {
        run.root().set_colormap(map);
        run.frame();
    }
    for (std::size_t bins = 10U; bins <= 200U; bins += 10U) {
        run.root().set_bin_count(bins);
        run.frame();
    }
    HeatmapPlot& heatmap = run.root().heatmap();
    const gf::Rect plot = heatmap.plot_rect();
    const gf::Point centre = heatmap.point_to_window(gf::Point{plot.x + plot.width / 2.0, plot.y + plot.height / 2.0});
    run.pointer(gf::PointerAction::move, centre, gf::PointerButton::none);
    run.frame();
    run.check("every bin count rebinned", run.root().histogram().rebins() >= 20U);
    run.check("hovering the heatmap names a cell", heatmap.hovered_cell().has_value());
    run.check("one heatmap frame draws at most one rectangle per cell",
              heatmap.statistics().last_frame_points_drawn <= heatmap.rows() * heatmap.columns() + 64U);
}

void run_drag(WorkloadRun& run) {
    run.root().select_page(PlotPage::drag);
    run.frame();
    DragPlot& plot = run.root().drag();
    if (!plot.transform_valid()) {
        run.check("the drag plot is laid out", false);
        return;
    }
    const DataPoint first = plot.model().points()[1];
    const gf::Point device = plot.transform().to_device(first.x, first.y);
    const gf::Point from = plot.point_to_window(device);
    run.drag(from, gf::Point{from.x + 30.0, from.y + 40.0}, 16U);
    const DataPoint moved = plot.model().points()[1];
    run.check("dragging a point moved it", moved.y < first.y);
    const gf::Point line_device = plot.transform().to_device(plot.model().vertical_line(), 0.0);
    const gf::Point line_from = plot.point_to_window(gf::Point{line_device.x, plot.plot_rect().y + 12.0});
    const double before = plot.model().vertical_line();
    run.drag(line_from, gf::Point{line_from.x + 50.0, line_from.y}, 12U);
    run.check("dragging the vertical line moved it", plot.model().vertical_line() > before);
    run.check("handle moves were reported", run.root().counters().handle_moves > 2U);
}

void run_live(WorkloadRun& run) {
    run.root().select_page(PlotPage::live);
    run.frame();
    gf::PlotView& plot = run.root().live_plot();
    const gf::FrameTime start = gf::FrameClock::now();
    // 120 frames at 60 Hz: two blocks (64 samples, 32 ms of feed) per
    // frame, then the plot's ambient frame at the frame's time.
    for (std::size_t step = 0U; step < 120U; ++step) {
        static_cast<void>(run.root().feed().write_blocks(run.root().live_channel(), 1U));
        const gf::FrameTime now = start + std::chrono::microseconds(16667 * static_cast<std::int64_t>(step + 1U));
        static_cast<void>(plot.on_ambient_frame(now));
        run.frame();
    }
    const gp::SeriesChannelStatistics channel = (*run.root().live_channel()).statistics();
    run.check("the plot consumed the feed", channel.consumed > 0U);
    run.check("nothing was overrun", channel.overrun == 0U);
    run.check("the plot ran its ambient frames", plot.frames_run() > 0U);
    // Leaving the page withdraws the sources; the feed then writes nothing.
    run.root().select_page(PlotPage::basics);
    run.frame();
    run.check("leaving the page paused the feed", run.root().feed().paused());
}

void run_link(WorkloadRun& run) {
    run.root().select_page(PlotPage::subplots);
    run.frame();
    LinkedPlotGrid& grid = run.root().subplots();
    for (std::size_t step = 0U; step < 30U; ++step) {
        const double start = 0.2 * static_cast<double>(step);
        static_cast<void>(grid.plot(step % LinkedPlotGrid::plot_count).set_x_range(0U, gp::PlotRange{start, start + 4.0}));
        run.frame();
    }
    const gp::PlotRange reference = grid.plot(0U).x_range(0U);
    bool linked = true;
    for (std::size_t index = 1U; index < LinkedPlotGrid::plot_count; ++index) {
        if (!ranges_equal(grid.plot(index).x_range(0U), reference)) linked = false;
    }
    run.check("every subplot shows the linked x range", linked);
    run.check("the page set the peers' ranges", grid.propagations() > 0U);
    run.check("echoes were ignored, not propagated", grid.echoes() > 0U);
    grid.set_link_y(true);
    static_cast<void>(grid.plot(2U).set_y_range(0U, gp::PlotRange{-3.0, 3.0}));
    run.frame();
    run.check("linked y follows", ranges_equal(grid.plot(0U).y_range(0U), gp::PlotRange{-3.0, 3.0}));
}

void run_idle(WorkloadRun& run) {
    run.root().select_page(PlotPage::basics);
    run.frame();
    run.frame();
    const std::size_t first = run.frame_count();
    for (std::size_t step = 0U; step < 60U; ++step) run.frame();
    run.check("an unchanged window requests no damage", run.requested_area_since(first) == 0.0);
}

} // namespace

std::optional<Workload> workload_named(std::string_view name) noexcept {
    for (const WorkloadName& entry : workload_names) {
        if (entry.name == name) return entry.workload;
    }
    return std::nullopt;
}

std::string_view workload_name(Workload workload) noexcept {
    for (const WorkloadName& entry : workload_names) {
        if (entry.workload == workload) return entry.name;
    }
    return "unknown";
}

int run_workload(gf::Application& application, const AppOptions& options, Workload workload) {
    gf::OffscreenRasterOptions raster_options{};
    if (!options.font_dir.empty()) raster_options.font_directory = options.font_dir.string();
    gf::OffscreenRasterCreateResult made = gf::OffscreenRaster::create(raster_options);
    std::unique_ptr<gf::OffscreenRaster> raster = made.take_raster();
    if (raster == nullptr) {
        std::fprintf(stderr, "implot-class: the offscreen raster could not be made (try --font-dir)\n");
        return 1;
    }
    JsonWriter json{};
    bool held = false;
    {
        WorkloadRun run(application, *raster, options);
        switch (workload) {
        case Workload::tour:
            run_tour(run);
            break;
        case Workload::large_pan:
            run_large_pan(run);
            break;
        case Workload::heatmap:
            run_heatmap(run);
            break;
        case Workload::drag:
            run_drag(run);
            break;
        case Workload::live:
            run_live(run);
            break;
        case Workload::link:
            run_link(run);
            break;
        case Workload::idle:
        default:
            run_idle(run);
            break;
        }
        run.write_record(json, workload);
        held = run.all_held();
    }
    const std::filesystem::path path = record_path(options.output_dir, host_kind(), workload_name(workload));
    if (!write_record_file(path, json.text())) {
        std::fprintf(stderr, "implot-class: could not write %s\n", path.string().c_str());
        return 1;
    }
    std::printf("implot-class: %s written\n", path.string().c_str());
    const int code = held ? 0 : 1;
    return code;
}

} // namespace implot_class
