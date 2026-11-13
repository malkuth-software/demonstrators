#include "live_feed.hpp"

#include "series_data.hpp"

#include <chrono>
#include <cmath>

namespace implot_class {

namespace gp = gui_forms::plots;

LiveFeed::LiveFeed() {
    block_.resize(block_samples * 2U);
}

LiveFeed::~LiveFeed() {
    stop();
}

void LiveFeed::produce_block(std::uint64_t first, std::size_t count, std::span<double> out) noexcept {
    if (count > out.size() / 2U) return;
    for (std::size_t index = 0U; index < count; ++index) {
        const std::uint64_t sample = first + index;
        const double time = static_cast<double>(sample) / sample_rate;
        // Two tones and a little deterministic noise keyed by the sample.
        SplitMix64 noise{sample * 0x2545F4914F6CDD1DULL};
        const double value = 0.8 * std::sin(2.0 * 3.141592653589793 * 0.7 * time) +
                             0.3 * std::sin(2.0 * 3.141592653589793 * 3.1 * time) + 0.08 * (noise.uniform() - 0.5);
        out[index * 2U] = time;
        out[index * 2U + 1U] = value;
    }
}

bool LiveFeed::start(const std::shared_ptr<gp::SeriesRingChannel>& channel) {
    if (thread_.joinable() || channel == nullptr || (*channel).stream_count() != 2U) return false;
    if (lease_ == nullptr || channel_ != channel) {
        lease_.reset();
        lease_ = (*channel).acquire_write_lease();
        if (lease_ == nullptr) return false;
        channel_ = channel;
    }
    stopping_.store(false, std::memory_order_relaxed);
    thread_ = std::thread(&LiveFeed::run, this);
    return true;
}

void LiveFeed::stop() noexcept {
    stopping_.store(true, std::memory_order_relaxed);
    if (thread_.joinable()) thread_.join();
    lease_.reset();
    channel_.reset();
}

void LiveFeed::run() noexcept {
    const std::chrono::steady_clock::duration period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(static_cast<double>(block_samples) / sample_rate));
    std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now();
    while (!stopping_.load(std::memory_order_relaxed)) {
        if (paused_.load(std::memory_order_relaxed)) {
            paused_periods_.fetch_add(1U, std::memory_order_relaxed);
        } else {
            produce_block(next_sample_, block_samples, block_);
            (*lease_).write(block_);
            next_sample_ += block_samples;
            blocks_written_.fetch_add(1U, std::memory_order_relaxed);
            samples_written_.fetch_add(block_samples, std::memory_order_relaxed);
        }
        next += period;
        std::this_thread::sleep_until(next);
    }
}

bool LiveFeed::write_blocks(const std::shared_ptr<gp::SeriesRingChannel>& channel, std::size_t blocks) {
    if (thread_.joinable() || channel == nullptr || (*channel).stream_count() != 2U) return false;
    if (lease_ == nullptr || channel_ != channel) {
        lease_.reset();
        lease_ = (*channel).acquire_write_lease();
        if (lease_ == nullptr) return false;
        channel_ = channel;
    }
    for (std::size_t block = 0U; block < blocks; ++block) {
        produce_block(next_sample_, block_samples, block_);
        (*lease_).write(block_);
        next_sample_ += block_samples;
        blocks_written_.fetch_add(1U, std::memory_order_relaxed);
        samples_written_.fetch_add(block_samples, std::memory_order_relaxed);
    }
    return true;
}

LiveFeedCounters LiveFeed::counters() const noexcept {
    LiveFeedCounters counters{};
    counters.blocks_written = blocks_written_.load(std::memory_order_relaxed);
    counters.samples_written = samples_written_.load(std::memory_order_relaxed);
    counters.paused_periods = paused_periods_.load(std::memory_order_relaxed);
    return counters;
}

} // namespace implot_class
