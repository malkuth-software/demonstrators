#include "workload_driver.hpp"

#include <gui_forms/event.hpp>
#include <gui_forms/events/input_events/input_events.hpp>

#include <array>
#include <cmath>
#include <cstdio>

namespace chat_stress {

namespace gf = gui_forms;

namespace {

constexpr std::uint64_t warm_up_ticks = 120U;        // 2 s at 60 Hz
constexpr std::uint64_t scroll_ticks = 600U;         // 10 s
constexpr std::uint64_t page_down_ticks = 7'200U;    // 120 s
constexpr std::uint64_t thumb_ticks = 300U;          // 5 s
constexpr std::uint64_t random_jumps = 1'000U;
constexpr std::uint64_t thumb_clicks = 100U;
constexpr std::uint64_t append_segment_ticks = 1'800U;   // 30 s
constexpr std::uint64_t append_segments = 6U;
constexpr std::uint64_t edits_per_phase = 1'000U;
constexpr std::uint64_t settle_ticks = 600U;         // 10 s
constexpr std::uint64_t startup_tick_limit = 600U;
constexpr std::chrono::milliseconds idle_observation{60'000};
constexpr std::uint64_t workload_random_field = 0x776F726B6C6F6164ULL;   // "workload"

struct WorkloadName final {
    Workload workload;
    std::string_view name;
};

constexpr std::array<WorkloadName, 11> workload_names{{
    {Workload::startup, "startup"},
    {Workload::memory, "memory"},
    {Workload::idle, "idle"},
    {Workload::idle_focused, "idle-focused"},
    {Workload::wheel, "wheel"},
    {Workload::trackpad, "trackpad"},
    {Workload::page_down, "page-down"},
    {Workload::thumb, "thumb"},
    {Workload::jump, "jump"},
    {Workload::append, "append"},
    {Workload::edit, "edit"},
}};

double milliseconds_between(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to) {
    const std::chrono::duration<double, std::milli> elapsed = to - from;
    return elapsed.count();
}

void write_samples(JsonWriter& json, const std::vector<FrameSample>& samples) {
    json.begin_array();
    for (const FrameSample& sample : samples) {
        json.begin_object();
        json.key("phase");
        json.value(static_cast<std::uint64_t>(sample.phase));
        json.key("interval_ms");
        json.value(sample.interval_ms);
        json.key("rows_measured");
        json.value(sample.rows_measured);
        json.key("rows_painted");
        json.value(sample.rows_painted);
        json.key("scroll_copies");
        json.value(sample.scroll_copies);
        json.key("scroll_repaints");
        json.value(sample.scroll_repaints);
        json.key("frames_presented");
        json.value(sample.frames_presented);
        json.key("painted_area");
        json.value(sample.painted_area);
        json.key("text_shaping_calls");
        json.value(sample.text_shaping_calls);
        json.key("ring_misses");
        json.value(sample.ring_misses);
        json.key("timestamp_hash_calls");
        json.value(sample.timestamp_hash_calls);
        json.key("semantic_children");
        json.value(sample.semantic_children);
        json.key("index_runs");
        json.value(sample.index_runs);
        json.key("index_bytes");
        json.value(sample.index_bytes);
        json.key("gui_forms_allocations");
        json.value(sample.gui_forms_allocations);
        json.key("gui_forms_allocated_bytes");
        json.value(sample.gui_forms_allocated_bytes);
        json.end_object();
    }
    json.end_array();
}

} // namespace

std::optional<Workload> workload_named(std::string_view name) noexcept {
    for (const WorkloadName& entry : workload_names) {
        if (entry.name == name) return entry.workload;
    }
    return std::nullopt;
}

const char* workload_name(Workload workload) noexcept {
    for (const WorkloadName& entry : workload_names) {
        if (entry.workload == workload) return entry.name.data();
    }
    return "unknown";
}

WorkloadDriver::WorkloadDriver(gf::Window& window, ChatRoot& root, Workload workload, const AppOptions& options)
    : window_(window), root_(root), workload_(workload), options_(options), timer_(window, tick_interval) {
    samples_.reserve(static_cast<std::size_t>(append_segments * append_segment_ticks + page_down_ticks + 64U));
    text_scratch_.reserve(maximum_overlay_text_bytes);
    gf::on(timer_.tick(), *this, &WorkloadDriver::on_tick);
}

WorkloadDriver::~WorkloadDriver() { timer_.stop(); }

void WorkloadDriver::start(gf::ApplicationWindowHandle handle) {
    if (started_) return;
    started_ = true;
    handle_ = handle;
    process_start_us_ = process_start_unix_microseconds();
    ready_us_ = unix_microseconds_now();
    ready_time_ = std::chrono::steady_clock::now();
    last_tick_ = ready_time_;
    start_counters_ = read_counters(window_, root_);
    previous_ = start_counters_;
    // The workload's test time source: typed text and edits get stamped
    // from the seed's epoch, not the wall clock.
    root_.set_test_time(corpus_epoch_ms);
    timer_.start();
}

std::uint64_t WorkloadDriver::random(std::uint64_t draw) const noexcept {
    const std::uint64_t value = field_hash(options_.corpus.seed, draw, workload_random_field);
    return value;
}

void WorkloadDriver::go_to_middle(std::uint64_t draw) {
    const std::uint64_t rows = root_.corpus().row_count();
    if (rows == 0U) return;
    // A seeded position in the middle half.
    const std::uint64_t quarter = rows / 4U;
    const std::uint64_t span = rows / 2U + 1U;
    const RowIndex row{quarter + random(draw) % span};
    static_cast<void>(root_.go_to_message(root_.corpus().message_of_row(row)));
}

void WorkloadDriver::wheel(double delta, gf::WheelDeltaUnit unit) {
    const gf::Rect viewport = root_.view().rectangle_to_window(root_.view().viewport_bounds());
    gf::PointerEvent event{};
    event.action = gf::PointerAction::wheel;
    event.position = gf::Point{viewport.x + viewport.width / 2.0, viewport.y + viewport.height / 2.0};
    // Negative y scrolls toward the end (content moves up).
    event.wheel_delta = gf::Point{0.0, delta};
    event.wheel_unit = unit;
    event.click_count = 0U;
    static_cast<void>(window_.dispatch_pointer(event));
}

void WorkloadDriver::key(std::uint32_t physical_key, gf::Modifier modifiers) {
    gf::KeyEvent down{};
    down.action = gf::KeyAction::down;
    down.physical_key = physical_key;
    down.modifiers = modifiers;
    static_cast<void>(window_.dispatch_key(down));
    gf::KeyEvent up = down;
    up.action = gf::KeyAction::up;
    static_cast<void>(window_.dispatch_key(up));
}

void WorkloadDriver::on_tick() {
    if (finished_) return;
    const bool more = step();
    record_sample();
    ++tick_;
    if (!more) finish();
}

bool WorkloadDriver::step() {
    switch (workload_) {
    case Workload::startup: return step_startup();
    case Workload::memory: return step_memory(tick_);
    case Workload::idle: return step_idle(tick_);
    case Workload::idle_focused: return step_idle(tick_);
    case Workload::wheel: return step_scroll(Workload::wheel, tick_);
    case Workload::trackpad: return step_scroll(Workload::trackpad, tick_);
    case Workload::page_down: return step_scroll(Workload::page_down, tick_);
    case Workload::thumb: return step_scroll(Workload::thumb, tick_);
    case Workload::jump: return step_jump(tick_);
    case Workload::append: return step_append(tick_);
    case Workload::edit: return step_edit(tick_);
    default: return false;
    }
}

bool WorkloadDriver::step_scroll(Workload kind, std::uint64_t tick) {
    gf::VirtualRowsView& view = root_.view();
    if (tick == 0U) {
        if (kind == Workload::thumb) {
            view.scroll_to_offset(0.0);
        } else {
            go_to_middle(++draws_);
        }
        static_cast<void>(window_.request_focus(root_.view_pointer()));
        phase_ = 0U;
        trackpad_velocity_ = 0.0;
        return true;
    }
    std::uint64_t measured = scroll_ticks;
    if (kind == Workload::page_down) measured = page_down_ticks;
    if (kind == Workload::thumb) measured = thumb_ticks;
    if (tick > warm_up_ticks + measured) return false;
    phase_ = tick <= warm_up_ticks ? 0U : 1U;
    switch (kind) {
    case Workload::wheel:
        wheel(-1.0, gf::WheelDeltaUnit::lines);
        break;
    case Workload::trackpad: {
        // The fixed fling table: 40 units, 5 % less each frame, restarted
        // every 60 frames.
        if (tick % 60U == 1U) {
            trackpad_velocity_ = 40.0;
        } else {
            trackpad_velocity_ = trackpad_velocity_ * 0.95;
        }
        wheel(-trackpad_velocity_, gf::WheelDeltaUnit::pixels);
        break;
    }
    case Workload::page_down:
        // The keyboard repeat rate, about 30 a second.
        if (tick % 2U == 0U) key(gf::PhysicalKey::page_down, gf::Modifier::none);
        break;
    case Workload::thumb: {
        if (phase_ == 1U) {
            const double share = static_cast<double>(tick - warm_up_ticks) / static_cast<double>(measured);
            const double end = view.content_height() - view.viewport_bounds().height;
            double offset = 0.0;
            if (end > 0.0) offset = share * end;
            view.scroll_to_offset(offset);
        }
        break;
    }
    default:
        return false;
    }
    return true;
}

bool WorkloadDriver::step_jump(std::uint64_t tick) {
    gf::VirtualRowsView& view = root_.view();
    const std::uint64_t rows = root_.corpus().row_count();
    if (tick == 0U) {
        static_cast<void>(window_.request_focus(root_.view_pointer()));
        phase_ = 0U;
        return true;
    }
    if (rows == 0U) return false;
    if (tick <= random_jumps) {
        phase_ = 1U;
        const RowIndex row{random(++draws_) % rows};
        static_cast<void>(root_.go_to_message(root_.corpus().message_of_row(row)));
        return true;
    }
    if (tick == random_jumps + 1U) {
        phase_ = 2U;
        key(gf::PhysicalKey::end, gf::Modifier::control);
        return true;
    }
    if (tick == random_jumps + 2U) {
        phase_ = 2U;
        key(gf::PhysicalKey::home, gf::Modifier::control);
        return true;
    }
    if (tick <= random_jumps + 2U + thumb_clicks) {
        // A thumb click: the content at a seeded share of the current extent.
        phase_ = 3U;
        const double share = static_cast<double>(random(++draws_) % 10'001U) / 10'000.0;
        const double end = view.content_height() - view.viewport_bounds().height;
        double offset = 0.0;
        if (end > 0.0) offset = share * end;
        view.scroll_to_offset(offset);
        return true;
    }
    return false;
}

bool WorkloadDriver::step_append(std::uint64_t tick) {
    if (tick == 0U) {
        static_cast<void>(window_.request_focus(root_.view_pointer()));
        root_.view().set_follow_tail(true);
        key(gf::PhysicalKey::end, gf::Modifier::control);
        return true;
    }
    const std::uint64_t position = tick - 1U;
    const std::uint64_t segment = position / append_segment_ticks;
    if (segment >= append_segments) return false;
    const std::uint64_t within = position % append_segment_ticks;
    phase_ = static_cast<std::uint32_t>(segment);
    if (segment == 3U && within == 0U) {
        // Second half: scrolled away from the end, follow-tail does not hold.
        go_to_middle(++draws_);
    }
    const std::uint64_t rate_kind = segment % 3U;
    if (rate_kind == 0U) {
        if (within % 60U == 0U) root_.append_generated(1U);   // 1 per second
    } else if (rate_kind == 1U) {
        const std::uint64_t due = (within + 1U) * 100U / 60U;  // 100 per second
        const std::uint64_t done = within * 100U / 60U;
        root_.append_generated(due - done);
    } else {
        if (within % 120U == 0U) root_.append_generated(10'000U);   // a burst every 2 s
    }
    return true;
}

bool WorkloadDriver::step_edit(std::uint64_t tick) {
    gf::VirtualRowsView& view = root_.view();
    Corpus& corpus = root_.corpus();
    if (tick == 0U) {
        go_to_middle(++draws_);
        phase_ = 0U;
        return true;
    }
    if (tick <= edits_per_phase) {
        // Visible rows, growing and shrinking: the text of another seeded message.
        phase_ = 1U;
        const std::size_t first = view.first_visible_row().value;
        const std::size_t end = view.end_visible_row().value;
        if (end <= first) {
            ++skipped_edits_;
            return true;
        }
        const RowIndex row{first + random(++draws_) % (end - first)};
        corpus.generated_text(random(++draws_) % corpus.generated_count(), text_scratch_);
        root_.edit_row(row, text_scratch_);
        return true;
    }
    if (tick <= 2U * edits_per_phase) {
        // Measured rows above the viewport: the anchor must not move.
        phase_ = 2U;
        const std::size_t anchor = view.first_visible_row().value;
        if (anchor == 0U) {
            ++skipped_edits_;
            return true;
        }
        std::size_t reach = anchor;
        if (reach > 32U) reach = 32U;
        const std::size_t candidate = anchor - 1U - static_cast<std::size_t>(random(++draws_) % reach);
        if (!view.row_measured(gf::VirtualRowIndex{candidate})) {
            ++skipped_edits_;
            return true;
        }
        const double before = view.row_bounds(gf::VirtualRowIndex{anchor}).y;
        corpus.generated_text(random(++draws_) % corpus.generated_count(), text_scratch_);
        root_.edit_row(RowIndex{candidate}, text_scratch_);
        const double after = view.row_bounds(gf::VirtualRowIndex{anchor}).y;
        const double drift = std::fabs(after - before);
        if (drift > anchor_drift_max_) anchor_drift_max_ = drift;
        ++anchor_checks_;
        return true;
    }
    if (tick <= 3U * edits_per_phase) {
        // Range erases, above and below the viewport.
        phase_ = 3U;
        const std::uint64_t rows = corpus.row_count();
        if (rows == 0U) return false;
        const RowIndex row{random(++draws_) % rows};
        const std::uint64_t count = 1U + random(++draws_) % 10'000U;
        root_.erase_rows(row, count);
        return true;
    }
    return false;
}

bool WorkloadDriver::step_idle(std::uint64_t tick) {
    if (tick == 0U) {
        go_to_middle(++draws_);
        gf::PointerEvent leave{};
        leave.action = gf::PointerAction::leave;
        leave.click_count = 0U;
        static_cast<void>(window_.dispatch_pointer(leave));
        if (workload_ == Workload::idle_focused) {
            static_cast<void>(window_.request_focus(root_.compose_pointer()));
        } else {
            static_cast<void>(window_.request_focus(root_.view_pointer()));
        }
        phase_ = 0U;
        return true;
    }
    if (tick <= settle_ticks) return true;
    if (tick == settle_ticks + 1U) {
        // Observe: the driver's timer is stopped and one 60 s one-shot ends
        // the observation (its wake is subtracted in the record).
        phase_ = 1U;
        idle_before_ = read_counters(window_, root_);
        idle_start_ = std::chrono::steady_clock::now();
        timer_.stop();
        timer_.set_interval(idle_observation);
        timer_.start();
        return true;
    }
    idle_seconds_ = milliseconds_between(idle_start_, std::chrono::steady_clock::now()) / 1000.0;
    return false;
}

bool WorkloadDriver::step_startup() {
    const gf::MetricsSnapshot metrics = window_.metrics_snapshot();
    const std::int64_t now = unix_microseconds_now();
    if (!first_present_us_.has_value() && metrics.frames_presented > 0U) first_present_us_ = now;
    gf::VirtualRowsView& view = root_.view();
    bool all_measured = true;
    const std::size_t first = view.first_visible_row().value;
    const std::size_t end = view.end_visible_row().value;
    if (end <= first && root_.corpus().row_count() > 0U) all_measured = false;
    for (std::size_t row = first; row < end; ++row) {
        if (!view.row_measured(gf::VirtualRowIndex{row})) {
            all_measured = false;
            break;
        }
    }
    if (all_measured && !condition_met_) {
        condition_met_ = true;
        presents_at_condition_ = metrics.frames_presented;
    }
    if (condition_met_ && metrics.frames_presented > presents_at_condition_) {
        ready_to_use_us_ = now;
        return false;
    }
    return tick_ < startup_tick_limit;
}

bool WorkloadDriver::step_memory(std::uint64_t tick) {
    // 7.2: wheel (7.4a), trackpad (7.4b), jumps (7.5), then settle.
    const std::uint64_t wheel_end = warm_up_ticks + scroll_ticks + 1U;
    const std::uint64_t trackpad_end = wheel_end + warm_up_ticks + scroll_ticks + 1U;
    const std::uint64_t jump_end = trackpad_end + random_jumps + 3U + thumb_clicks;
    const std::uint64_t settle_end = jump_end + settle_ticks;
    if (tick < wheel_end) {
        static_cast<void>(step_scroll(Workload::wheel, tick));
        phase_ = 0U;
        return true;
    }
    if (tick < trackpad_end) {
        static_cast<void>(step_scroll(Workload::trackpad, tick - wheel_end));
        phase_ = 1U;
        return true;
    }
    if (tick < jump_end) {
        static_cast<void>(step_jump(tick - trackpad_end));
        phase_ = 2U;
        return true;
    }
    phase_ = 3U;
    return tick < settle_end;
}

void WorkloadDriver::record_sample() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    FrameSample sample{};
    sample.phase = phase_;
    sample.interval_ms = milliseconds_between(last_tick_, now);
    last_tick_ = now;
    CounterSnapshot current{};
    current.view = root_.view().statistics();
    current.text = root_.rows().text_counters();
    current.allocations = gf::allocation_statistics();
    current.ring = root_.rows().ring_counters();
    current.corpus = root_.corpus().counters();
    current.metrics = window_.metrics_snapshot();
    sample.rows_measured = current.view.rows_measured - previous_.view.rows_measured;
    sample.rows_painted = current.view.rows_painted - previous_.view.rows_painted;
    sample.scroll_copies = current.view.scroll_copies - previous_.view.scroll_copies;
    sample.scroll_repaints = current.view.scroll_repaints - previous_.view.scroll_repaints;
    sample.frames_presented = current.metrics.frames_presented - previous_.metrics.frames_presented;
    sample.painted_area = current.metrics.painted_damage_area - previous_.metrics.painted_damage_area;
    sample.text_shaping_calls = current.text.shaping_calls - previous_.text.shaping_calls;
    sample.ring_misses = current.ring.misses - previous_.ring.misses;
    sample.timestamp_hash_calls = current.corpus.timestamp_hash_calls - previous_.corpus.timestamp_hash_calls;
    sample.semantic_children = root_.view().realized_item_count();
    sample.index_runs = current.view.index_runs;
    sample.index_bytes = current.view.index_bytes;
    const gf::AllocationCounts allocated_now = current.allocations.total();
    const gf::AllocationCounts allocated_before = previous_.allocations.total();
    sample.gui_forms_allocations = allocated_now.allocations - allocated_before.allocations;
    sample.gui_forms_allocated_bytes = allocated_now.allocated_bytes - allocated_before.allocated_bytes;
    if (samples_.size() < samples_.capacity()) samples_.push_back(sample);
    previous_ = current;
}

void WorkloadDriver::finish() {
    finished_ = true;
    timer_.stop();
    const CounterSnapshot end = read_counters(window_, root_);
    const MachineDescription machine = describe_machine();
    JsonWriter json{};
    json.begin_object();
    json.key("workload");
    json.value(workload_name(workload_));
    json.key("seed");
    json.value(options_.corpus.seed);
    json.key("messages");
    json.value(options_.corpus.messages);
    json.key("users");
    json.value(static_cast<std::uint64_t>(options_.corpus.users));
    json.key("scripts");
    json.value(options_.corpus.mixed_scripts ? "mixed" : "latin");
    json.key("window");
    json.begin_object();
    json.key("width");
    json.value(options_.width);
    json.key("height");
    json.value(options_.height);
    json.key("scale");
    json.value(window_.scale());
    json.end_object();
    json.key("machine");
    write_machine(json, machine);
    json.key("build");
    write_build(json);
    json.key("ticks");
    json.value(tick_);
    json.key("results");
    json.begin_object();
    if (workload_ == Workload::startup) {
        // t0 process start, t1 first present, t2 first present with every
        // visible row measured (section 7.1); microseconds.
        json.key("t0_process_start_us");
        if (process_start_us_.has_value()) {
            json.value(*process_start_us_);
        } else {
            json.null();
        }
        json.key("window_ready_us");
        json.value(ready_us_);
        json.key("t1_first_present_us");
        if (first_present_us_.has_value()) {
            json.value(*first_present_us_);
        } else {
            json.null();
        }
        json.key("t2_ready_us");
        if (ready_to_use_us_.has_value()) {
            json.value(*ready_to_use_us_);
        } else {
            json.null();
        }
        json.key("note");
        json.value("t1 and t2 are observed at the driver's 16 ms ticks: each is at most one tick late");
    }
    if (workload_ == Workload::edit) {
        json.key("anchor_drift_max");
        json.value(anchor_drift_max_);
        json.key("anchor_checks");
        json.value(anchor_checks_);
        json.key("skipped_edits");
        json.value(skipped_edits_);
    }
    if ((workload_ == Workload::idle || workload_ == Workload::idle_focused) && idle_before_.has_value()) {
        const CounterSnapshot& before = *idle_before_;
        json.key("observed_seconds");
        json.value(idle_seconds_);
        json.key("frames_presented");
        json.value(end.metrics.frames_presented - before.metrics.frames_presented);
        json.key("scheduler_wakes");
        json.value(end.metrics.scheduler_wakes - before.metrics.scheduler_wakes);
        // The driver's one-shot is the only timer tick it causes.
        std::uint64_t timer_ticks = end.metrics.ui_timer_ticks - before.metrics.ui_timer_ticks;
        if (timer_ticks > 0U) --timer_ticks;
        json.key("ui_timer_ticks_excluding_driver");
        json.value(timer_ticks);
        json.key("retained_pixels_presented");
        json.value(end.metrics.retained_pixels_presented - before.metrics.retained_pixels_presented);
        json.key("cpu_microseconds");
        if (end.cpu_microseconds.has_value() && before.cpu_microseconds.has_value()) {
            json.value(*end.cpu_microseconds - *before.cpu_microseconds);
        } else {
            json.null();
        }
    }
    json.end_object();
    json.key("start");
    write_counters(json, start_counters_);
    json.key("end");
    write_counters(json, end);
    json.key("samples");
    write_samples(json, samples_);
    json.end_object();

    const std::filesystem::path path =
        record_path(options_.output_dir, machine.host_kind, workload_name(workload_), options_.corpus.seed);
    if (write_record_file(path, json.text())) {
        std::printf("chat-stress: wrote %s\n", path.string().c_str());
    } else {
        std::fprintf(stderr, "chat-stress: could not write %s\n", path.string().c_str());
    }
    close_window();
}

void WorkloadDriver::close_window() {
    static_cast<void>(handle_.request_close());
}

} // namespace chat_stress
