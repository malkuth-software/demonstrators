#pragma once

// Trace loaders: Chrome trace JSON and folded stacks, into a TraceModel.
//
// Chrome trace (the Trace Event Format): the top value is an array of
// events, or an object whose "traceEvents" member is one (other members
// are skipped). Per event, by "ph":
//   X  a complete span: ts and dur, in microseconds (fractions kept to the
//      nanosecond, rounded);
//   B, E  begin and end, paired per thread by a stack: an E with no open B
//      is counted (unmatched_ends) and dropped; a B still open at the end
//      is closed at the thread's latest timestamp (unclosed_begins);
//   M  metadata: "thread_name" with args.name names the thread;
//   anything else (instants, counters, async, flows) is counted and
//      skipped.
// pid and tid may be numbers or strings (a string that is not an integer
// is identified by its FNV-1a hash).
//
// Folded stacks (Brendan Gregg's stackcollapse output): one line per stack,
// frames from the root joined by ';', a space, then a sample count. Blank
// lines and lines starting with '#' are skipped; a line without a count is
// counted (malformed_lines). Every stack is laid on one lane, "samples",
// one unit per sample: consecutive lines sharing a prefix extend those
// frames' spans, so the timeline shows the file's order and the flame
// graph its merged tree. The unit is samples.
//
// Both loaders clear the model first and call finish() when they accepted
// the text; on a refusal the model is cleared (no partial trace is left).
// Ownership: the text is borrowed for the call. One thread.

#include "json_reader.hpp"
#include "trace_model.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace flame_graph {

enum class TraceFormat : std::uint8_t { automatic, chrome_json, folded };

struct LoadReport final {
    bool loaded{};
    TraceFormat format{TraceFormat::automatic};
    // Empty when loaded; otherwise what refused the text.
    std::string message{};
    JsonError json_error{};
    std::size_t bytes{};
    // Chrome.
    std::size_t events{};
    std::size_t complete_events{};
    std::size_t begin_events{};
    std::size_t end_events{};
    std::size_t unmatched_ends{};
    std::size_t unclosed_begins{};
    std::size_t metadata_events{};
    std::size_t other_events{};
    std::size_t malformed_events{};
    // Folded.
    std::size_t lines{};
    std::size_t malformed_lines{};
    std::size_t samples{};
    // Both: what the model refused (span or name limits).
    std::size_t spans_refused{};
    std::size_t names_refused{};
    std::size_t threads_refused{};
};

// The largest file load_trace_file reads.
inline constexpr std::size_t maximum_trace_bytes = 512U * 1024U * 1024U;

// chrome_json when the first non-blank byte opens an array or object,
// folded otherwise.
[[nodiscard]] TraceFormat detect_format(std::string_view text) noexcept;
[[nodiscard]] std::string_view format_name(TraceFormat format) noexcept;

LoadReport load_chrome_trace(std::string_view text, TraceModel& model);
LoadReport load_folded_stacks(std::string_view text, TraceModel& model);
// Detects the format when `format` is automatic.
LoadReport load_trace_text(std::string_view text, TraceFormat format, TraceModel& model);
// Reads the file into `buffer` (reused; its old contents are lost) and
// loads it. Refuses files over maximum_trace_bytes.
LoadReport load_trace_file(const std::filesystem::path& path, TraceFormat format, TraceModel& model,
                           std::string& buffer);

} // namespace flame_graph
