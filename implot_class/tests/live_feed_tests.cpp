// The live feed without its thread: produce_block is pure and continuous
// across blocks, write_blocks publishes whole samples through a
// SeriesRingChannel, and the consumer side takes them into ring columns
// in order; a second producer is refused while the feed holds the lease;
// the thread starts, writes and stops. Headless.

#include "../src/live_feed.hpp"
#include "test_support.hpp"

#include <gui_forms/plots/series_column/series_column.hpp>
#include <gui_forms/plots/series_ring_channel/series_ring_channel.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace gp = gui_forms::plots;
using implot_class::LiveFeed;
using implot_class_tests::require;

namespace {

void test_produce_block() {
    std::vector<double> whole(2U * 64U);
    LiveFeed::produce_block(0U, 64U, whole);
    std::vector<double> halves(2U * 64U);
    LiveFeed::produce_block(0U, 32U, std::span<double>(halves).first(64U));
    LiveFeed::produce_block(32U, 32U, std::span<double>(halves).subspan(64U));
    require(whole == halves, "blocks are continuous: the same samples whatever the split");
    for (std::size_t index = 1U; index < 64U; ++index) {
        require(whole[2U * index] > whole[2U * (index - 1U)], "time increases");
    }
    require(std::fabs(whole[2U] - 1.0 / LiveFeed::sample_rate) < 1e-12, "x is seconds at the sample rate");
    std::vector<double> small(10U, -7.0);
    LiveFeed::produce_block(0U, 32U, small);
    require(small[0] == -7.0, "too small an output writes nothing");
}

void test_write_and_consume() {
    std::shared_ptr<gp::SeriesRingChannel> channel = std::make_shared<gp::SeriesRingChannel>(4096U, 2U);
    LiveFeed feed{};
    require(feed.write_blocks(channel, 10U), "the feed takes the lease and writes");
    require((*channel).published() == 10U * LiveFeed::block_samples, "every sample is published");
    require((*channel).acquire_write_lease() == nullptr, "a second producer is refused while the feed holds the lease");
    gp::SeriesColumn<double> xs(gp::SeriesMode::ring, 1024U, gp::XOrder::monotone);
    gp::SeriesColumn<double> ys(gp::SeriesMode::ring, 1024U, gp::XOrder::unordered);
    std::array<gp::SeriesColumn<double>*, 2> columns{&xs, &ys};
    std::vector<double> scratch((*channel).capacity() * (*channel).stream_count());
    const std::size_t taken = (*channel).consume(std::span<gp::SeriesColumn<double>* const>(columns), scratch);
    require(taken == 10U * LiveFeed::block_samples, "the consumer takes every sample");
    require(xs.length() == taken && ys.length() == taken, "both columns hold them");
    std::vector<double> expected(2U * taken);
    LiveFeed::produce_block(0U, taken, expected);
    const gp::SeriesSpans<double> read = ys.read();
    for (std::size_t index = 0U; index < taken; ++index) {
        require(read.at(gp::ElementIndex{index}) == expected[2U * index + 1U], "the samples arrive in order");
    }
    require(feed.write_blocks(channel, 2U), "the feed keeps its lease");
    require((*channel).consume(std::span<gp::SeriesColumn<double>* const>(columns), scratch) == 2U * LiveFeed::block_samples,
            "a later frame takes only the new samples");
    require((*channel).statistics().overrun == 0U, "nothing was overrun");
    feed.stop();
    std::unique_ptr<gp::SeriesWriteLease> next = (*channel).acquire_write_lease();
    require(next != nullptr, "stopping the feed released its lease");
}

void test_thread() {
    std::shared_ptr<gp::SeriesRingChannel> channel = std::make_shared<gp::SeriesRingChannel>(65536U, 2U);
    LiveFeed feed{};
    require(feed.start(channel), "the producer starts");
    require(!feed.start(channel), "a running feed refuses a second start");
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((*channel).published() < 4U * LiveFeed::block_samples && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    feed.stop();
    require(!feed.running(), "stop joins the thread");
    require((*channel).published() >= 4U * LiveFeed::block_samples, "the thread wrote blocks");
    require(feed.counters().samples_written == (*channel).published(), "the counters match what was published");
}

} // namespace

int main() {
    try {
        test_produce_block();
        test_write_and_consume();
        test_thread();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "live_feed_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("live_feed_tests: passed");
    return 0;
}
