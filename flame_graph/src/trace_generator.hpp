#pragma once

// A seeded synthetic trace, for the workloads and tests: per thread, a run
// of top-level spans, each filled with nested children (gaps between them,
// some recursion, a skewed choice among `names` frame names), so the flame
// graph has wide and narrow cells and the timeline busy and quiet stretches.
// splitmix64 drives every choice, so a seed gives the same trace on every
// platform (integer arithmetic only).
//
// The same spans can go straight into a TraceModel (generate_trace) or out
// as Chrome trace JSON text (write_chrome_trace, X events plus thread_name
// metadata); loading that text gives the same model, which the load
// workload and loader_tests check.

#include "trace_model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace flame_graph {

struct GeneratorOptions final {
    std::uint64_t seed{1U};
    std::size_t threads{4U};
    std::size_t spans_per_thread{20000U};
    std::size_t names{400U};
    std::size_t maximum_depth{24U};
};

// The bounds the command line enforces.
inline constexpr std::size_t maximum_generated_threads = 64U;
inline constexpr std::size_t maximum_generated_spans = 2'000'000U;

struct GeneratedSpan final {
    std::uint32_t thread{};
    std::uint32_t name{};
    std::int64_t start{};
    std::int64_t duration{};
};

// The spans in generation order (each thread's in start order), into
// `spans` (cleared first, reserved once).
void generate_spans(const GeneratorOptions& options, std::vector<GeneratedSpan>& spans);
// The frame name of index `name` ("render::step_17"), into `out`.
void generated_name(std::uint32_t name, std::string& out);
// Clears the model, adds the spans and finishes it.
void generate_trace(const GeneratorOptions& options, TraceModel& model);
// The same trace as Chrome trace JSON, into `out` (cleared first).
void write_chrome_trace(const GeneratorOptions& options, std::string& out);

} // namespace flame_graph
