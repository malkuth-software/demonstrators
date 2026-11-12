#include "setup_collection.hpp"

#include "chat_view.hpp"
#include "measurement.hpp"
#include "process_stats.hpp"

#include <gui_forms/allocation/statistics/allocation_statistics.hpp>
#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>
#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/window/window.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace chat_stress {

namespace gf = gui_forms;

namespace {

constexpr std::array<std::uint64_t, 3> setup_sizes{100U, 1'000'000U, 10'000'000U};
constexpr std::size_t setup_runs = 3U;
constexpr double capture_scale = 2.0;
constexpr std::uint64_t steady_wheel_steps = 600U;
constexpr std::uint64_t steady_capture_every = 60U;
constexpr std::uint64_t steady_jumps = 1'102U;
constexpr std::chrono::seconds idle_observation{10};
constexpr std::uint64_t setup_random_field = 0x7365747570000000ULL;   // "setup"

using Clock = std::chrono::steady_clock;

std::uint64_t microseconds_between(Clock::time_point from, Clock::time_point to) {
    const std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(to - from);
    return static_cast<std::uint64_t>(elapsed.count());
}

// One run at one size.
struct SetupRun final {
    std::uint64_t rows{};
    std::uint64_t corpus_us{};
    std::uint64_t rows_handed_us{};   // make_control<ChatRoot>: reset_rows and the children
    std::uint64_t layout_us{};        // perform_layout, measuring the rows in view
    std::uint64_t frame_us{};         // capture_window: the first frame painted
    std::uint64_t setup_us{};         // rows handed through the first frame
    bool frame_ready{};
    gf::VirtualRowsStatistics view{};
    TextLayoutCounters text{};
    RingCounters ring{};
    CorpusCounters corpus{};
    // What this thread allocated through GUI.Forms' counted paths over the
    // setup span (AllocationScope): not the process's allocations.
    gf::AllocationStatistics allocations{};
    std::size_t corpus_bytes{};
};

// The state after the scroll and jump sequence at the largest size.
struct SteadyState final {
    bool collected{};
    gf::VirtualRowsStatistics view{};
    std::optional<std::uint64_t> footprint_bytes{};
    std::optional<std::uint64_t> cpu_microseconds{};
    double idle_seconds{};
    std::size_t ring_bytes{};
    std::size_t atlas_bytes{};
    std::size_t corpus_bytes{};
    // GUI.Forms' counted allocation paths, process-wide since start.
    gf::AllocationStatistics allocations{};
};

std::uint64_t median(std::vector<std::uint64_t> values) {
    if (values.empty()) return 0U;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2U];
}

void scroll_and_jump(gf::Window& window, gf::OffscreenAttachment& attached, ChatRoot& root, std::uint64_t seed) {
    const gf::Rect viewport = root.view().rectangle_to_window(root.view().viewport_bounds());
    gf::OffscreenCaptureOptions capture{};
    capture.scale = capture_scale;
    for (std::uint64_t step = 0U; step < steady_wheel_steps; ++step) {
        gf::PointerEvent wheel{};
        wheel.action = gf::PointerAction::wheel;
        wheel.position = gf::Point{viewport.x + viewport.width / 2.0, viewport.y + viewport.height / 2.0};
        wheel.wheel_delta = gf::Point{0.0, -1.0};
        wheel.click_count = 0U;
        static_cast<void>(window.dispatch_pointer(wheel));
        window.perform_layout();
        if (step % steady_capture_every == 0U) static_cast<void>(attached.capture_window(capture));
    }
    const std::uint64_t rows = root.corpus().row_count();
    for (std::uint64_t jump = 0U; jump < steady_jumps && rows > 0U; ++jump) {
        const RowIndex row{field_hash(seed, jump, setup_random_field) % rows};
        static_cast<void>(root.go_to_message(root.corpus().message_of_row(row)));
        window.perform_layout();
        if (jump % steady_capture_every == 0U) static_cast<void>(attached.capture_window(capture));
    }
}

void write_run(JsonWriter& json, const SetupRun& run) {
    json.begin_object();
    json.key("rows");
    json.value(run.rows);
    json.key("corpus_us");
    json.value(run.corpus_us);
    json.key("rows_handed_us");
    json.value(run.rows_handed_us);
    json.key("layout_us");
    json.value(run.layout_us);
    json.key("frame_us");
    json.value(run.frame_us);
    json.key("setup_us");
    json.value(run.setup_us);
    json.key("frame_ready");
    json.value(run.frame_ready);
    json.key("rows_measured");
    json.value(run.view.rows_measured);
    json.key("rows_painted");
    json.value(run.view.rows_painted);
    json.key("index_runs");
    json.value(static_cast<std::uint64_t>(run.view.index_runs));
    json.key("index_nodes");
    json.value(static_cast<std::uint64_t>(run.view.index_nodes));
    json.key("index_depth");
    json.value(static_cast<std::uint64_t>(run.view.index_depth));
    json.key("settle_passes");
    json.value(static_cast<std::uint64_t>(run.view.last_settle_passes));
    json.key("index_bytes");
    json.value(static_cast<std::uint64_t>(run.view.index_bytes));
    json.key("text_shaping_calls");
    json.value(run.text.shaping_calls);
    json.key("text_shaped_bytes");
    json.value(run.text.shaped_bytes);
    json.key("layout_ring_misses");
    json.value(run.ring.misses);
    json.key("timestamp_hash_calls");
    json.value(run.corpus.timestamp_hash_calls);
    json.key("texts_generated");
    json.value(run.corpus.texts_generated);
    json.key("corpus_resident_bytes");
    json.value(static_cast<std::uint64_t>(run.corpus_bytes));
    // GUI.Forms' counted allocation paths on this thread over the setup
    // span; the record's "scope" member names what is counted.
    json.key("gui_forms_allocations");
    json.raw(run.allocations.to_json());
    json.end_object();
}

} // namespace

int collect_setup(const AppOptions& options, gf::Application& application) {
    gf::OffscreenRasterOptions raster_options{};
    raster_options.font_directory = options.font_dir;
    const Clock::time_point raster_start = Clock::now();
    gf::OffscreenRasterCreateResult made = gf::OffscreenRaster::create(raster_options);
    std::unique_ptr<gf::OffscreenRaster> raster = made.take_raster();
    const std::uint64_t raster_us = microseconds_between(raster_start, Clock::now());
    if (raster == nullptr) {
        std::fprintf(stderr, "chat-stress: no offscreen raster: %s\n", made.detail().c_str());
        return 1;
    }

    std::vector<SetupRun> runs{};
    runs.reserve(setup_sizes.size() * setup_runs);
    SteadyState steady{};
    for (const std::uint64_t size : setup_sizes) {
        for (std::size_t attempt = 0U; attempt < setup_runs; ++attempt) {
            SetupRun run{};
            run.rows = size;
            CorpusOptions corpus_options = options.corpus;
            corpus_options.messages = size;
            const Clock::time_point corpus_start = Clock::now();
            Corpus corpus(corpus_options);
            run.corpus_us = microseconds_between(corpus_start, Clock::now());

            const gf::AllocationScope allocation_scope{};
            const Clock::time_point t0 = Clock::now();
            std::shared_ptr<ChatRoot> root = gf::make_control<ChatRoot>(gf::StableId("chat-stress"), corpus);
            const Clock::time_point t_rows = Clock::now();
            {
                gf::Window window(application, root, gf::Size{options.width, options.height});
                gf::OffscreenAttachment attached(*raster, window);
                window.perform_layout();
                const Clock::time_point t_layout = Clock::now();
                gf::OffscreenCaptureOptions capture{};
                capture.scale = capture_scale;
                const std::optional<gf::WindowCapture> frame = attached.capture_window(capture);
                const Clock::time_point t_frame = Clock::now();
                run.allocations = allocation_scope.counted();

                run.frame_ready = frame.has_value();
                run.rows_handed_us = microseconds_between(t0, t_rows);
                run.layout_us = microseconds_between(t_rows, t_layout);
                run.frame_us = microseconds_between(t_layout, t_frame);
                run.setup_us = microseconds_between(t0, t_frame);
                run.view = (*root).view().statistics();
                run.text = (*root).rows().text_counters();
                run.ring = (*root).rows().ring_counters();
                run.corpus = corpus.counters();
                run.corpus_bytes = corpus.resident_bytes();

                const bool last = size == setup_sizes.back() && attempt + 1U == setup_runs;
                if (last) {
                    scroll_and_jump(window, attached, *root, options.corpus.seed);
                    steady.collected = true;
                    steady.view = (*root).view().statistics();
                    steady.ring_bytes = (*root).rows().storage_bytes();
                    steady.atlas_bytes = (*root).atlas().storage_bytes();
                    steady.corpus_bytes = corpus.resident_bytes();
                    steady.footprint_bytes = process_footprint_bytes();
                    steady.allocations = gf::allocation_statistics();
                    // Nothing is scheduled headless: this observes background
                    // work only. The windowed `idle` workload counts presents
                    // and wakes under a real host.
                    const std::optional<std::uint64_t> cpu_before = process_cpu_microseconds();
                    const Clock::time_point idle_start = Clock::now();
                    std::this_thread::sleep_for(idle_observation);
                    const std::optional<std::uint64_t> cpu_after = process_cpu_microseconds();
                    steady.idle_seconds = static_cast<double>(microseconds_between(idle_start, Clock::now())) / 1e6;
                    if (cpu_before.has_value() && cpu_after.has_value()) {
                        steady.cpu_microseconds = *cpu_after - *cpu_before;
                    }
                }
            }
            root.reset();
            runs.push_back(run);
        }
    }

    const MachineDescription machine = describe_machine();
    JsonWriter json{};
    json.begin_object();
    json.key("workload");
    json.value("setup");
    json.key("seed");
    json.value(options.corpus.seed);
    json.key("window");
    json.begin_object();
    json.key("width");
    json.value(options.width);
    json.key("height");
    json.value(options.height);
    json.key("capture_scale");
    json.value(capture_scale);
    json.end_object();
    json.key("machine");
    write_machine(json, machine);
    json.key("build");
    write_build(json);
    json.key("raster_create_us");
    json.value(raster_us);
    json.key("definition");
    json.value("setup_us: from constructing the chat log with its rows (VirtualRowsView::reset_rows) through "
               "perform_layout to the first frame captured by the offscreen CPU raster at capture_scale");
    json.key("medians");
    json.begin_array();
    for (const std::uint64_t size : setup_sizes) {
        std::vector<std::uint64_t> setup{};
        std::vector<std::uint64_t> measured{};
        std::vector<std::uint64_t> shaping{};
        std::vector<std::uint64_t> allocations{};
        for (const SetupRun& run : runs) {
            if (run.rows != size) continue;
            setup.push_back(run.setup_us);
            measured.push_back(run.view.rows_measured);
            shaping.push_back(run.text.shaping_calls);
            allocations.push_back(run.allocations.total().allocations);
        }
        json.begin_object();
        json.key("rows");
        json.value(size);
        json.key("setup_us_median");
        json.value(median(setup));
        json.key("rows_measured_median");
        json.value(median(measured));
        json.key("text_shaping_calls_median");
        json.value(median(shaping));
        json.key("gui_forms_counted_allocations_median");
        json.value(median(allocations));
        json.end_object();
    }
    json.end_array();
    json.key("runs");
    json.begin_array();
    for (const SetupRun& run : runs) write_run(json, run);
    json.end_array();
    json.key("steady_state");
    json.begin_object();
    json.key("collected");
    json.value(steady.collected);
    json.key("rows");
    json.value(setup_sizes.back());
    json.key("sequence");
    json.value("600 wheel lines, 1102 seeded jumps, then 10 s with nothing scheduled");
    json.key("footprint_bytes");
    json.optional(steady.footprint_bytes);
    json.key("idle_cpu_microseconds");
    json.optional(steady.cpu_microseconds);
    json.key("idle_seconds");
    json.value(steady.idle_seconds);
    json.key("rows_measured");
    json.value(steady.view.rows_measured);
    json.key("index_runs");
    json.value(static_cast<std::uint64_t>(steady.view.index_runs));
    json.key("index_nodes");
    json.value(static_cast<std::uint64_t>(steady.view.index_nodes));
    json.key("index_depth");
    json.value(static_cast<std::uint64_t>(steady.view.index_depth));
    json.key("index_bytes");
    json.value(static_cast<std::uint64_t>(steady.view.index_bytes));
    json.key("index_measured");
    json.value(static_cast<std::uint64_t>(steady.view.index_measured));
    json.key("rows_forgotten");
    json.value(steady.view.rows_forgotten);
    json.key("measured_row_budget");
    json.value(static_cast<std::uint64_t>(ChatRoot::measured_row_budget));
    json.key("layout_ring_bytes");
    json.value(static_cast<std::uint64_t>(steady.ring_bytes));
    json.key("atlas_bytes");
    json.value(static_cast<std::uint64_t>(steady.atlas_bytes));
    json.key("corpus_resident_bytes");
    json.value(static_cast<std::uint64_t>(steady.corpus_bytes));
    json.key("gui_forms_allocations");
    json.raw(steady.allocations.to_json());
    json.end_object();
    json.end_object();

    const std::filesystem::path path = record_path(options.output_dir, machine.host_kind, "setup", options.corpus.seed);
    if (!write_record_file(path, json.text())) {
        std::fprintf(stderr, "chat-stress: could not write %s\n", path.string().c_str());
        return 1;
    }
    std::printf("chat-stress: wrote %s\n", path.string().c_str());
    for (const std::uint64_t size : setup_sizes) {
        std::vector<std::uint64_t> setup{};
        for (const SetupRun& run : runs) {
            if (run.rows == size) setup.push_back(run.setup_us);
        }
        std::printf("  %llu rows: setup median %llu us\n", static_cast<unsigned long long>(size),
                    static_cast<unsigned long long>(median(setup)));
    }
    return 0;
}

} // namespace chat_stress
