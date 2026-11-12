#include "workloads.hpp"

#include "generator.hpp"
#include "graph_root.hpp"
#include "json_writer.hpp"
#include "measurement.hpp"

#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>
#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/window/window.hpp>

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

struct FrameSample final {
    std::size_t nodes{};
    std::size_t links{};
    std::uint64_t microseconds{};
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
    }

    [[nodiscard]] GraphRoot& root() noexcept { return *root_; }
    [[nodiscard]] NodeCanvasView& canvas() noexcept { return (*root_).canvas(); }
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
        if (frames_.size() >= maximum_frames) return;
        FrameSample sample{};
        sample.nodes = canvas().statistics().last_frame_nodes;
        sample.links = canvas().statistics().last_frame_links;
        const std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        sample.microseconds = static_cast<std::uint64_t>(elapsed.count());
        sample.captured = capture.has_value();
        frames_.push_back(sample);
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
    std::shared_ptr<GraphRoot> root_;
    gf::Window window_;
    gf::OffscreenAttachment attached_;
    GeneratorReport generated_{};
    std::vector<FrameSample> frames_{};
    std::vector<Invariant> invariants_{};
};

// The centre of the first visible node's caption, in window units (the
// canvas sits at the window's origin), or none.
std::optional<gf::Point> visible_caption(WorkloadRun& run, std::size_t which) {
    NodeCanvasView& canvas = run.canvas();
    const std::vector<std::uint32_t>& visible = canvas.visible_nodes();
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
    NodeCanvasView& canvas = run.canvas();
    GraphDocument& document = run.root().document();
    const gf::NodeGraph& graph = document.graph();
    const std::span<const gf::NodeRecord> slots = graph.node_slots();
    const std::vector<std::uint32_t>& visible = canvas.visible_nodes();
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
    const std::uint64_t rebuilds = run.canvas().statistics().index_rebuilds;
    for (std::size_t step = 0U; step < 120U; ++step) {
        run.canvas().pan_by(gf::Point{-8.0, -3.0});
        run.frame();
    }
    run.check("panning does not rebuild the spatial index", run.canvas().statistics().index_rebuilds == rebuilds);
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
    run.check("every drag committed a move", run.canvas().statistics().moves_committed == drags);
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
}

void run_marquee_knife(WorkloadRun& run) {
    run.canvas().zoom_about(gf::Point{0.0, 0.0}, 2.0);
    run.frame();
    const gf::Size size = run.window().client_size();
    // A marquee from the first empty point found on a coarse scan.
    std::optional<gf::Point> empty{};
    for (double y = 8.0; y < size.height && !empty.has_value(); y += 16.0) {
        for (double x = 8.0; x < size.width; x += 16.0) {
            if (run.canvas().hit_test(gf::Point{x, y}).kind == CanvasHitKind::none) {
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
    std::vector<gf::LinkId> expected{};
    const gf::Point from{size.width * 0.1, size.height * 0.5};
    const gf::Point to{size.width * 0.9, size.height * 0.55};
    run.canvas().links_crossing(from, to, false, expected);
    run.drag(from, to, 24U, gf::PointerButton::secondary, gf::Modifier::control);
    std::size_t survivors = 0U;
    for (const gf::LinkId link : expected) {
        if (run.root().document().graph().link(link) != nullptr) ++survivors;
    }
    run.check("the knife cut every link the brute-force oracle found", survivors == 0U);
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
        const EditRequest request{gf::NodeId{slot, slots[slot].generation}, (slot % 2U) == 0U};
        if (!run.root().open_edit(request)) continue;
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
        const EditRequest request{gf::NodeId{slot, slots[slot].generation}, true};
        if (!run.root().open_edit(request)) continue;
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
    const CanvasStatistics first = run.canvas().statistics();
    for (std::size_t step = 0U; step < 60U; ++step) run.frame();
    const CanvasStatistics last = run.canvas().statistics();
    run.check("idle frames do not rebuild the index", last.index_rebuilds == first.index_rebuilds);
    run.check("idle frames lay out no title again", last.titles_laid_out == first.titles_laid_out);
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
