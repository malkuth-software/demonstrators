#include "workloads.hpp"

#include "json_writer.hpp"
#include "measurement.hpp"
#include "demo_root.hpp"

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

namespace imgui_class {

namespace gf = gui_forms;

namespace {

struct WorkloadName final {
    Workload workload{};
    std::string_view name{};
};

constexpr std::array<WorkloadName, 7> workload_names{{
    {Workload::tour, "tour"},
    {Workload::widgets, "widgets"},
    {Workload::table, "table"},
    {Workload::scrub, "scrub"},
    {Workload::colour, "colour"},
    {Workload::style, "style"},
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

    [[nodiscard]] DemoRoot& root() noexcept { return *root_; }
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
            std::fprintf(stderr, "imgui-class: invariant failed: %.*s\n", static_cast<int>(name.size()), name.data());
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
        json.value("imgui_class");
        json.key("workload");
        json.value(workload_name(workload));
        json.key("inputs");
        json.begin_object();
        json.key("rows");
        json.value(static_cast<std::uint64_t>(options_.rows));
        json.key("seed");
        json.value(options_.seed);
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
    static std::shared_ptr<DemoRoot> make_root(const AppOptions& options) {
        DemoRootOptions root_options{};
        root_options.rows = options.rows;
        root_options.seed = options.seed;
        std::shared_ptr<DemoRoot> root = gf::make_control<DemoRoot>(gf::StableId("imgui"), root_options);
        return root;
    }

    const AppOptions& options_;
    std::shared_ptr<DemoRoot> root_;
    gf::Window window_;
    gf::OffscreenAttachment attached_;
    gf::MetricsSnapshot previous_metrics_{};
    gf::AllocationStatistics previous_allocations_{};
    std::vector<FrameSample> frames_{};
    std::vector<Invariant> invariants_{};
};

// The centre of `control` in window coordinates.
gf::Point centre_of(gf::Control& control) {
    const gf::Rect local = control.client_rectangle();
    const gf::Point centre = control.point_to_window(gf::Point{local.x + local.width / 2.0, local.y + local.height / 2.0});
    return centre;
}

void click(WorkloadRun& run, gf::Point at) {
    run.pointer(gf::PointerAction::move, at, gf::PointerButton::none);
    run.pointer(gf::PointerAction::down, at, gf::PointerButton::primary);
    run.pointer(gf::PointerAction::up, at, gf::PointerButton::primary);
    run.frame();
}

void run_tour(WorkloadRun& run) {
    for (std::size_t page = 0U; page < demo_page_count; ++page) {
        run.root().select_page(static_cast<DemoPage>(page));
        run.frame();
        run.frame();
    }
    const DemoRootCounters counters = run.root().counters();
    run.check("every page was shown", counters.page_switches >= demo_page_count - 1U);
    run.check("the table drew its cell plots", counters.cell_plots_painted > 0U);
    run.check("the table drew its value bars", counters.cell_bars_painted > 0U);
    run.check("the colour field painted", run.root().color_field().statistics().frames_painted > 0U);
    run.check("the tree holds the model's nodes", run.root().tree().items().size() == run.root().model().tree().size());
}

void run_widgets(WorkloadRun& run) {
    run.root().select_page(DemoPage::widgets);
    run.frame();
    const gf::Point button = centre_of(run.root().counter_button());
    for (std::size_t step = 0U; step < 5U; ++step) click(run, button);
    run.check("five clicks counted", run.root().counters().button_clicks == 5U);
    for (std::size_t effect = 0U; effect < DemoRoot::effect_count; ++effect) {
        run.root().set_effect(effect);
        run.frame();
    }
    run.check("every effect was set", run.root().counters().effect_changes >= DemoRoot::effect_count);
}

void run_table(WorkloadRun& run) {
    run.root().select_page(DemoPage::tables);
    run.frame();
    const std::size_t all = run.root().visible_rows();
    run.root().set_filter("ka");
    run.frame();
    const std::size_t filtered = run.root().visible_rows();
    run.root().set_sort(TableColumn::value, false);
    run.frame();
    const std::uint64_t plots_before = run.root().counters().cell_plots_painted;
    for (std::size_t step = 0U; step < 30U; ++step) {
        run.root().advance();
        run.frame();
    }
    const DemoRootCounters counters = run.root().counters();
    run.check("the filter kept a subset", filtered <= all);
    run.check("thirty advances", counters.advances == 30U && run.root().model().advances() == 30U);
    run.check("advancing repainted the cell plots", counters.cell_plots_painted > plots_before);
    run.check("each cell plot drew its 32 samples", counters.cell_plot_points == counters.cell_plots_painted * history_length);
    run.root().set_filter(std::string{});
    run.frame();
    run.check("clearing the filter shows every row", run.root().visible_rows() == all);
}

void run_scrub(WorkloadRun& run) {
    run.root().select_page(DemoPage::values);
    run.frame();
    gf::ValueBar& bar = run.root().plain_bar();
    const double before = bar.value.read();
    const gf::Point from = centre_of(bar);
    run.drag(from, gf::Point{from.x + 80.0, from.y}, 16U);
    run.check("scrubbing the bar changed its value", bar.value.read() != before);
    run.check("the scrub's release committed", run.root().counters().value_commits >= 1U);
    VectorField& vector = run.root().vector_field();
    const Vector3 start = vector.vector();
    const gf::Point x_bar = centre_of(vector.bar(0U));
    run.drag(x_bar, gf::Point{x_bar.x - 40.0, x_bar.y}, 8U);
    run.check("scrubbing x changed the vector", !(vector.vector() == start));
    run.check("the vector field reported its changes", run.root().counters().vector_changes > 0U);
}

void run_colour(WorkloadRun& run) {
    run.root().select_page(DemoPage::colour);
    run.frame();
    ColorField& field = run.root().color_field();
    const gf::Rect square = field.square();
    const gf::Point top_left = field.point_to_window(gf::Point{square.x + 2.0, square.y + 2.0});
    const gf::Point bottom_right = field.point_to_window(gf::Point{square.x + square.width - 2.0, square.y + square.height - 2.0});
    run.drag(top_left, bottom_right, 16U);
    const Hsva dragged = field.color();
    run.check("the drag ended at full saturation", dragged.saturation > 0.95);
    run.check("the drag ended near black", dragged.value < 0.05);
    const gf::Rect hue = field.hue_bar();
    const gf::Point hue_top = field.point_to_window(gf::Point{hue.x + hue.width / 2.0, hue.y + 1.0});
    run.drag(hue_top, gf::Point{hue_top.x, hue_top.y + hue.height / 3.0}, 8U);
    run.check("the hue bar moved the hue", std::fabs(field.color().hue - 1.0 / 3.0) < 0.05);
    const gf::Color shown = run.root().color_editor().value.read();
    run.check("the hex editor follows the field", shown == hsv_color(field.color()));
    run.root().set_color(Hsva{0.0, 1.0, 1.0, 0.5});
    run.frame();
    run.check("set_color reaches the editor", run.root().color_editor().value.read() == hsv_color(Hsva{0.0, 1.0, 1.0, 0.5}));
}

void run_style(WorkloadRun& run) {
    run.root().select_page(DemoPage::style);
    run.frame();
    for (std::size_t step = 0U; step < 6U; ++step) {
        StyleChoice choice{};
        choice.accent_hue = static_cast<double>(step) / 6.0;
        choice.corner_radius = 2.0 * static_cast<double>(step);
        choice.dark = step % 2U == 1U;
        run.root().set_style(choice);
        run.frame();
    }
    const DemoRootCounters counters = run.root().counters();
    run.check("every choice compiled", counters.style_compiles >= 6U);
    run.check("no generated sheet was refused", counters.style_refusals == 0U);
    run.check("the preview has the theme override", run.root().style_applied());
    const bool refused = !run.root().apply_style_sheet("button::part(face) { background-color: ; }");
    run.frame();
    run.check("a broken hand-written sheet is refused", refused);
    run.check("the refusal kept the previous theme", run.root().style_applied());
}

void run_idle(WorkloadRun& run) {
    run.root().select_page(DemoPage::trees);
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
        std::fprintf(stderr, "imgui-class: the offscreen raster could not be made (try --font-dir)\n");
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
        case Workload::widgets:
            run_widgets(run);
            break;
        case Workload::table:
            run_table(run);
            break;
        case Workload::scrub:
            run_scrub(run);
            break;
        case Workload::colour:
            run_colour(run);
            break;
        case Workload::style:
            run_style(run);
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
        std::fprintf(stderr, "imgui-class: could not write %s\n", path.string().c_str());
        return 1;
    }
    std::printf("imgui-class: %s written\n", path.string().c_str());
    const int code = held ? 0 : 1;
    return code;
}

} // namespace imgui_class
