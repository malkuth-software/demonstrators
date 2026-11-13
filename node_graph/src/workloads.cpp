#include "workloads.hpp"

#include "generator.hpp"
#include "graph_root.hpp"
#include "json_writer.hpp"
#include "measurement.hpp"

#include <gui_forms/allocation.hpp>
#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>
#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/window/window.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace node_graph {

namespace gf = gui_forms;

namespace {

struct WorkloadName final {
    Workload workload{};
    std::string_view name{};
};

constexpr std::array<WorkloadName, 9> workload_names{{
    {Workload::load, "load"},
    {Workload::pan, "pan"},
    {Workload::zoom, "zoom"},
    {Workload::drag, "drag"},
    {Workload::magnet, "magnet"},
    {Workload::marquee_knife, "marquee-knife"},
    {Workload::edit_host, "edit-host"},
    {Workload::roundtrip, "roundtrip"},
    {Workload::idle, "idle"},
}};

// The most frames one workload records.
constexpr std::size_t maximum_frames = 512U;

// One frame's counts: what the canvas drew, the deltas of the window's
// public metrics (paint passes and damage) and of GUI.Forms' counted
// allocations, and the capture's interval.
struct FrameSample final {
    std::size_t nodes{};
    std::size_t links{};
    std::uint64_t microseconds{};
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

struct PhaseAllocations final {
    std::string_view phase{};
    std::uint64_t allocations{};
};

// One run: the root, its window, the raster attachment and what the run
// records. Declared in that order so the attachment ends before the
// window, and the window before the root it holds.
class WorkloadRun final {
public:
    WorkloadRun(gf::Application& application, gf::OffscreenRaster& raster, const AppOptions& options)
        : options_(options), root_(gf::make_control<GraphRoot>(gf::StableId("node-graph"))),
          window_(application, root_, gf::Size{options.width, options.height}), attached_(raster, window_) {
        frames_.reserve(maximum_frames);
        invariants_.reserve(16U);
        GeneratorOptions generator{};
        generator.nodes = options.nodes;
        generator.links_per_node = options.links_per_node;
        generator.seed = options.seed;
        generated_ = generate_stress_graph((*root_).document(), generator);
        (*root_).canvas().document_changed();
        window_.perform_layout();
        (*root_).canvas().frame_all();
        previous_metrics_ = window_.metrics_snapshot();
        previous_allocations_ = gf::allocation_statistics();
    }

    [[nodiscard]] GraphRoot& root() noexcept { return *root_; }
    [[nodiscard]] GraphCanvas& canvas() noexcept { return (*root_).canvas(); }
    [[nodiscard]] gf::Window& window() noexcept { return window_; }
    [[nodiscard]] const GeneratorReport& generated() const noexcept { return generated_; }

    // Lays out and paints one frame through the raster; records its counts
    // and its interval.
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
            sample.nodes = canvas().statistics().last_frame_nodes;
            sample.links = canvas().statistics().last_frame_links;
            const std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            sample.microseconds = static_cast<std::uint64_t>(elapsed.count());
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

    // The frame count so far, to mark where a phase starts.
    [[nodiscard]] std::size_t frame_count() const noexcept { return frames_.size(); }

    // GUI.Forms' counted allocations in the frames from `first` on.
    [[nodiscard]] std::uint64_t allocations_since(std::size_t first) const noexcept {
        std::uint64_t total = 0U;
        for (std::size_t index = first; index < frames_.size(); ++index) total += frames_[index].gui_forms_allocations;
        return total;
    }

    // The damage area the window was asked to repaint in the frames from
    // `first` on.
    [[nodiscard]] double requested_area_since(std::size_t first) const noexcept {
        double area = 0.0;
        for (std::size_t index = first; index < frames_.size(); ++index) area += frames_[index].requested_damage_area;
        return area;
    }

    void pointer(gf::PointerAction action, gf::Point position, gf::PointerButton button, gf::Modifier modifiers) {
        gf::PointerEvent event{};
        event.action = action;
        event.position = position;
        event.modifiers = modifiers;
        if (action != gf::PointerAction::move) {
            event.button = button;
            event.click_count = 1U;
        }
        static_cast<void>(window_.dispatch_pointer(event));
    }

    // Press, `steps` moves along the straight line, release.
    void drag(gf::Point from, gf::Point to, std::size_t steps, gf::PointerButton button, gf::Modifier modifiers) {
        pointer(gf::PointerAction::down, from, button, modifiers);
        for (std::size_t step = 1U; step <= steps; ++step) {
            const double share = static_cast<double>(step) / static_cast<double>(steps);
            const gf::Point at{from.x + (to.x - from.x) * share, from.y + (to.y - from.y) * share};
            pointer(gf::PointerAction::move, at, button, modifiers);
            if (step % 4U == 0U) frame();
        }
        pointer(gf::PointerAction::up, to, button, modifiers);
        frame();
    }

    // `steps` moves from `from` to `to` with the button held, a frame every
    // fourth.
    void move_to(gf::Point from, gf::Point to, std::size_t steps) {
        for (std::size_t step = 1U; step <= steps; ++step) {
            const double share = static_cast<double>(step) / static_cast<double>(steps);
            const gf::Point at{from.x + (to.x - from.x) * share, from.y + (to.y - from.y) * share};
            pointer(gf::PointerAction::move, at, gf::PointerButton::primary, gf::Modifier::none);
            if (step % 4U == 0U) frame();
        }
    }

    // A phase's counted allocations, written into the record as they are.
    void note_allocations(std::string_view phase, std::uint64_t allocations) {
        phases_.push_back(PhaseAllocations{phase, allocations});
    }

    void check(std::string_view name, bool held) {
        invariants_.push_back(Invariant{name, held});
        if (!held) std::fprintf(stderr, "node-graph: invariant failed: %.*s\n", static_cast<int>(name.size()), name.data());
    }

    [[nodiscard]] bool all_held() const noexcept {
        for (const Invariant& invariant : invariants_) {
            if (!invariant.held) return false;
        }
        return true;
    }

    void write_record(JsonWriter& json, Workload workload, const gf::AllocationStatistics& counted,
                      const std::optional<EditReport>& edits) {
        json.begin_object();
        json.key("workload");
        json.value(workload_name(workload));
        json.key("inputs");
        json.begin_object();
        json.key("nodes");
        json.value(static_cast<std::uint64_t>(options_.nodes));
        json.key("links_per_node");
        json.value(static_cast<std::uint64_t>(options_.links_per_node));
        json.key("seed");
        json.value(options_.seed);
        json.key("width");
        json.value(options_.width);
        json.key("height");
        json.value(options_.height);
        json.end_object();
        json.key("generated");
        write_generator(json, generated_);
        if (edits.has_value()) {
            json.key("edits");
            write_edits(json, *edits);
        }
        json.key("counters");
        write_counters(json, read_counters(*root_));
        json.key("window_metrics");
        json.raw(window_.metrics_snapshot().to_json());
        json.key("workload_allocations");
        json.raw(counted.to_json());
        json.key("frames");
        json.begin_array();
        for (const FrameSample& sample : frames_) {
            json.begin_object();
            json.key("nodes");
            json.value(static_cast<std::uint64_t>(sample.nodes));
            json.key("links");
            json.value(static_cast<std::uint64_t>(sample.links));
            json.key("interval_us");
            json.value(sample.microseconds);
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
        json.key("phase_allocations");
        json.begin_object();
        for (const PhaseAllocations& phase : phases_) {
            json.key(phase.phase);
            json.value(phase.allocations);
        }
        json.end_object();
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
    std::shared_ptr<GraphRoot> root_;
    gf::Window window_;
    gf::OffscreenAttachment attached_;
    GeneratorReport generated_{};
    gf::MetricsSnapshot previous_metrics_{};
    gf::AllocationStatistics previous_allocations_{};
    std::vector<FrameSample> frames_{};
    std::vector<Invariant> invariants_{};
    std::vector<PhaseAllocations> phases_{};
};

// The centre of the first visible node's caption, in window units (the
// canvas sits at the window's origin), or none.
std::optional<gf::Point> visible_caption(WorkloadRun& run, std::size_t which) {
    GraphCanvas& canvas = run.canvas();
    const std::span<const std::uint32_t> visible = canvas.visible_nodes();
    if (which >= visible.size()) return std::nullopt;
    const std::span<const gf::NodeRecord> slots = run.root().document().graph().node_slots();
    const std::uint32_t slot = visible[which];
    if (slot >= slots.size() || !slots[slot].live) return std::nullopt;
    const gf::Rect rect = canvas.view().rect_to_view(slots[slot].bounds);
    const gf::Point centre{rect.x + rect.width / 2.0, rect.y + 12.0 * canvas.view().zoom};
    return centre;
}

// A visible output and a compatible, unconnected input on another visible
// node, or false.
bool find_link_pair(WorkloadRun& run, std::size_t skip, gf::PortRef& from, gf::PortRef& to) {
    GraphCanvas& canvas = run.canvas();
    GraphDocument& document = run.root().document();
    const gf::NodeGraph& graph = document.graph();
    const std::span<const gf::NodeRecord> slots = graph.node_slots();
    const std::span<const std::uint32_t> visible = canvas.visible_nodes();
    std::size_t found = 0U;
    for (const std::uint32_t source : visible) {
        if (source >= slots.size() || !slots[source].live) continue;
        const gf::NodeRecord& source_node = slots[source];
        for (std::size_t port = 0U; port < source_node.ports.size(); ++port) {
            if (source_node.ports[port].direction != gf::PortDirection::output) continue;
            const gf::PortRef output{gf::NodeId{source, source_node.generation},
                                     gf::PortIndex{static_cast<std::uint16_t>(port)}};
            for (const std::uint32_t target : visible) {
                if (target == source || target >= slots.size() || !slots[target].live) continue;
                const gf::NodeRecord& target_node = slots[target];
                for (std::size_t input = 0U; input < target_node.ports.size(); ++input) {
                    if (target_node.ports[input].direction != gf::PortDirection::input) continue;
                    const gf::PortRef candidate{gf::NodeId{target, target_node.generation},
                                                gf::PortIndex{static_cast<std::uint16_t>(input)}};
                    if (graph.link(graph.link_into(candidate)) != nullptr) continue;
                    if (document.check_connection(output, candidate) != ConnectStatus::connected) continue;
                    if (found == skip) {
                        from = output;
                        to = candidate;
                        return true;
                    }
                    ++found;
                }
            }
        }
    }
    return false;
}

void run_load(WorkloadRun& run) {
    run.frame();
    run.frame();
    run.check("the generated nodes are in the graph", run.generated().nodes == run.root().document().graph().node_slots().size());
    run.check("a framed graph paints nodes", run.canvas().statistics().last_frame_nodes > 0U);
}

void run_pan(WorkloadRun& run) {
    run.canvas().zoom_about(gf::Point{run.window().client_size().width / 2.0, run.window().client_size().height / 2.0}, 4.0);
    run.frame();
    const gf::NodeCanvasStatistics before = run.canvas().statistics();
    const std::size_t first = run.frame_count();
    for (std::size_t step = 0U; step < 120U; ++step) {
        run.canvas().pan_by(gf::Point{-8.0, -3.0});
        run.frame();
    }
    const gf::NodeCanvasStatistics after = run.canvas().statistics();
    run.check("panning does not rebuild the spatial indexes", after.node_index_builds == before.node_index_builds &&
                                                                 after.link_index_builds == before.link_index_builds);
    // Recorded, not judged: the spec's zero-allocation budget is for the
    // interior frames of a windowed run; the status line's text is set on
    // every view change here.
    run.note_allocations("pan", run.allocations_since(first));
}

void run_zoom(WorkloadRun& run) {
    const gf::Point centre{run.window().client_size().width / 2.0, run.window().client_size().height / 2.0};
    for (std::size_t step = 0U; step < 90U; ++step) {
        run.canvas().zoom_about(centre, 1.06);
        run.frame();
    }
    for (std::size_t step = 0U; step < 180U; ++step) {
        run.canvas().zoom_about(centre, 1.0 / 1.06);
        run.frame();
    }
    run.check("zooming crosses levels of detail", run.canvas().statistics().level_transitions > 0U);
}

void run_drag(WorkloadRun& run) {
    run.canvas().zoom_about(gf::Point{0.0, 0.0}, 4.0);
    run.frame();
    const std::size_t steps_before = run.root().document().graph().undo_steps();
    std::size_t drags = 0U;
    for (std::size_t index = 0U; index < 10U; ++index) {
        const std::optional<gf::Point> caption = visible_caption(run, index);
        if (!caption.has_value()) break;
        const gf::Point to{(*caption).x + 40.0, (*caption).y + 24.0};
        run.drag(*caption, to, 24U, gf::PointerButton::primary, gf::Modifier::none);
        ++drags;
    }
    const std::size_t steps_after = run.root().document().graph().undo_steps();
    run.check("each drag is one undo step", steps_after == steps_before + drags);
    run.check("every drag committed a move", run.root().document().counters().canvas_moves == drags);
}

void run_magnet(WorkloadRun& run) {
    run.canvas().zoom_about(gf::Point{0.0, 0.0}, 4.0);
    run.frame();
    std::size_t attempts = 0U;
    const std::uint64_t made_before = run.canvas().statistics().connections_made;
    for (std::size_t index = 0U; index < 10U; ++index) {
        gf::PortRef from{};
        gf::PortRef to{};
        if (!find_link_pair(run, 0U, from, to)) break;
        const std::optional<gf::Point> start = run.canvas().port_view_anchor(from);
        const std::optional<gf::Point> end = run.canvas().port_view_anchor(to);
        if (!start.has_value() || !end.has_value()) break;
        // Released 10 units off the input: inside the capture radius.
        const gf::Point release{(*end).x - 6.0, (*end).y + 8.0};
        run.drag(*start, release, 20U, gf::PointerButton::primary, gf::Modifier::none);
        ++attempts;
    }
    const std::uint64_t made = run.canvas().statistics().connections_made - made_before;
    run.check("every release near a compatible input connected", made == attempts);

    // The release radius: capture within 16 view units, then pull out to 20
    // (inside the 24-unit release radius) and let go: still captured, so
    // it connects. Pulled out to 40 instead, the magnet lets go and the
    // release asks for the palette.
    gf::PortRef from{};
    gf::PortRef to{};
    if (find_link_pair(run, 0U, from, to)) {
        const std::optional<gf::Point> start = run.canvas().port_view_anchor(from);
        const std::optional<gf::Point> end = run.canvas().port_view_anchor(to);
        if (start.has_value() && end.has_value()) {
            const std::uint64_t made_held = run.canvas().statistics().connections_made;
            run.pointer(gf::PointerAction::down, *start, gf::PointerButton::primary, gf::Modifier::none);
            run.move_to(*start, gf::Point{(*end).x - 4.0, (*end).y}, 16U);
            run.move_to(gf::Point{(*end).x - 4.0, (*end).y}, gf::Point{(*end).x - 20.0, (*end).y}, 8U);
            run.pointer(gf::PointerAction::up, gf::Point{(*end).x - 20.0, (*end).y}, gf::PointerButton::primary,
                        gf::Modifier::none);
            run.frame();
            run.check("a capture pulled inside the release radius still connects",
                      run.canvas().statistics().connections_made == made_held + 1U);
        }
    }
    if (find_link_pair(run, 0U, from, to)) {
        const std::optional<gf::Point> start = run.canvas().port_view_anchor(from);
        const std::optional<gf::Point> end = run.canvas().port_view_anchor(to);
        if (start.has_value() && end.has_value()) {
            const std::uint64_t made_free = run.canvas().statistics().connections_made;
            const std::uint64_t palettes = run.canvas().statistics().palette_requests;
            const gf::Point away{(*end).x - 40.0, (*end).y - 40.0};
            run.pointer(gf::PointerAction::down, *start, gf::PointerButton::primary, gf::Modifier::none);
            run.move_to(*start, gf::Point{(*end).x - 4.0, (*end).y}, 16U);
            run.move_to(gf::Point{(*end).x - 4.0, (*end).y}, away, 8U);
            run.pointer(gf::PointerAction::up, away, gf::PointerButton::primary, gf::Modifier::none);
            run.frame();
            run.check("a capture pulled past the release radius lets go",
                      run.canvas().statistics().connections_made == made_free);
            run.check("the free release asks for the palette",
                      run.canvas().statistics().palette_requests == palettes + 1U);
            run.root().close_palette();
        }
    }
}

void run_marquee_knife(WorkloadRun& run) {
    run.canvas().zoom_about(gf::Point{0.0, 0.0}, 2.0);
    run.frame();
    const gf::Size size = run.window().client_size();
    // A marquee from the first empty point found on a coarse scan.
    std::optional<gf::Point> empty{};
    for (double y = 8.0; y < size.height && !empty.has_value(); y += 16.0) {
        for (double x = 8.0; x < size.width; x += 16.0) {
            if (run.canvas().hit_test(gf::Point{x, y}).kind == gf::NodeCanvasHitKind::none) {
                empty = gf::Point{x, y};
                break;
            }
        }
    }
    if (empty.has_value()) {
        run.drag(*empty, gf::Point{size.width * 0.7, size.height * 0.7}, 24U, gf::PointerButton::primary,
                 gf::Modifier::none);
    }
    run.check("a marquee selected nodes", !empty.has_value() || !run.canvas().selection().empty());
    const gf::Point from{size.width * 0.1, size.height * 0.5};
    const gf::Point to{size.width * 0.9, size.height * 0.55};
    // The canvas's own query is the oracle: its flattening is not public,
    // so an independent brute force cannot reproduce its paths exactly
    // (GAPS.md N11). What is checked is that the cut matches the query and
    // that every other link survives.
    std::vector<gf::LinkId> expected(run.root().document().graph().link_slots().size());
    const std::size_t crossing = run.canvas().links_crossing(from, to, std::span<gf::LinkId>(expected));
    expected.resize(std::min(crossing, expected.size()));
    const std::size_t links_before = run.root().document().graph().link_count();
    run.drag(from, to, 24U, gf::PointerButton::secondary, gf::Modifier::control);
    std::size_t survivors = 0U;
    for (const gf::LinkId link : expected) {
        if (run.root().document().graph().link(link) != nullptr) ++survivors;
    }
    run.check("the knife cut every link the canvas's query listed", survivors == 0U);
    run.check("the knife cut nothing else",
              run.root().document().graph().link_count() + expected.size() == links_before);
}

void run_edit_host(WorkloadRun& run) {
    run.frame();
    const std::span<const gf::NodeRecord> slots = run.root().document().graph().node_slots();
    std::string before{};
    run.root().document().save_text(before);
    // Open and cancel: the document stays byte-identical.
    std::size_t opened = 0U;
    for (std::uint32_t slot = 0U; slot < slots.size() && opened < 40U; ++slot) {
        if (!slots[slot].live) continue;
        const gf::NodeEditPart part = (slot % 2U) == 0U ? gf::NodeEditPart::caption : gf::NodeEditPart::body;
        if (!run.root().open_edit(gf::NodeId{slot, slots[slot].generation}, part)) continue;
        run.frame();
        run.root().cancel_edit();
        ++opened;
    }
    std::string after{};
    run.root().document().save_text(after);
    run.check("cancelled edits leave the document byte-identical", before == after);
    run.check("at most one edit host per kind", run.root().counters().edit_hosts_created <= kind_table().size());
    // Open and commit a changed title: one step each.
    const std::size_t steps_before = run.root().document().graph().undo_steps();
    std::size_t committed = 0U;
    for (std::uint32_t slot = 0U; slot < slots.size() && committed < 10U; ++slot) {
        if (!slots[slot].live) continue;
        if (!run.root().open_edit(gf::NodeId{slot, slots[slot].generation}, gf::NodeEditPart::caption)) continue;
        run.root().commit_edit(std::string("Edited ") + std::to_string(slot));
        run.frame();
        ++committed;
    }
    run.check("each committed edit is one undo step",
              run.root().document().graph().undo_steps() == steps_before + committed);
}

std::optional<EditReport> run_roundtrip(WorkloadRun& run) {
    GraphDocument& document = run.root().document();
    std::string before{};
    document.save_text(before);
    const EditReport edits = apply_seeded_edits(document, run.root().document().graph().revision() ^ 0x9e3779b97f4a7c15ULL,
                                                1000U);
    run.canvas().document_changed();
    run.frame();
    std::size_t undone = 0U;
    while (document.graph().undo_steps() > 0U && undone < 2000U) {
        static_cast<void>(document.undo());
        ++undone;
    }
    run.canvas().document_changed();
    run.frame();
    std::string after{};
    document.save_text(after);
    run.check("1000 seeded edits, all undone, save the same bytes", before == after);
    return edits;
}

void run_idle(WorkloadRun& run) {
    run.frame();
    run.frame();
    const gf::NodeCanvasStatistics first = run.canvas().statistics();
    const EvaluatorCounters evaluated = run.root().evaluator().counters();
    const std::size_t first_frame = run.frame_count();
    for (std::size_t step = 0U; step < 60U; ++step) run.frame();
    const gf::NodeCanvasStatistics last = run.canvas().statistics();
    run.check("idle frames do not rebuild the indexes", last.node_index_builds == first.node_index_builds &&
                                                           last.link_index_builds == first.link_index_builds);
    run.check("idle frames resolve no item look again", last.item_looks_resolved == first.item_looks_resolved);
    run.check("idle frames evaluate nothing", run.root().evaluator().counters().full_passes == evaluated.full_passes);
    run.check("an unchanged window requests no damage", run.requested_area_since(first_frame) == 0.0);
    run.note_allocations("idle", run.allocations_since(first_frame));
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
        std::fprintf(stderr, "node-graph: the offscreen raster could not be made (pass --font-dir)\n");
        return 1;
    }
    JsonWriter json{};
    bool held = false;
    {
        WorkloadRun run(application, *raster, options);
        const gf::AllocationScope scope{};
        std::optional<EditReport> edits{};
        switch (workload) {
        case Workload::load: run_load(run); break;
        case Workload::pan: run_pan(run); break;
        case Workload::zoom: run_zoom(run); break;
        case Workload::drag: run_drag(run); break;
        case Workload::magnet: run_magnet(run); break;
        case Workload::marquee_knife: run_marquee_knife(run); break;
        case Workload::edit_host: run_edit_host(run); break;
        case Workload::roundtrip: edits = run_roundtrip(run); break;
        case Workload::idle: run_idle(run); break;
        default: run.check("a known workload", false); break;
        }
        const gf::AllocationStatistics counted = scope.counted();
        run.write_record(json, workload, counted, edits);
        held = run.all_held();
    }
    const std::filesystem::path path = record_path(options.output_dir, host_kind(), workload_name(workload), options.seed);
    if (!write_record_file(path, json.text())) {
        std::fprintf(stderr, "node-graph: could not write %s\n", path.string().c_str());
        return 1;
    }
    std::printf("node-graph: %s\n", path.string().c_str());
    const int code = held ? 0 : 1;
    return code;
}

} // namespace node_graph
