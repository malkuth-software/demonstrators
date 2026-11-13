#include "workloads.hpp"

#include "json_writer.hpp"
#include "measurement.hpp"
#include "profiler_root.hpp"
#include "profiler_window.hpp"
#include "trace_generator.hpp"

#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>
#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/window/window.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace flame_graph {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

struct WorkloadName final {
    Workload workload{};
    std::string_view name{};
};

constexpr std::array<WorkloadName, 9> workload_names{{
    {Workload::load, "load"},
    {Workload::flame_zoom, "flame-zoom"},
    {Workload::flame_pan, "flame-pan"},
    {Workload::timeline_zoom, "timeline-zoom"},
    {Workload::timeline_pan, "timeline-pan"},
    {Workload::search, "search"},
    {Workload::table_scroll, "table-scroll"},
    {Workload::mode_switch, "mode-switch"},
    {Workload::idle, "idle"},
}};

constexpr std::size_t maximum_frames = 512U;

// One frame's deltas of the window's public metrics, the counted
// allocations and the views' per-frame counts, and its capture interval.
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
    std::uint64_t flame_cells{};
    std::uint64_t flame_visits{};
    std::uint64_t timeline_bars{};
    std::uint64_t timeline_visits{};
    std::uint64_t timeline_rows{};
    bool captured{};
};

struct Invariant final {
    std::string_view name{};
    bool held{};
};

// Allocations counted over one phase of a workload (GUI.Forms' counted
// paths only).
struct PhaseAllocations final {
    std::string_view name{};
    std::uint64_t allocations{};
    std::uint64_t allocated_bytes{};
};

// One run: the root, its window, the raster attachment and what the run
// records. Declared in that order so the attachment ends before the
// window, and the window before the root it holds.
class WorkloadRun final {
public:
    WorkloadRun(gf::Application& application, gf::OffscreenRaster& raster, const AppOptions& options)
        : options_(options), root_(make_profiler_root(options)),
          window_(application, root_, gf::Size{options.width, options.height}), attached_(raster, window_) {
        frames_.reserve(maximum_frames);
        invariants_.reserve(32U);
        phases_.reserve(16U);
        window_.perform_layout();
        previous_metrics_ = window_.metrics_snapshot();
        previous_allocations_ = gf::allocation_statistics();
    }

    [[nodiscard]] ProfilerRoot& root() noexcept { return *root_; }
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
            const FlameViewStatistics& flame = (*root_).flame().statistics();
            const TimelineStatistics& timeline = (*root_).timeline().statistics();
            sample.flame_cells = flame.last_frame_cells;
            sample.flame_visits = flame.last_frame_visits;
            sample.timeline_bars = timeline.last_frame_bars;
            sample.timeline_visits = timeline.last_frame_visits;
            sample.timeline_rows = timeline.last_frame_rows;
            sample.captured = capture.has_value();
            frames_.push_back(sample);
        }
        previous_metrics_ = metrics;
        previous_allocations_ = allocations;
    }

    void pointer(gf::PointerAction action, gf::Point position, gf::PointerButton button, std::uint32_t clicks) {
        gf::PointerEvent event{};
        event.action = action;
        event.position = position;
        if (action != gf::PointerAction::move) {
            event.button = button;
            event.click_count = clicks;
        }
        static_cast<void>(window_.dispatch_pointer(event));
    }

    void click(gf::Point at, std::uint32_t clicks) {
        pointer(gf::PointerAction::move, at, gf::PointerButton::none, 1U);
        pointer(gf::PointerAction::down, at, gf::PointerButton::primary, clicks);
        pointer(gf::PointerAction::up, at, gf::PointerButton::primary, clicks);
        frame();
    }

    void double_click(gf::Point at) {
        pointer(gf::PointerAction::move, at, gf::PointerButton::none, 1U);
        pointer(gf::PointerAction::down, at, gf::PointerButton::primary, 1U);
        pointer(gf::PointerAction::up, at, gf::PointerButton::primary, 1U);
        pointer(gf::PointerAction::down, at, gf::PointerButton::primary, 2U);
        pointer(gf::PointerAction::up, at, gf::PointerButton::primary, 2U);
        frame();
    }

    void wheel(gf::Point at, double notches, gf::Modifier modifiers) {
        gf::PointerEvent event{};
        event.action = gf::PointerAction::wheel;
        event.position = at;
        event.wheel_delta = gf::Point{0.0, notches};
        event.wheel_unit = gf::WheelDeltaUnit::lines;
        event.modifiers = modifiers;
        event.click_count = 0U;
        static_cast<void>(window_.dispatch_pointer(event));
    }

    void key(std::uint32_t physical_key) {
        gf::KeyEvent down{};
        down.action = gf::KeyAction::down;
        down.physical_key = physical_key;
        static_cast<void>(window_.dispatch_key(down));
        gf::KeyEvent up{};
        up.action = gf::KeyAction::up;
        up.physical_key = physical_key;
        static_cast<void>(window_.dispatch_key(up));
    }

    void drag(gf::Point from, gf::Point to, std::size_t steps) {
        pointer(gf::PointerAction::move, from, gf::PointerButton::none, 1U);
        pointer(gf::PointerAction::down, from, gf::PointerButton::primary, 1U);
        for (std::size_t step = 1U; step <= steps; ++step) {
            const double share = static_cast<double>(step) / static_cast<double>(steps);
            const gf::Point at{from.x + (to.x - from.x) * share, from.y + (to.y - from.y) * share};
            pointer(gf::PointerAction::move, at, gf::PointerButton::primary, 1U);
            if (step % 4U == 0U) frame();
        }
        pointer(gf::PointerAction::up, to, gf::PointerButton::primary, 1U);
        frame();
    }

    void check(std::string_view name, bool held) {
        invariants_.push_back(Invariant{name, held});
        if (!held) {
            std::fprintf(stderr, "flame-graph: invariant failed: %.*s\n", static_cast<int>(name.size()), name.data());
        }
    }

    [[nodiscard]] bool all_held() const noexcept {
        for (const Invariant& invariant : invariants_) {
            if (!invariant.held) return false;
        }
        return true;
    }

    [[nodiscard]] std::size_t frame_count() const noexcept { return frames_.size(); }
    [[nodiscard]] const FrameSample& frame_at(std::size_t index) const noexcept { return frames_[index]; }
    [[nodiscard]] double requested_area_since(std::size_t first) const noexcept {
        double area = 0.0;
        for (std::size_t index = first; index < frames_.size(); ++index) area += frames_[index].requested_damage_area;
        return area;
    }

    // Starts a phase whose counted allocations note_phase records.
    void begin_phase() noexcept { phase_start_ = gf::allocation_statistics().total(); }
    void note_phase(std::string_view name) {
        const gf::AllocationCounts now = gf::allocation_statistics().total();
        PhaseAllocations phase{};
        phase.name = name;
        phase.allocations = now.allocations - phase_start_.allocations;
        phase.allocated_bytes = now.allocated_bytes - phase_start_.allocated_bytes;
        phases_.push_back(phase);
    }

    void write_record(JsonWriter& json, Workload workload) {
        json.begin_object();
        json.key("demonstrator");
        json.value("flame_graph");
        json.key("workload");
        json.value(workload_name(workload));
        json.key("inputs");
        json.begin_object();
        json.key("trace");
        json.value(options_.trace.has_value() ? (*options_.trace).string() : std::string("generated"));
        json.key("threads");
        json.value(static_cast<std::uint64_t>(options_.threads));
        json.key("spans_per_thread");
        json.value(static_cast<std::uint64_t>(options_.spans));
        json.key("names");
        json.value(static_cast<std::uint64_t>(options_.names));
        json.key("seed");
        json.value(options_.seed);
        json.key("width");
        json.value(options_.width);
        json.key("height");
        json.value(options_.height);
        json.end_object();
        json.key("counters");
        write_counters(json, read_counters(window_, *root_));
        json.key("phase_allocations");
        json.begin_array();
        for (const PhaseAllocations& phase : phases_) {
            json.begin_object();
            json.key("phase");
            json.value(phase.name);
            json.key("allocations");
            json.value(phase.allocations);
            json.key("allocated_bytes");
            json.value(phase.allocated_bytes);
            json.end_object();
        }
        json.end_array();
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
            json.key("flame_cells");
            json.value(sample.flame_cells);
            json.key("flame_visits");
            json.value(sample.flame_visits);
            json.key("timeline_bars");
            json.value(sample.timeline_bars);
            json.key("timeline_visits");
            json.value(sample.timeline_visits);
            json.key("timeline_rows");
            json.value(sample.timeline_rows);
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
    const AppOptions& options_;
    std::shared_ptr<ProfilerRoot> root_;
    gf::Window window_;
    gf::OffscreenAttachment attached_;
    gf::MetricsSnapshot previous_metrics_{};
    gf::AllocationStatistics previous_allocations_{};
    gf::AllocationCounts phase_start_{};
    std::vector<FrameSample> frames_{};
    std::vector<Invariant> invariants_{};
    std::vector<PhaseAllocations> phases_{};
};

// The centre of a local rectangle of `control`, in window coordinates.
gf::Point window_point(gf::Control& control, gf::Point local) {
    return control.point_to_window(local);
}

gf::Point plot_centre(gf::Control& control, gf::Rect plot) {
    return window_point(control, gf::Point{plot.x + plot.width / 2.0, plot.y + plot.height / 2.0});
}

// The heaviest descent from the root, `levels` deep (or to a leaf): a
// node whose cell is wide and centred enough to click.
CallNodeIndex heavy_node(const TraceModel& model, std::size_t levels) {
    CallNodeIndex node = root_node;
    for (std::size_t level = 0U; level < levels; ++level) {
        const std::span<const std::uint32_t> children = model.children(node);
        if (children.empty()) break;
        std::uint32_t heaviest = children[0];
        for (const std::uint32_t child : children) {
            if (model.node(CallNodeIndex{child}).total > model.node(CallNodeIndex{heaviest}).total) heaviest = child;
        }
        node = CallNodeIndex{heaviest};
    }
    return node;
}

// The search's results counted again, independently: lower-cased copies
// and std::string::find for the names, the parent chain for coverage.
struct SearchOracle final {
    std::size_t names_matched{};
    std::size_t nodes_matched{};
    std::int64_t matched_weight{};
};

std::string lower_ascii(std::string_view text) {
    std::string lowered(text);
    for (char& byte : lowered) {
        if (byte >= 'A' && byte <= 'Z') byte = static_cast<char>(byte + ('a' - 'A'));
    }
    return lowered;
}

SearchOracle search_oracle(const TraceModel& model, std::string_view query) {
    SearchOracle oracle{};
    if (query.empty()) return oracle;
    const std::string needle = lower_ascii(query);
    std::vector<std::uint8_t> matched_names(model.names().size(), 0U);
    for (std::size_t name = 0U; name < model.names().size(); ++name) {
        const std::string hay = lower_ascii(model.names().text(NameId{static_cast<std::uint32_t>(name)}));
        if (hay.find(needle) == std::string::npos) continue;
        matched_names[name] = 1U;
        ++oracle.names_matched;
    }
    const std::span<const CallNode> nodes = model.nodes();
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const CallNode& node = nodes[index];
        if (node.synthetic || matched_names[node.name.value] == 0U) continue;
        ++oracle.nodes_matched;
        bool ancestor_matched = false;
        CallNodeIndex up = node.parent;
        while (up != root_node) {
            const CallNode& parent = model.node(up);
            if (!parent.synthetic && matched_names[parent.name.value] != 0U) {
                ancestor_matched = true;
                break;
            }
            up = parent.parent;
        }
        if (!ancestor_matched) oracle.matched_weight += node.total;
    }
    return oracle;
}

// Whether every frame from `first` on keeps `Field` at or under `limit`.
template <std::uint64_t FrameSample::* Field>
bool frames_within(const WorkloadRun& run, std::size_t first, std::uint64_t limit) {
    for (std::size_t index = first; index < run.frame_count(); ++index) {
        if (run.frame_at(index).*Field > limit) return false;
    }
    return true;
}

// -- workloads ----------------------------------------------------------------------------------

void run_load(WorkloadRun& run, const AppOptions& options) {
    run.frame();
    const GeneratorOptions generator = generator_options(options);
    std::string text{};
    write_chrome_trace(generator, text);
    TraceModel direct{};
    generate_trace(generator, direct);
    run.begin_phase();
    const LoadReport report = run.root().load_text(text, TraceFormat::automatic);
    run.note_phase("load chrome json");
    run.frame();
    const TraceModel& loaded = run.root().model();
    run.check("the generated JSON loads", report.loaded);
    run.check("it was read as Chrome trace JSON", report.format == TraceFormat::chrome_json);
    run.check("no event was malformed", report.malformed_events == 0U && report.other_events == 0U);
    run.check("every complete event became a span", report.complete_events == loaded.statistics().spans);
    run.check("the loaded trace has the generated spans", loaded.statistics().spans == direct.statistics().spans);
    run.check("the same call tree", loaded.statistics().nodes == direct.statistics().nodes);
    run.check("the same total weight", loaded.total_weight() == direct.total_weight());
    run.check("the same functions", loaded.functions().size() == direct.functions().size());
    run.check("the same depth", loaded.statistics().max_depth == direct.statistics().max_depth);
    run.check("the threads are named", loaded.threads().size() == generator.threads &&
                                           loaded.names().text(loaded.threads()[0].label) == "worker 1");
    run.check("the views painted the loaded trace",
              run.root().flame().statistics().last_frame_cells > 0U &&
                  run.root().timeline().statistics().last_frame_bars > 0U);
}

void run_flame_zoom(WorkloadRun& run) {
    run.frame();
    FlameView& flame = run.root().flame();
    const TraceModel& model = run.root().model();
    const CallNodeIndex target = heavy_node(model, 4U);
    const std::optional<gf::Rect> cell = flame.cell_rect(target);
    run.check("the heavy node has a cell", cell.has_value());
    if (!cell.has_value()) return;
    const gf::Point centre = window_point(flame, gf::Point{(*cell).x + (*cell).width / 2.0, (*cell).y + 4.0});
    run.click(centre, 1U);
    run.check("a click selects the node", run.root().selection().node.has_value() &&
                                              *run.root().selection().node == target);
    const std::optional<std::size_t> function = model.function_of(model.node(target).name);
    const std::optional<gf::VirtualRowIndex> current = run.root().table().rows().selected_row();
    run.check("the table selects the node's function",
              function.has_value() && current.has_value() &&
                  run.root().table().function_at_row((*current).value) == function);
    const std::size_t first = run.frame_count();
    run.begin_phase();
    run.double_click(centre);
    const gp::PlotRange zoomed = flame.x_range();
    const CallNode& node = model.node(target);
    run.check("a double click zooms to the node's extent",
              std::fabs(zoomed.minimum - static_cast<double>(node.offset)) < 1.0 &&
                  std::fabs(zoomed.maximum - static_cast<double>(node.offset + node.total)) < 1.0);
    const gf::Point plot = plot_centre(flame, flame.plot_rect());
    for (std::size_t step = 0U; step < 20U; ++step) {
        run.wheel(plot, 1.0, gf::Modifier::none);
        run.frame();
    }
    run.note_phase("double click and 20 wheel zooms");
    run.check("wheel zoom narrowed the range", flame.x_range().extent() < zoomed.extent());
    // A frame draws at most one cell per minimum width per visible row.
    const std::uint64_t cell_bound = static_cast<std::uint64_t>(flame.visible_rows()) *
                                     static_cast<std::uint64_t>(flame.plot_rect().width / FlameView::minimum_cell_width + 2.0);
    run.check("frame cells within rows times width", frames_within<&FrameSample::flame_cells>(run, first, cell_bound));
    run.key(gf::PhysicalKey::home);
    run.frame();
    run.check("Home shows the whole trace", flame.x_range().minimum == 0.0 &&
                                                flame.x_range().maximum == static_cast<double>(model.total_weight()));
}

void run_flame_pan(WorkloadRun& run) {
    run.frame();
    FlameView& flame = run.root().flame();
    const gf::Point plot = plot_centre(flame, flame.plot_rect());
    for (std::size_t step = 0U; step < 12U; ++step) run.wheel(plot, 1.0, gf::Modifier::none);
    run.frame();
    const gp::PlotRange before = flame.x_range();
    run.begin_phase();
    run.drag(plot, gf::Point{plot.x + 240.0, plot.y}, 24U);
    run.drag(plot, gf::Point{plot.x - 120.0, plot.y}, 12U);
    run.note_phase("two drags");
    const gp::PlotRange after = flame.x_range();
    const double total = static_cast<double>(run.root().model().total_weight());
    run.check("the drags moved the range", after.minimum != before.minimum);
    run.check("panning kept the zoom", std::fabs(after.extent() - before.extent()) < before.extent() * 1.0e-6 + 1.0);
    run.check("the range stays in the trace", after.minimum >= 0.0 && after.maximum <= total + 1.0e-6);
    run.check("view changes were raised", flame.statistics().view_changes > 0U);
    for (std::size_t step = 0U; step < 4U; ++step) run.wheel(plot, -1.0, gf::Modifier::shift);
    run.frame();
    run.check("Shift+wheel scrolls rows", flame.statistics().row_scrolls >= 4U);
}

void run_timeline_zoom(WorkloadRun& run) {
    run.frame();
    TimelineView& timeline = run.root().timeline();
    const gp::PlotRange full = timeline.x_range();
    const gf::Point plot = plot_centre(timeline, timeline.plot_rect());
    const std::size_t first = run.frame_count();
    run.begin_phase();
    for (std::size_t step = 0U; step < 30U; ++step) {
        run.wheel(plot, 1.0, gf::Modifier::none);
        run.frame();
    }
    run.note_phase("30 wheel zooms");
    run.check("the range narrowed", timeline.x_range().extent() < full.extent());
    // About one visit per device pixel per row (merges jump ahead); two
    // per pixel bounds it with the visited span after each jump.
    bool bounded = true;
    for (std::size_t index = first; index < run.frame_count(); ++index) {
        const FrameSample& sample = run.frame_at(index);
        const std::uint64_t limit =
            sample.timeline_rows * static_cast<std::uint64_t>(2.0 * timeline.plot_rect().width + 4.0);
        if (sample.timeline_visits > limit) bounded = false;
    }
    run.check("row visits within twice the width", bounded);
    run.check("merged spans were jumped over at full range", timeline.statistics().spans_merged > 0U);
    run.key(gf::PhysicalKey::home);
    run.frame();
    run.check("Home restores the full range", timeline.x_range() == full);
}

void run_timeline_pan(WorkloadRun& run) {
    run.frame();
    TimelineView& timeline = run.root().timeline();
    const gf::Point plot = plot_centre(timeline, timeline.plot_rect());
    for (std::size_t step = 0U; step < 16U; ++step) run.wheel(plot, 1.0, gf::Modifier::none);
    run.frame();
    const gp::PlotRange before = timeline.x_range();
    run.begin_phase();
    run.drag(plot, gf::Point{plot.x - 300.0, plot.y}, 24U);
    run.note_phase("drag");
    run.check("the drag moved the range", timeline.x_range().minimum != before.minimum);
    for (std::size_t step = 0U; step < 6U; ++step) run.wheel(plot, -1.0, gf::Modifier::shift);
    run.frame();
    run.check("Shift+wheel scrolls lanes", timeline.row_scroll() > 0.0 ||
                                               timeline.content_height() <= timeline.plot_rect().height);
    // A click on the first lane's top row picks a span there, if one shows.
    const ThreadIndex lane{0U};
    const gf::Point local{timeline.plot_rect().x + timeline.plot_rect().width / 2.0,
                          timeline.lane_top(lane) + TimelineView::lane_header_height + TimelineView::row_height / 2.0};
    const std::optional<SpanHit> hit = timeline.span_at(local);
    if (hit.has_value()) {
        run.click(window_point(timeline, local), 1U);
        run.check("a click on a span selects its node", run.root().selection().node.has_value() &&
                                                            *run.root().selection().node == (*hit).node);
    }
}

void run_search(WorkloadRun& run) {
    run.frame();
    const std::array<std::string_view, 5> queries{"render", "STEP_1", "gc::step_2", "no such frame", ""};
    gf::TextBox& box = run.root().search_box();
    run.begin_phase();
    for (const std::string_view query : queries) {
        // The box's text, and the search it asks for (set_search runs
        // again when text_changed already did; it is idempotent).
        box.text = std::string(query);
        run.root().set_search(query);
        run.frame();
        const SearchOracle oracle = search_oracle(run.root().model(), query);
        const SearchSummary& summary = run.root().search().summary;
        const bool agrees = summary.names_matched == oracle.names_matched &&
                            summary.nodes_matched == oracle.nodes_matched &&
                            summary.matched_weight == oracle.matched_weight;
        run.check("the search agrees with an independent count", agrees);
        run.check("matched weight within the total", summary.matched_weight <= run.root().model().total_weight());
        std::size_t matching_functions = 0U;
        for (const FunctionRow& function : run.root().model().functions()) {
            if (query.empty() || run.root().search().matches(function.name)) ++matching_functions;
        }
        run.check("the table shows the matching functions", run.root().table().row_count() == matching_functions);
    }
    run.note_phase("five searches");
    run.check("every query searched", run.root().counters().searches >= queries.size() - 1U);
    run.check("the empty query clears the search", !run.root().search().active());
}

void run_table_scroll(WorkloadRun& run) {
    run.frame();
    DetailsTable& table = run.root().table();
    const TraceModel& model = run.root().model();
    bool ordered = true;
    for (std::size_t row = 1U; row < table.row_count(); ++row) {
        const FunctionRow& previous = model.functions()[*table.function_at_row(row - 1U)];
        const FunctionRow& current = model.functions()[*table.function_at_row(row)];
        if (previous.total < current.total) ordered = false;
    }
    run.check("rows start sorted by total, largest first", ordered);
    const std::size_t rows = table.row_count();
    run.begin_phase();
    for (std::size_t step = 0U; step <= 20U && rows > 0U; ++step) {
        const std::size_t row = (rows - 1U) * step / 20U;
        table.rows().scroll_to_row(gf::VirtualRowIndex{row}, gf::VirtualRowAlignment::start);
        run.frame();
    }
    run.note_phase("scroll through the table");
    const double visible = std::ceil(table.rows().client_rectangle().height / DetailsTable::row_height) + 2.0;
    run.check("each frame paints only the visible rows",
              static_cast<double>(table.rows().statistics().last_paint_rows) <= visible);
    const gf::Point calls_header = table.point_to_window(
        gf::Point{table.client_rectangle().width - DetailsTable::share_width - DetailsTable::calls_width / 2.0, 10.0});
    run.click(calls_header, 1U);
    run.check("a header click sorts by calls", table.sort_column() == TableColumn::calls && table.sort_descending());
    bool by_calls = true;
    for (std::size_t row = 1U; row < table.row_count(); ++row) {
        if (model.functions()[*table.function_at_row(row - 1U)].calls <
            model.functions()[*table.function_at_row(row)].calls) {
            by_calls = false;
        }
    }
    run.check("rows sorted by calls", by_calls);
}

void run_mode_switch(WorkloadRun& run) {
    run.frame();
    FlameView& flame = run.root().flame();
    const gf::Point button = run.root().search_box().point_to_window(
        gf::Point{ProfilerRoot::search_width + 8.0 + ProfilerRoot::button_width / 2.0, 10.0});
    std::array<std::uint64_t, 2> cells{};
    run.begin_phase();
    for (std::size_t step = 0U; step < 6U; ++step) {
        run.click(button, 1U);
        cells[step % 2U] = flame.statistics().last_frame_cells;
    }
    run.note_phase("six switches");
    run.check("six switches", run.root().counters().orientation_switches == 6U);
    run.check("back to the icicle", flame.orientation() == FlameOrientation::icicle);
    run.check("both orientations drew cells", cells[0] > 0U && cells[1] > 0U);
}

void run_idle(WorkloadRun& run) {
    run.frame();
    run.frame();
    const std::size_t first = run.frame_count();
    const std::uint64_t flame_frames = run.root().flame().statistics().frames_painted;
    for (std::size_t step = 0U; step < 60U; ++step) run.frame();
    run.check("an unchanged window requests no damage", run.requested_area_since(first) == 0.0);
    run.check("the flame graph did not repaint", run.root().flame().statistics().frames_painted == flame_frames);
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
        std::fprintf(stderr, "flame-graph: the offscreen raster could not be made (try --font-dir)\n");
        return 1;
    }
    JsonWriter json{};
    bool held = false;
    {
        WorkloadRun run(application, *raster, options);
        switch (workload) {
        case Workload::load:
            run_load(run, options);
            break;
        case Workload::flame_zoom:
            run_flame_zoom(run);
            break;
        case Workload::flame_pan:
            run_flame_pan(run);
            break;
        case Workload::timeline_zoom:
            run_timeline_zoom(run);
            break;
        case Workload::timeline_pan:
            run_timeline_pan(run);
            break;
        case Workload::search:
            run_search(run);
            break;
        case Workload::table_scroll:
            run_table_scroll(run);
            break;
        case Workload::mode_switch:
            run_mode_switch(run);
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
        std::fprintf(stderr, "flame-graph: could not write %s\n", path.string().c_str());
        return 1;
    }
    std::printf("flame-graph: %s written\n", path.string().c_str());
    const int code = held ? 0 : 1;
    return code;
}

} // namespace flame_graph
