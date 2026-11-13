#pragma once

// LiveFeed: the producer of the streaming plot (ImPlot's real-time demo,
// behaviour only). One producer thread writes (x, y) samples into a
// plots::SeriesRingChannel through the channel's single write lease; the
// plot consumes them on its own clock (the motion clock, or the live
// sub-surface's pacer). The feed never touches the UI thread.
//
// Asynchronous work, named:
// - start(channel) acquires the write lease (refused, false, when another
//   lease is held), then starts the thread;
// - the thread writes one block of `block_samples` samples per period and
//   sleeps until the next period; x is the sample's time in seconds from
//   the feed's start (sample n at n / sample_rate), so x is monotone;
// - pause(true) keeps the thread but writes nothing (the plot goes idle);
// - stop() (and the destructor) asks the thread to end, joins it and
//   releases the lease. After stop() returns no write is in flight.
//
// The samples depend only on their index (produce_block), so a test can
// write blocks without the thread and check what the consumer takes.

#include <gui_forms/plots/series_ring_channel/series_ring_channel.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace implot_class {

struct LiveFeedCounters final {
    std::uint64_t blocks_written{};
    std::uint64_t samples_written{};
    std::uint64_t paused_periods{};
};

class LiveFeed final {
public:
    static constexpr double sample_rate = 2000.0;
    static constexpr std::size_t block_samples = 32U;

    LiveFeed();
    ~LiveFeed();
    LiveFeed(const LiveFeed&) = delete;
    LiveFeed& operator=(const LiveFeed&) = delete;

    // Writes the samples [first, first + count) into `out` (interleaved x,
    // y; out.size() >= 2 * count, or nothing is written). Pure.
    static void produce_block(std::uint64_t first, std::size_t count, std::span<double> out) noexcept;

    // Starts the producer on `channel` (two streams). False when the
    // channel refuses a lease or the feed already runs.
    bool start(const std::shared_ptr<gui_forms::plots::SeriesRingChannel>& channel);
    void pause(bool paused) noexcept { paused_.store(paused, std::memory_order_relaxed); }
    [[nodiscard]] bool paused() const noexcept { return paused_.load(std::memory_order_relaxed); }
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept { return thread_.joinable(); }
    // Read from any thread (relaxed counters).
    [[nodiscard]] LiveFeedCounters counters() const noexcept;

    // Without the thread (tests and the headless workloads): acquires a
    // lease if none is held, writes `blocks` blocks from the feed's next
    // sample and keeps the lease. False when no lease could be taken.
    bool write_blocks(const std::shared_ptr<gui_forms::plots::SeriesRingChannel>& channel, std::size_t blocks);

private:
    void run() noexcept;

    std::shared_ptr<gui_forms::plots::SeriesRingChannel> channel_{};
    std::unique_ptr<gui_forms::plots::SeriesWriteLease> lease_{};
    // block_samples * 2 doubles: the block being written, reused.
    std::vector<double> block_{};
    std::uint64_t next_sample_{};
    std::atomic<bool> stopping_{};
    std::atomic<bool> paused_{};
    std::atomic<std::uint64_t> blocks_written_{};
    std::atomic<std::uint64_t> samples_written_{};
    std::atomic<std::uint64_t> paused_periods_{};
    // Declared last: started after every member it reads exists, joined in
    // stop() before any of them goes.
    std::thread thread_{};
};

} // namespace implot_class
