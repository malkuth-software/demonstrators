#include "trace_generator.hpp"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <string_view>

namespace flame_graph {

namespace {

constexpr std::array<std::string_view, 8> modules{"app", "render", "layout", "io", "net", "gc", "text", "image"};

class SplitMix final {
public:
    explicit SplitMix(std::uint64_t seed) noexcept : state_(seed) {}

    std::uint64_t next() noexcept {
        state_ += 0x9e3779b97f4a7c15ULL;
        std::uint64_t mixed = state_;
        mixed = (mixed ^ (mixed >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        mixed = (mixed ^ (mixed >> 27U)) * 0x94d049bb133111ebULL;
        return mixed ^ (mixed >> 31U);
    }
    // Uniform in [0, bound) for bound > 0 (the modulo bias is immaterial
    // for a synthetic workload and keeps the trace integer-exact).
    std::uint64_t below(std::uint64_t bound) noexcept { return next() % bound; }

private:
    std::uint64_t state_{};
};

struct OpenFrame final {
    std::int64_t end{};
    std::int64_t cursor{};
    std::uint32_t name{};
    std::size_t depth{};
};

// A skewed name choice: the square of a uniform draw, so low indices are
// common (hot functions) and high ones rare.
std::uint32_t pick_name(SplitMix& random, std::size_t names) noexcept {
    const std::uint64_t draw = random.below(1024U);
    const std::uint64_t index = (draw * draw * static_cast<std::uint64_t>(names)) / (1024U * 1024U);
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(index, names - 1U));
}

} // namespace

void generated_name(std::uint32_t name, std::string& out) {
    out.assign(modules[name % modules.size()]);
    out.append("::step_");
    out.append(std::to_string(name));
}

void generate_spans(const GeneratorOptions& options, std::vector<GeneratedSpan>& spans) {
    spans.clear();
    const std::size_t threads = std::clamp<std::size_t>(options.threads, 1U, maximum_generated_threads);
    const std::size_t per_thread = std::min(options.spans_per_thread, maximum_generated_spans / threads);
    const std::size_t names = std::max<std::size_t>(options.names, 1U);
    const std::size_t depth_limit = std::clamp<std::size_t>(options.maximum_depth, 1U, 256U);
    spans.reserve(threads * per_thread);
    std::vector<OpenFrame> open{};
    open.reserve(depth_limit + 1U);
    SplitMix random(options.seed);
    for (std::size_t thread = 0U; thread < threads; ++thread) {
        std::size_t made = 0U;
        std::int64_t cursor = static_cast<std::int64_t>(random.below(20'000U));
        while (made < per_thread) {
            // A top-level span of 50 us to 5 ms, in nanoseconds.
            const std::int64_t duration = 50'000 + static_cast<std::int64_t>(random.below(4'950'000U));
            const std::uint32_t top_name = pick_name(random, names);
            spans.push_back(GeneratedSpan{static_cast<std::uint32_t>(thread), top_name, cursor, duration});
            ++made;
            open.clear();
            open.push_back(OpenFrame{cursor + duration, cursor, top_name, 0U});
            while (!open.empty() && made < per_thread) {
                OpenFrame& frame = open.back();
                const bool deep = frame.depth + 1U >= depth_limit;
                const bool stop = random.below(100U) < 28U;
                const std::int64_t gap = static_cast<std::int64_t>(random.below(2'000U));
                const std::int64_t child_start = frame.cursor + gap;
                const std::int64_t room = frame.end - child_start;
                if (deep || stop || room < 2) {
                    open.pop_back();
                    continue;
                }
                const std::int64_t share = 10 + static_cast<std::int64_t>(random.below(60U));
                const std::int64_t child_duration = std::max<std::int64_t>(1, room * share / 100);
                // One child in twenty calls its parent again (recursion).
                const bool recursive = random.below(20U) == 0U;
                const std::uint32_t child_name = recursive ? frame.name : pick_name(random, names);
                const std::size_t child_depth = frame.depth + 1U;
                frame.cursor = child_start + child_duration;
                spans.push_back(
                    GeneratedSpan{static_cast<std::uint32_t>(thread), child_name, child_start, child_duration});
                ++made;
                // `frame` is not used after this push (it may move).
                open.push_back(OpenFrame{child_start + child_duration, child_start, child_name, child_depth});
            }
            cursor += duration + static_cast<std::int64_t>(random.below(200'000U));
        }
    }
}

void generate_trace(const GeneratorOptions& options, TraceModel& model) {
    std::vector<GeneratedSpan> spans{};
    generate_spans(options, spans);
    model.clear();
    model.set_unit(TraceUnit::nanoseconds);
    const std::size_t names = std::max<std::size_t>(options.names, 1U);
    model.names().reserve(names + maximum_generated_threads + 2U, names * 20U);
    std::string text{};
    text.reserve(64U);
    std::vector<NameId> ids(names);
    for (std::size_t index = 0U; index < names; ++index) {
        generated_name(static_cast<std::uint32_t>(index), text);
        ids[index] = model.names().intern(text).value_or(NameId{});
    }
    const std::size_t threads = std::clamp<std::size_t>(options.threads, 1U, maximum_generated_threads);
    std::vector<ThreadIndex> lanes{};
    lanes.reserve(threads);
    for (std::size_t thread = 0U; thread < threads; ++thread) {
        const std::optional<ThreadIndex> lane = model.thread(1, static_cast<std::int64_t>(thread + 1U));
        text.assign("worker ");
        text.append(std::to_string(thread + 1U));
        const std::optional<NameId> label = model.names().intern(text);
        if (lane.has_value() && label.has_value()) model.set_thread_name(*lane, *label);
        lanes.push_back(lane.value_or(ThreadIndex{}));
    }
    model.reserve_spans(spans.size());
    for (const GeneratedSpan& span : spans) {
        static_cast<void>(model.add_span(lanes[span.thread], span.start, span.duration, ids[span.name]));
    }
    model.finish();
}

void write_chrome_trace(const GeneratorOptions& options, std::string& out) {
    std::vector<GeneratedSpan> spans{};
    generate_spans(options, spans);
    out.clear();
    // About 110 bytes per event.
    out.reserve(spans.size() * 112U + 4096U);
    out.append("{\"traceEvents\":[");
    // Each event after the first is preceded by its comma.
    bool first = true;
    const std::size_t threads = std::clamp<std::size_t>(options.threads, 1U, maximum_generated_threads);
    std::array<char, 256> line{};
    for (std::size_t thread = 0U; thread < threads; ++thread) {
        const int written = std::snprintf(line.data(), line.size(),
                                          "%s\n{\"ph\":\"M\",\"name\":\"thread_name\",\"pid\":1,\"tid\":%zu,"
                                          "\"args\":{\"name\":\"worker %zu\"}}",
                                          first ? "" : ",", thread + 1U, thread + 1U);
        first = false;
        if (written > 0) out.append(line.data(), static_cast<std::size_t>(written));
    }
    std::string name{};
    name.reserve(64U);
    for (const GeneratedSpan& span : spans) {
        generated_name(span.name, name);
        // Microseconds with three decimals: the nanoseconds exactly.
        const int written = std::snprintf(
            line.data(), line.size(),
            "%s\n{\"ph\":\"X\",\"name\":\"%s\",\"pid\":1,\"tid\":%" PRIu32 ",\"ts\":%" PRId64
            ".%03" PRId64 ",\"dur\":%" PRId64 ".%03" PRId64 "}",
            first ? "" : ",", name.c_str(), span.thread + 1U, span.start / 1000, span.start % 1000,
            span.duration / 1000, span.duration % 1000);
        first = false;
        if (written > 0) out.append(line.data(), static_cast<std::size_t>(written));
    }
    out.append("\n],\"displayTimeUnit\":\"ns\"}\n");
}

} // namespace flame_graph
