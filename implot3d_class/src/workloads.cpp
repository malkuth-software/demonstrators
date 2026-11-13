#include "workloads.hpp"

#include "json_writer.hpp"
#include "measurement.hpp"
#include "plot3d_root.hpp"

#include <gui_forms/allocation.hpp>
#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>
#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/window/window.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace implot3d_class {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

struct WorkloadName final {
    Workload workload{};
    std::string_view name{};
};

constexpr std::array<WorkloadName, 8> workload_names{{
    {Workload::tour, "tour"},
    {Workload::orbit, "orbit"},
    {Workload::zoom, "zoom"},
    {Workload::pick, "pick"},
    {Workload::strides, "strides"},
    {Workload::interpolation, "interpolation"},
    {Workload::resolution, "resolution"},
    {Workload::idle, "idle"},
}};

constexpr std::size_t maximum_frames = 512U;
constexpr std::size_t maximum_steps = 64U;

// One frame's deltas of the window's public metrics, the plot's pictures
// and the counted allocations, and its capture interval.
struct FrameSample final {
    std::uint64_t interval_us{};
    std::uint64_t paint_passes{};
    std::uint64_t controls_painted{};
    std::uint64_t damage_rectangles{};
    std::uint64_t partial_paints{};
    std::uint64_t full_window_paints{};
    double requested_damage_area{};
    double painted_damage_area{};
    std::uint64_t picture_renders{};
    std::uint64_t gui_forms_allocations{};
    std::uint64_t gui_forms_allocated_bytes{};
    bool captured{};
};

// A named point in a workload (a stride, a grid size, a scene): the plot's
// figures after its frame, and that frame's capture interval.
struct StepSample final {
    std::string label{};
    SceneSnapshot scene{};
    std::uint64_t interval_us{};
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
        steps_.reserve(maximum_steps);
        invariants_.reserve(24U);
        window_.perform_layout();
        previous_metrics_ = window_.metrics_snapshot();
        previous_allocations_ = gf::allocation_statistics();
        previous_renders_ = (*root_).plot().picture_renders();
    }

    [[nodiscard]] Plot3DRoot& root() noexcept { return *root_; }
    [[nodiscard]] gf::Window& window() noexcept { return window_; }
    [[nodiscard]] gf::Plot3DView& plot() noexcept { return (*root_).plot(); }

    void frame() {
        window_.perform_layout();
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        gf::OffscreenCaptureOptions capture_options{};
        const std::optional<gf::WindowCapture> capture = attached_.capture_window(capture_options);
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        const gf::MetricsSnapshot metrics = window_.metrics_snapshot();
        const gf::AllocationStatistics allocations = gf::allocation_statistics();
        const std::uint64_t renders = plot().picture_renders();
        const std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        last_interval_us_ = static_cast<std::uint64_t>(elapsed.count());
        if (frames_.size() < maximum_frames) {
            FrameSample sample{};
            sample.interval_us = last_interval_us_;
            sample.paint_passes = metrics.paint_passes - previous_metrics_.paint_passes;
            sample.controls_painted = metrics.controls_painted - previous_metrics_.controls_painted;
            sample.damage_rectangles = metrics.damage_rectangles_painted - previous_metrics_.damage_rectangles_painted;
            sample.partial_paints = metrics.partial_paints - previous_metrics_.partial_paints;
            sample.full_window_paints = metrics.full_window_paints - previous_metrics_.full_window_paints;
            sample.requested_damage_area = metrics.requested_damage_area - previous_metrics_.requested_damage_area;
            sample.painted_damage_area = metrics.painted_damage_area - previous_metrics_.painted_damage_area;
            sample.picture_renders = renders - previous_renders_;
            const gf::AllocationCounts now = allocations.total();
            const gf::AllocationCounts before = previous_allocations_.total();
            sample.gui_forms_allocations = now.allocations - before.allocations;
            sample.gui_forms_allocated_bytes = now.allocated_bytes - before.allocated_bytes;
            sample.captured = capture.has_value();
            frames_.push_back(sample);
        }
        previous_metrics_ = metrics;
        previous_allocations_ = allocations;
        previous_renders_ = renders;
    }

    // Records the plot's figures after the last frame under `label`.
    void step(std::string label) {
        if (steps_.size() >= maximum_steps) return;
        StepSample sample{};
        sample.label = std::move(label);
        sample.scene = read_scene(plot());
        sample.interval_us = last_interval_us_;
        steps_.push_back(std::move(sample));
    }

    // The plot rectangle's centre, in window coordinates.
    [[nodiscard]] gf::Point plot_centre() {
        const gf::Rect bounds = plot().plot_bounds();
        const gf::Point local{bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0};
        const gf::Point centre = plot().point_to_window(local);
        return centre;
    }

    void pointer(gf::PointerAction action, gf::Point position, gf::PointerButton button, gf::Modifier modifiers) {
        gf::PointerEvent event{};
        event.action = action;
        event.position = position;
        event.modifiers = modifiers;
        if (action == gf::PointerAction::down || action == gf::PointerAction::up) {
            event.button = button;
            event.click_count = 1U;
        } else {
            // A move reports the held button, as the hosts do.
            event.button = button;
            event.click_count = 0U;
        }
        static_cast<void>(window_.dispatch_pointer(event));
    }

    void wheel(gf::Point position, double notches) {
        gf::PointerEvent event{};
        event.action = gf::PointerAction::wheel;
        event.position = position;
        event.wheel_delta = gf::Point{0.0, notches};
        event.wheel_unit = gf::WheelDeltaUnit::lines;
        event.click_count = 0U;
        static_cast<void>(window_.dispatch_pointer(event));
    }

    bool key(std::uint32_t physical_key, gf::Modifier modifiers) {
        gf::KeyEvent event{};
        event.action = gf::KeyAction::down;
        event.physical_key = physical_key;
        event.modifiers = modifiers;
        const bool handled = window_.dispatch_key(event);
        return handled;
    }

    // A drag in `steps` moves with a frame every fourth, then the release
    // and a frame.
    void drag(gf::Point from, gf::Point to, std::size_t steps, gf::PointerButton button, gf::Modifier modifiers) {
        pointer(gf::PointerAction::down, from, button, modifiers);
        for (std::size_t index = 1U; index <= steps; ++index) {
            const double share = static_cast<double>(index) / static_cast<double>(steps);
            const gf::Point at{from.x + (to.x - from.x) * share, from.y + (to.y - from.y) * share};
            pointer(gf::PointerAction::move, at, button, modifiers);
            if (index % 4U == 0U) frame();
        }
        pointer(gf::PointerAction::up, to, button, modifiers);
        frame();
    }

    void check(std::string_view name, bool held) {
        invariants_.push_back(Invariant{name, held});
        if (!held) {
            std::fprintf(stderr, "implot3d-class: invariant failed: %.*s\n", static_cast<int>(name.size()), name.data());
        }
    }

    [[nodiscard]] bool all_held() const noexcept {
        for (const Invariant& invariant : invariants_) {
            if (!invariant.held) return false;
        }
        return true;
    }

    [[nodiscard]] std::size_t frame_count() const noexcept { return frames_.size(); }
    [[nodiscard]] const std::vector<StepSample>& steps() const& noexcept { return steps_; }
    const std::vector<StepSample>& steps() const&& = delete;

    // Sums over the frames recorded from `first` on.
    [[nodiscard]] double requested_area_since(std::size_t first) const noexcept {
        double area = 0.0;
        for (std::size_t index = first; index < frames_.size(); ++index) area += frames_[index].requested_damage_area;
        return area;
    }
    [[nodiscard]] std::uint64_t renders_since(std::size_t first) const noexcept {
        std::uint64_t renders = 0U;
        for (std::size_t index = first; index < frames_.size(); ++index) renders += frames_[index].picture_renders;
        return renders;
    }
    // The most pictures any one frame from `first` on rendered.
    [[nodiscard]] std::uint64_t most_renders_per_frame_since(std::size_t first) const noexcept {
        std::uint64_t most = 0U;
        for (std::size_t index = first; index < frames_.size(); ++index) {
            if (frames_[index].picture_renders > most) most = frames_[index].picture_renders;
        }
        return most;
    }

    void write_record(JsonWriter& json, Workload workload) {
        json.begin_object();
        json.key("demonstrator");
        json.value("implot3d_class");
        json.key("workload");
        json.value(workload_name(workload));
        json.key("inputs");
        json.begin_object();
        json.key("grid_side");
        json.value(static_cast<std::uint64_t>(options_.grid_side));
        json.key("width");
        json.value(options_.width);
        json.key("height");
        json.value(options_.height);
        json.end_object();
        json.key("counters");
        write_counters(json, read_counters(window_, *root_));
        json.key("steps");
        json.begin_array();
        for (const StepSample& sample : steps_) {
            json.begin_object();
            json.key("label");
            json.value(sample.label);
            json.key("interval_us");
            json.value(sample.interval_us);
            json.key("scene");
            write_scene(json, sample.scene);
            json.end_object();
        }
        json.end_array();
        json.key("frames");
        json.begin_array();
        for (const FrameSample& sample : frames_) write_frame(json, sample);
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
    static std::shared_ptr<Plot3DRoot> make_root(const AppOptions& options) {
        Plot3DRootOptions root_options{};
        root_options.grid_side = options.grid_side;
        std::shared_ptr<Plot3DRoot> root = gf::make_control<Plot3DRoot>(gf::StableId("implot3d"), root_options);
        return root;
    }

    static void write_frame(JsonWriter& json, const FrameSample& sample) {
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
        json.key("picture_renders");
        json.value(sample.picture_renders);
        json.key("gui_forms_allocations");
        json.value(sample.gui_forms_allocations);
        json.key("gui_forms_allocated_bytes");
        json.value(sample.gui_forms_allocated_bytes);
        json.key("captured");
        json.value(sample.captured);
        json.end_object();
    }

    const AppOptions& options_;
    std::shared_ptr<Plot3DRoot> root_;
    gf::Window window_;
    gf::OffscreenAttachment attached_;
    gf::MetricsSnapshot previous_metrics_{};
    gf::AllocationStatistics previous_allocations_{};
    std::uint64_t previous_renders_{};
    std::uint64_t last_interval_us_{};
    std::vector<FrameSample> frames_{};
    std::vector<StepSample> steps_{};
    std::vector<Invariant> invariants_{};
};

std::string sized_label(const char* prefix, std::size_t value) {
    char buffer[64]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%s %zu", prefix, value);
    std::string label{};
    if (written > 0) label.assign(buffer, std::min(static_cast<std::size_t>(written), sizeof(buffer) - 1U));
    return label;
}

void run_tour(WorkloadRun& run) {
    run.frame();
    run.step(scene_name(run.root().scene()));
    bool every_scene_rendered = true;
    bool surfaces_drew_triangles = true;
    for (std::size_t index = 1U; index < scene_count; ++index) {
        const std::uint64_t before = run.plot().picture_renders();
        run.root().select_scene(static_cast<SceneKind>(index));
        run.frame();
        run.frame();
        run.step(scene_name(run.root().scene()));
        if (run.plot().picture_renders() == before) every_scene_rendered = false;
        if (run.root().surface_items() > 0U && run.plot().scene().triangles_drawn() == 0U) surfaces_drew_triangles = false;
    }
    run.check("every scene rendered a picture", every_scene_rendered);
    run.check("every surface scene drew triangles", surfaces_drew_triangles);
    run.check("no item was refused", run.root().counters().items_refused == 0U);
    run.check("every scene was shown", run.root().counters().scene_switches == scene_count - 1U);
    run.check("tick labels were laid out", !run.plot().scene().labels().empty());
}

void run_orbit(WorkloadRun& run) {
    run.frame();
    const gp::Plot3DCamera start = run.plot().camera();
    const std::size_t first = run.frame_count();
    const gf::Point centre = run.plot_centre();
    // Across: turns the azimuth. Then up: tilts the elevation.
    run.drag(centre, gf::Point{centre.x + 240.0, centre.y}, 60U, gf::PointerButton::primary, gf::Modifier::none);
    const gp::Plot3DCamera turned = run.plot().camera();
    run.step("after the azimuth drag");
    const std::uint64_t before_tilt = run.plot().picture_renders();
    run.drag(centre, gf::Point{centre.x, centre.y - 120.0}, 32U, gf::PointerButton::primary, gf::Modifier::none);
    const gp::Plot3DCamera tilted = run.plot().camera();
    run.step("after the elevation drag");
    // Middle drag pans (the target moves; angles stay).
    run.drag(centre, gf::Point{centre.x + 60.0, centre.y + 40.0}, 16U, gf::PointerButton::middle, gf::Modifier::none);
    const gp::Plot3DCamera panned = run.plot().camera();
    run.step("after the pan drag");
    run.check("a drag across turned the azimuth", turned.azimuth != start.azimuth);
    run.check("a drag across kept the elevation", turned.elevation == start.elevation);
    run.check("a drag up changed the elevation", tilted.elevation != turned.elevation);
    run.check("a middle drag moved the target", panned.target != tilted.target);
    run.check("a middle drag kept the angles", panned.azimuth == tilted.azimuth && panned.elevation == tilted.elevation);
    run.check("gestures raised camera_changed", run.root().counters().camera_changes > 0U);
    run.check("no frame rendered more than one picture", run.most_renders_per_frame_since(first) <= 1U);
    run.check("the tilt rendered pictures", run.plot().picture_renders() > before_tilt);
    // At rest after the drags, nothing renders again.
    const std::size_t rest = run.frame_count();
    run.frame();
    run.frame();
    run.check("the picture is kept at rest", run.renders_since(rest) == 0U);
}

void run_zoom(WorkloadRun& run) {
    run.frame();
    const gf::Point centre = run.plot_centre();
    const double start = run.plot().camera().zoom;
    for (std::size_t notch = 0U; notch < 5U; ++notch) {
        run.wheel(centre, 1.0);
        run.frame();
    }
    const double zoomed_in = run.plot().camera().zoom;
    run.step("five notches in");
    for (std::size_t notch = 0U; notch < 5U; ++notch) {
        run.wheel(centre, -1.0);
        run.frame();
    }
    const double zoomed_out = run.plot().camera().zoom;
    run.step("five notches out");
    run.check("the wheel zoomed in", zoomed_in > start);
    run.check("the wheel zoomed back out", std::fabs(zoomed_out - start) < 1e-9 * start + 1e-12);
    // Right drag zooms too.
    run.drag(centre, gf::Point{centre.x, centre.y - 80.0}, 16U, gf::PointerButton::secondary, gf::Modifier::none);
    run.check("a right drag changed the zoom", run.plot().camera().zoom != zoomed_out);
    // The keyboard: P switches the projection (the toolbar box follows),
    // Home resets the view.
    const bool focused = run.window().request_focus(run.root().plot_pointer());
    run.check("the plot takes focus", focused);
    const bool switched = run.key(gf::PhysicalKey::p, gf::Modifier::none);
    run.frame();
    run.step("orthographic");
    run.check("P is handled", switched);
    run.check("P switched to orthographic",
              run.plot().camera().projection == gp::Plot3DProjection::orthographic);
    const bool reset = run.key(gf::PhysicalKey::home, gf::Modifier::none);
    run.frame();
    run.check("Home is handled", reset);
    run.check("Home reset the zoom", run.plot().camera().zoom == start);
    run.check("Home kept the projection", run.plot().camera().projection == gp::Plot3DProjection::orthographic);
}

void run_pick(WorkloadRun& run) {
    run.frame();
    run.frame();
    const gf::Point centre = run.plot_centre();
    run.pointer(gf::PointerAction::move, centre, gf::PointerButton::none, gf::Modifier::none);
    run.frame();
    const gp::Plot3DHit hit = run.plot().hover();
    run.step("hover at the centre");
    run.check("the centre hovers the surface", hit.kind == gp::Plot3DHitKind::surface);
    run.check("the hover raised hovered", run.root().counters().hovers > 0U);
    run.check("the readout names the point", !run.plot().readout().empty());
    run.check("the status line shows the readout", run.root().status_text() == run.plot().readout());
    if (hit.kind == gp::Plot3DHitKind::surface) {
        // The CONV interpolant passes through the samples without
        // overshoot; at 96 samples per side it stays near the function.
        const double expected = surface_value(SurfaceKind::peaks, hit.data_point[0], hit.data_point[1]);
        const gp::PlotRange heights = run.plot().scene().limits(gp::Plot3DAxis::z);
        const double tolerance = 0.05 * (heights.maximum - heights.minimum);
        run.check("the picked height is the surface's", std::fabs(hit.data_point[2] - expected) <= tolerance);
    }
    // A click that does not drag picks.
    run.pointer(gf::PointerAction::down, centre, gf::PointerButton::primary, gf::Modifier::none);
    run.pointer(gf::PointerAction::up, centre, gf::PointerButton::primary, gf::Modifier::none);
    run.frame();
    run.check("a click picked", run.root().counters().picks == 1U);
    // Leaving the plot clears the hover.
    run.pointer(gf::PointerAction::move, gf::Point{2.0, 2.0}, gf::PointerButton::none, gf::Modifier::none);
    run.frame();
    run.check("leaving the data clears the hover", run.plot().hover().kind != gp::Plot3DHitKind::surface);
}

// Linear surfaces first, whose triangles are two per kept cell, so a
// coarser stride never draws more; then the CONV surface's, recorded only
// (its tessellation follows a tolerance, not the cells).
void run_strides(WorkloadRun& run) {
    run.root().set_interpolation(gp::Plot3DInterpolation::linear);
    run.frame();
    const std::array<std::size_t, 5> strides{{1U, 2U, 4U, 8U, 16U}};
    std::uint64_t previous_triangles = 0U;
    bool fewer_each_time = true;
    bool each_built = true;
    std::uint64_t first_triangles = 0U;
    for (std::size_t index = 0U; index < strides.size(); ++index) {
        const std::uint64_t builds = run.plot().scene().mesh_build_count();
        run.root().set_stride(strides[index]);
        run.frame();
        run.step(sized_label("linear stride", strides[index]));
        const std::uint64_t triangles = static_cast<std::uint64_t>(run.plot().scene().triangles_drawn());
        if (index == 0U) {
            first_triangles = triangles;
        } else {
            if (triangles > previous_triangles) fewer_each_time = false;
            if (run.plot().scene().mesh_build_count() == builds) each_built = false;
        }
        previous_triangles = triangles;
    }
    run.check("a coarser stride never drew more triangles", fewer_each_time);
    run.check("stride 16 drew fewer triangles than stride 1", previous_triangles < first_triangles);
    run.check("every stride change rebuilt the mesh", each_built);
    run.root().set_interpolation(gp::Plot3DInterpolation::conv);
    for (std::size_t index = strides.size(); index > 0U; --index) {
        run.root().set_stride(strides[index - 1U]);
        run.frame();
        run.step(sized_label("conv stride", strides[index - 1U]));
    }
    run.check("no item was refused", run.root().counters().items_refused == 0U);
}

void run_interpolation(WorkloadRun& run) {
    run.frame();
    const std::array<gp::Plot3DInterpolation, 3> modes{{gp::Plot3DInterpolation::linear, gp::Plot3DInterpolation::flat,
                                                       gp::Plot3DInterpolation::conv}};
    const std::array<const char*, 3> names{{"linear", "flat", "conv"}};
    bool each_built = true;
    bool each_drew = true;
    run.step("conv");
    for (std::size_t index = 0U; index < modes.size(); ++index) {
        const std::uint64_t builds = run.plot().scene().mesh_build_count();
        run.root().set_interpolation(modes[index]);
        run.frame();
        run.step(names[index]);
        if (run.plot().scene().mesh_build_count() == builds) each_built = false;
        if (run.plot().scene().triangles_drawn() == 0U) each_drew = false;
    }
    run.check("every interpolation rebuilt the mesh", each_built);
    run.check("every interpolation drew triangles", each_drew);
}

void run_resolution(WorkloadRun& run) {
    run.frame();
    const std::array<std::size_t, 5> sides{{32U, 64U, 128U, 256U, 512U}};
    bool each_built = true;
    bool each_drew = true;
    for (const std::size_t side : sides) {
        const std::uint64_t builds = run.plot().scene().mesh_build_count();
        const std::size_t before_side = run.root().grid_side();
        run.root().set_grid_side(side);
        run.frame();
        run.step(sized_label("side", side));
        if (side != run.root().grid_side()) each_built = false;
        if (run.plot().scene().mesh_build_count() == builds && side != before_side) each_built = false;
        if (run.plot().scene().triangles_drawn() == 0U) each_drew = false;
    }
    run.check("every grid size was built", each_built);
    run.check("every grid size drew triangles", each_drew);
    run.check("no item was refused", run.root().counters().items_refused == 0U);
}

void run_idle(WorkloadRun& run) {
    run.frame();
    run.frame();
    const std::size_t first = run.frame_count();
    for (std::size_t index = 0U; index < 60U; ++index) run.frame();
    run.check("an unchanged window requests no damage", run.requested_area_since(first) == 0.0);
    run.check("an unchanged plot renders no picture", run.renders_since(first) == 0U);
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
    if (!options.font_dir.empty()) raster_options.font_directory = options.font_dir;
    gf::OffscreenRasterCreateResult made = gf::OffscreenRaster::create(raster_options);
    std::unique_ptr<gf::OffscreenRaster> raster = made.take_raster();
    if (raster == nullptr) {
        std::fprintf(stderr, "implot3d-class: the offscreen raster could not be made (try --font-dir)\n");
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
        case Workload::orbit:
            run_orbit(run);
            break;
        case Workload::zoom:
            run_zoom(run);
            break;
        case Workload::pick:
            run_pick(run);
            break;
        case Workload::strides:
            run_strides(run);
            break;
        case Workload::interpolation:
            run_interpolation(run);
            break;
        case Workload::resolution:
            run_resolution(run);
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
        std::fprintf(stderr, "implot3d-class: could not write %s\n", path.string().c_str());
        return 1;
    }
    std::printf("implot3d-class: %s written\n", path.string().c_str());
    const int code = held ? 0 : 1;
    return code;
}

} // namespace implot3d_class
