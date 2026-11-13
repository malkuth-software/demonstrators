// The loaders and the JSON reader, without GUI.Forms controls: tokens,
// escapes and surrogate pairs, error positions, the depth limit and
// skipping; Chrome traces (X, B/E pairing with unmatched and unclosed
// events, thread_name metadata, string ids, the traceEvents wrapper,
// refusals leaving an empty model); folded stacks (comments, malformed
// lines, merged prefixes); and the generator's JSON loading back into the
// same trace it builds directly.

#include "../src/json_reader.hpp"
#include "../src/trace_generator.hpp"
#include "../src/trace_loader.hpp"
#include "../src/trace_model.hpp"
#include "test_support.hpp"

#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

using flame_graph::CallNodeIndex;
using flame_graph::JsonReader;
using flame_graph::JsonToken;
using flame_graph::LoadReport;
using flame_graph::TraceFormat;
using flame_graph::TraceModel;
using flame_graph_tests::require;

namespace {

std::optional<CallNodeIndex> child_named(const TraceModel& model, CallNodeIndex parent, std::string_view text) {
    for (const std::uint32_t child : model.children(parent)) {
        if (model.names().text(model.node(CallNodeIndex{child}).name) == text) return CallNodeIndex{child};
    }
    return std::nullopt;
}

void reader_tokens() {
    const std::string_view text =
        "{\"a\":[1,2.5,-3e2,true,false,null,\"x\\u00e9\\ud83d\\ude00\\n\"],\"b\":{}}";
    JsonReader reader(text);
    require(reader.next() == JsonToken::begin_object, "object");
    require(reader.next() == JsonToken::key && reader.string() == "a", "key a");
    require(reader.next() == JsonToken::begin_array, "array");
    require(reader.next() == JsonToken::number && reader.integer() == 1, "integer 1");
    require(reader.next() == JsonToken::number && reader.number() == 2.5 && !reader.integer().has_value(),
            "2.5 is no integer");
    require(reader.next() == JsonToken::number && reader.number() == -300.0, "an exponent");
    require(reader.next() == JsonToken::true_value, "true");
    require(reader.next() == JsonToken::false_value, "false");
    require(reader.next() == JsonToken::null_value, "null");
    require(reader.next() == JsonToken::string, "a string");
    require(reader.string() == "x\xc3\xa9\xf0\x9f\x98\x80\n", "escapes, \\u and a surrogate pair decoded");
    require(reader.next() == JsonToken::end_array, "end of array");
    require(reader.next() == JsonToken::key && reader.string() == "b", "key b");
    require(reader.skip_value(), "an empty object skipped");
    require(reader.next() == JsonToken::end_object, "end of object");
    require(reader.next() == JsonToken::end && !reader.failed(), "the end");

    JsonReader lone("\"\\ud800x\"");
    require(lone.next() == JsonToken::string && lone.string() == "\xef\xbf\xbdx", "a lone surrogate is U+FFFD");
}

void reader_errors() {
    JsonReader trailing("{\"a\":1,}");
    while (trailing.next() != JsonToken::error) {
    }
    require(trailing.error().line == 1U && trailing.error().offset == 7U, "a trailing comma fails at the brace");

    JsonReader lines("[1,\n 2,\n x]");
    while (lines.next() != JsonToken::error) {
    }
    require(lines.error().line == 3U && lines.error().column == 2U, "line and column of the bad character");
    require(lines.next() == JsonToken::error, "the reader stays failed");

    std::string deep(JsonReader::maximum_depth + 1U, '[');
    deep.append(JsonReader::maximum_depth + 1U, ']');
    JsonReader nested(deep);
    bool failed = false;
    for (std::size_t step = 0U; step < deep.size() + 2U && !failed; ++step) failed = nested.next() == JsonToken::error;
    require(failed, "nesting past the limit fails");

    JsonReader after("1 2");
    require(after.next() == JsonToken::number && after.next() == JsonToken::error, "text after the top value fails");

    JsonReader skipping("{\"x\":{\"y\":[1,{\"z\":2}]},\"w\":3}");
    require(skipping.next() == JsonToken::begin_object && skipping.next() == JsonToken::key, "first key");
    require(skipping.skip_value(), "a nested value skipped");
    require(skipping.next() == JsonToken::key && skipping.string() == "w", "the next key follows");
}

void chrome_trace() {
    const std::string_view text = R"([
 {"ph":"M","name":"thread_name","pid":1,"tid":7,"args":{"name":"main thread"}},
 {"ph":"X","name":"main","pid":1,"tid":7,"ts":0,"dur":100},
 {"ph":"X","name":"parse","pid":1,"tid":7,"ts":10.5,"dur":20.25},
 {"ph":"B","name":"io","pid":"2","tid":"worker","ts":5},
 {"ph":"E","pid":"2","tid":"worker","ts":15},
 {"ph":"E","pid":"2","tid":"worker","ts":16},
 {"ph":"B","name":"open","pid":"2","tid":"worker","ts":20},
 {"ph":"X","name":"late","pid":"2","tid":"worker","ts":30,"dur":2},
 {"ph":"i","name":"instant","pid":1,"tid":7,"ts":3},
 {"ph":"C","name":"counter","pid":1,"tid":7,"ts":3,"args":{"value":[1,2,{"x":3}]}}
])";
    TraceModel model{};
    const LoadReport report = flame_graph::load_trace_text(text, TraceFormat::automatic, model);
    require(report.loaded && report.format == TraceFormat::chrome_json, "the trace loads as Chrome JSON");
    require(report.events == 10U, "ten events");
    require(report.complete_events == 3U && report.begin_events == 2U && report.end_events == 2U, "phases counted");
    require(report.unmatched_ends == 1U && report.unclosed_begins == 1U, "an unmatched E and an unclosed B");
    require(report.metadata_events == 1U && report.other_events == 2U, "metadata and others");
    require(model.threads().size() == 2U, "two threads");
    require(model.names().text(model.threads()[0].label) == "main thread", "thread_name names the lane");
    require(model.unit() == flame_graph::TraceUnit::nanoseconds, "nanoseconds");
    const flame_graph::ThreadLane& first = model.threads()[0];
    require(first.spans[1].start == 10'500 && first.spans[1].duration == 20'250, "fractional microseconds kept");
    // Thread 2: io [5, 15) us, open [20, 32) us (closed at the latest
    // time), late [30, 32) us inside it.
    require(model.children(flame_graph::root_node).size() == 2U, "the string-identified thread is a second node");
    const std::optional<CallNodeIndex> main_thread = child_named(model, flame_graph::root_node, "main thread");
    require(main_thread.has_value(), "a thread node per thread");
    require(model.node(*main_thread).total == 100'000, "the main thread weighs main");
    require(model.total_weight() == 122'000, "the root sums both threads");
    const std::optional<std::size_t> open = model.function_of(*model.names().find("open"));
    require(open.has_value() && model.functions()[*open].total == 12'000, "the unclosed B ends at the latest time");

    TraceModel wrapped{};
    const LoadReport wrapper = flame_graph::load_chrome_trace(
        R"({"otherData":{"x":[1,2]},"traceEvents":[{"ph":"X","name":"a","pid":1,"tid":1,"ts":1,"dur":2}],"displayTimeUnit":"ms"})",
        wrapped);
    require(wrapper.loaded && wrapped.total_weight() == 2'000, "the traceEvents wrapper");

    TraceModel refused{};
    const LoadReport number = flame_graph::load_chrome_trace("42", refused);
    require(!number.loaded && !number.message.empty(), "a number is no trace");
    const LoadReport broken =
        flame_graph::load_chrome_trace("[{\"ph\":\"X\",\"name\":\"a\",\"ts\":1,\"dur\":2},{\"ph\":", refused);
    require(!broken.loaded && broken.json_error.offset > 0U, "a broken trace is refused with its position");
    require(!refused.finished() && refused.threads().empty(), "a refusal leaves an empty model");
}

void folded_stacks() {
    const std::string_view text = "# comment\n"
                                  "main;parse;lex 3\n"
                                  "main;parse 2\n"
                                  "\n"
                                  "main;render 5\n"
                                  "bad line without count\n"
                                  "main;parse;lex x\n";
    TraceModel model{};
    const LoadReport report = flame_graph::load_trace_text(text, TraceFormat::automatic, model);
    require(report.loaded && report.format == TraceFormat::folded, "folded stacks are detected");
    require(report.lines == 7U && report.malformed_lines == 2U && report.samples == 10U, "lines, refusals, samples");
    require(model.unit() == flame_graph::TraceUnit::samples, "samples");
    require(model.total_weight() == 10, "ten samples in all");
    const std::optional<CallNodeIndex> main_node = child_named(model, flame_graph::root_node, "main");
    require(main_node.has_value(), "main under the root");
    const std::optional<CallNodeIndex> parse = child_named(model, *main_node, "parse");
    const std::optional<CallNodeIndex> render = child_named(model, *main_node, "render");
    require(parse.has_value() && render.has_value(), "parse and render under main");
    require(model.node(*parse).total == 5 && model.node(*parse).self == 2, "parse holds lex's 3 and its own 2");
    require(model.node(*render).total == 5 && model.node(*render).offset == 5, "render after parse");
    // Shared prefixes extend the open spans: one main span over all ten.
    const flame_graph::ThreadLane& lane = model.threads()[0];
    require(lane.depth_start[1] - lane.depth_start[0] == 1U, "one main span");
    require(lane.spans[0].duration == 10, "main spans every sample");
    require(model.names().text(lane.label) == "samples", "the samples lane");
}

void generator_round_trip() {
    flame_graph::GeneratorOptions options{};
    options.seed = 7U;
    options.threads = 3U;
    options.spans_per_thread = 600U;
    options.names = 50U;
    std::string text{};
    flame_graph::write_chrome_trace(options, text);
    TraceModel loaded{};
    const LoadReport report = flame_graph::load_trace_text(text, TraceFormat::automatic, loaded);
    require(report.loaded, "the generated JSON loads");
    require(report.malformed_events == 0U && report.spans_refused == 0U, "nothing was refused");
    TraceModel direct{};
    flame_graph::generate_trace(options, direct);
    require(loaded.statistics().spans == direct.statistics().spans && direct.statistics().spans == 1800U,
            "the same spans");
    require(loaded.statistics().nodes == direct.statistics().nodes, "the same call tree");
    require(loaded.total_weight() == direct.total_weight(), "the same weight");
    require(loaded.functions().size() == direct.functions().size(), "the same functions");
    for (const flame_graph::FunctionRow& row : direct.functions()) {
        const std::optional<flame_graph::NameId> name = loaded.names().find(direct.names().text(row.name));
        require(name.has_value(), "each function's name is loaded");
        const std::optional<std::size_t> other = loaded.function_of(*name);
        require(other.has_value(), "each function has a loaded row");
        const flame_graph::FunctionRow& loaded_row = loaded.functions()[*other];
        require(loaded_row.total == row.total && loaded_row.self == row.self && loaded_row.calls == row.calls,
                "each function's figures match");
    }
    std::string again{};
    flame_graph::write_chrome_trace(options, again);
    require(again == text, "a seed writes the same text");
}

} // namespace

int main() {
    try {
        reader_tokens();
        reader_errors();
        chrome_trace();
        folded_stacks();
        generator_round_trip();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "loader_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("loader_tests: passed");
    return 0;
}
