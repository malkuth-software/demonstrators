#pragma once

// The windowed workloads (CHAT_STRESS_SPEC section 7): a scripted input
// sequence driven by a Timer on the window's thread, one step per tick
// (16 ms), recording a sample per tick and writing one JSON record at the
// end, then closing the window. docs/WORKLOADS.md lists each procedure and
// how this driver realises it.
//
// The driver's own timer wakes the run loop; the idle workloads stop it
// for the observation and count exactly one wake of their own (the
// one-shot that ends the observation), which the record subtracts.
//
// Ownership: a component owned by the chat window (ChatWindow), borrowing
// the window and its root control, which outlive it. One thread.

#include "chat_view.hpp"
#include "measurement.hpp"
#include "options.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/component.hpp>
#include <gui_forms/timer.hpp>
#include <gui_forms/window/window.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace chat_stress {

enum class Workload : std::uint8_t {
    startup,
    memory,
    idle,
    idle_focused,
    wheel,
    trackpad,
    page_down,
    thumb,
    jump,
    append,
    edit,
};

[[nodiscard]] std::optional<Workload> workload_named(std::string_view name) noexcept;
[[nodiscard]] const char* workload_name(Workload workload) noexcept;

// One tick's sample: deltas since the previous tick.
struct FrameSample final {
    std::uint32_t phase{};
    double interval_ms{};
    std::uint64_t rows_measured{};
    std::uint64_t rows_painted{};
    std::uint64_t scroll_copies{};
    std::uint64_t scroll_repaints{};
    std::uint64_t frames_presented{};
    double painted_area{};
    std::uint64_t text_shaping_calls{};
    std::uint64_t ring_misses{};
    std::uint64_t timestamp_hash_calls{};
    std::uint64_t semantic_children{};
    std::uint64_t index_runs{};
    std::uint64_t index_bytes{};
    // GUI.Forms' counted allocation paths over the tick, every thread
    // (allocation_statistics totals): not the process's allocations.
    std::uint64_t gui_forms_allocations{};
    std::uint64_t gui_forms_allocated_bytes{};
};

class WorkloadDriver final : public gui_forms::Component {
public:
    static constexpr std::chrono::milliseconds tick_interval{16};

    WorkloadDriver(gui_forms::Window& window, ChatRoot& root, Workload workload, const AppOptions& options);
    ~WorkloadDriver() override;

    // Called once the window is ready (ApplicationWindowOptions::ready):
    // starts the script.
    void start(gui_forms::ApplicationWindowHandle handle);
    [[nodiscard]] bool finished() const noexcept { return finished_; }

private:
    void on_tick();
    // One step of the current workload; returns false when it is done.
    bool step();
    bool step_scroll(Workload kind, std::uint64_t tick);
    bool step_jump(std::uint64_t tick);
    bool step_append(std::uint64_t tick);
    bool step_edit(std::uint64_t tick);
    bool step_idle(std::uint64_t tick);
    bool step_startup();
    bool step_memory(std::uint64_t tick);
    void record_sample();
    void finish();
    void close_window();

    // Input as a host would deliver it.
    void wheel(double delta, gui_forms::WheelDeltaUnit unit);
    void key(std::uint32_t physical_key, gui_forms::Modifier modifiers);
    [[nodiscard]] std::uint64_t random(std::uint64_t draw) const noexcept;
    void go_to_middle(std::uint64_t draw);

    gui_forms::Window& window_;
    ChatRoot& root_;
    Workload workload_;
    AppOptions options_;
    gui_forms::Timer timer_;
    gui_forms::ApplicationWindowHandle handle_{};
    bool started_{};
    bool finished_{};
    std::uint64_t tick_{};
    std::uint64_t phase_tick_{};
    std::uint32_t phase_{};
    std::uint64_t draws_{};
    std::chrono::steady_clock::time_point last_tick_{};
    std::chrono::steady_clock::time_point ready_time_{};
    std::vector<FrameSample> samples_{};
    CounterSnapshot previous_{};
    CounterSnapshot start_counters_{};

    // Workload results beyond the samples.
    std::optional<std::int64_t> process_start_us_{};
    std::int64_t ready_us_{};
    std::optional<std::int64_t> first_present_us_{};
    std::optional<std::int64_t> ready_to_use_us_{};
    std::uint64_t presents_at_condition_{};
    bool condition_met_{};
    double trackpad_velocity_{};
    std::uint64_t appended_fraction_{};
    double anchor_drift_max_{};
    std::uint64_t anchor_checks_{};
    std::uint64_t skipped_edits_{};
    std::optional<CounterSnapshot> idle_before_{};
    std::chrono::steady_clock::time_point idle_start_{};
    double idle_seconds_{};
    std::string text_scratch_{};
};

} // namespace chat_stress
