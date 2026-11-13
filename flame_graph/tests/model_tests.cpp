// The trace model, the string table and the formats, without GUI.Forms
// controls: depths and the depth grouping, the merged call tree (totals,
// self, calls, name order, offsets), recursion counted once per stack in
// the function rows, clipping of a span that outlives its parent, thread
// nodes, search flags and their weight, the refusals, and the label cut.

#include "../src/profile_ink.hpp"
#include "../src/string_table.hpp"
#include "../src/trace_model.hpp"
#include "test_support.hpp"

#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

using flame_graph::CallNode;
using flame_graph::CallNodeIndex;
using flame_graph::FunctionRow;
using flame_graph::NameId;
using flame_graph::SearchFlags;
using flame_graph::StringTable;
using flame_graph::ThreadIndex;
using flame_graph::TraceModel;
using flame_graph_tests::require;

namespace {

NameId name_of(TraceModel& model, std::string_view text) {
    const std::optional<NameId> name = model.names().intern(text);
    require(name.has_value(), "a name is interned");
    return *name;
}

ThreadIndex lane_of(TraceModel& model, std::int64_t tid) {
    const std::optional<ThreadIndex> lane = model.thread(1, tid);
    require(lane.has_value(), "a lane is made");
    return *lane;
}

// The child of `parent` named `text`, or none.
std::optional<CallNodeIndex> child_named(const TraceModel& model, CallNodeIndex parent, std::string_view text) {
    for (const std::uint32_t child : model.children(parent)) {
        if (model.names().text(model.node(CallNodeIndex{child}).name) == text) return CallNodeIndex{child};
    }
    return std::nullopt;
}

const FunctionRow& function_named(const TraceModel& model, std::string_view text) {
    const std::optional<NameId> name = model.names().find(text);
    require(name.has_value(), "the function's name is known");
    const std::optional<std::size_t> row = model.function_of(*name);
    require(row.has_value(), "the function has a row");
    return model.functions()[*row];
}

// main [0,100): parse [10,50) holding lex [20,30), parse again [60,90).
void build_basic(TraceModel& model) {
    const ThreadIndex lane = lane_of(model, 1);
    const NameId main_name = name_of(model, "main");
    const NameId parse = name_of(model, "parse");
    const NameId lex = name_of(model, "lex");
    // Added out of order: finish sorts.
    require(model.add_span(lane, 60, 30, parse), "a span is added");
    require(model.add_span(lane, 0, 100, main_name), "a span is added");
    require(model.add_span(lane, 20, 10, lex), "a span is added");
    require(model.add_span(lane, 10, 40, parse), "a span is added");
    model.finish();
}

void tree_and_layout() {
    TraceModel model{};
    build_basic(model);
    require(model.finished(), "the model is finished");
    require(model.total_weight() == 100, "the root weighs the single thread's top span");
    const std::optional<CallNodeIndex> main_node = child_named(model, flame_graph::root_node, "main");
    require(main_node.has_value(), "one thread: main sits under the root");
    const std::optional<CallNodeIndex> parse = child_named(model, *main_node, "parse");
    require(parse.has_value(), "parse under main");
    const std::optional<CallNodeIndex> lex = child_named(model, *parse, "lex");
    require(lex.has_value(), "lex under parse");
    const CallNode& parse_node = model.node(*parse);
    require(parse_node.total == 70 && parse_node.calls == 2U, "both parse calls merge into one node");
    require(parse_node.self == 60, "parse's self is its total less lex");
    require(model.node(*main_node).self == 30, "main's self is its total less parse");
    require(model.node(*lex).depth == 3U, "lex is three below the root");
    require(model.node(*lex).offset == 0 && parse_node.offset == 0, "first children start at their parent's offset");
    require(model.statistics().nodes == 4U, "root, main, parse, lex");
    // The lane: grouped by depth, start order within a depth.
    const flame_graph::ThreadLane& lane = model.threads()[0];
    require(lane.max_depth == 2U, "three depths");
    require(lane.depth_start.size() == 4U, "max_depth + 2 row starts");
    require(lane.depth_start[0] == 0U && lane.depth_start[1] == 1U && lane.depth_start[2] == 3U &&
                lane.depth_start[3] == 4U,
            "one main, two parse, one lex");
    require(lane.spans[1].start == 10 && lane.spans[2].start == 60, "depth 1 in start order");
    require(lane.spans[3].node == *lex, "each span knows its node");
    require(model.first_time() == 0 && model.last_time() == 100, "the trace's extent");
    // Function rows.
    require(model.functions().size() == 3U, "three functions");
    const FunctionRow& parse_row = function_named(model, "parse");
    require(parse_row.total == 70 && parse_row.self == 60 && parse_row.calls == 2U, "parse's row");
}

void name_order_and_offsets() {
    TraceModel model{};
    const ThreadIndex lane = lane_of(model, 1);
    const NameId top = name_of(model, "top");
    const NameId zeta = name_of(model, "zeta");
    const NameId alpha = name_of(model, "alpha");
    require(model.add_span(lane, 0, 100, top), "top");
    require(model.add_span(lane, 0, 30, zeta), "zeta first in time");
    require(model.add_span(lane, 40, 50, alpha), "alpha later");
    model.finish();
    const CallNodeIndex top_node = *child_named(model, flame_graph::root_node, "top");
    const std::span<const std::uint32_t> children = model.children(top_node);
    require(children.size() == 2U, "two children");
    require(model.names().text(model.node(CallNodeIndex{children[0]}).name) == "alpha", "children in name order");
    require(model.node(CallNodeIndex{children[0]}).offset == 0, "alpha first");
    require(model.node(CallNodeIndex{children[1]}).offset == 50, "zeta after alpha's total");
}

void recursion_counted_once() {
    TraceModel model{};
    const ThreadIndex lane = lane_of(model, 1);
    const NameId f = name_of(model, "f");
    const NameId g = name_of(model, "g");
    require(model.add_span(lane, 0, 100, f), "outer f");
    require(model.add_span(lane, 10, 50, f), "inner f");
    require(model.add_span(lane, 20, 10, g), "g");
    model.finish();
    const FunctionRow& f_row = function_named(model, "f");
    require(f_row.total == 100, "a recursive function's total counts the outer call only");
    require(f_row.self == 90 && f_row.calls == 2U, "self and calls sum over both");
    require(function_named(model, "g").total == 10, "g's total");
}

void clipping_and_refusals() {
    TraceModel model{};
    const ThreadIndex lane = lane_of(model, 1);
    const NameId a = name_of(model, "a");
    const NameId b = name_of(model, "b");
    require(model.add_span(lane, 0, 10, a), "a");
    require(model.add_span(lane, 5, 15, b), "b outlives a");
    require(!model.add_span(lane, 0, -1, a), "a negative duration is refused");
    require(!model.add_span(ThreadIndex{7U}, 0, 1, a), "a stale thread is refused");
    model.finish();
    require(model.statistics().overlaps_clipped == 1U, "the overlap is counted");
    const CallNodeIndex a_node = *child_named(model, flame_graph::root_node, "a");
    const CallNodeIndex b_node = *child_named(model, a_node, "b");
    require(model.node(b_node).total == 5, "b is clipped to a's end");
    // Cleared and reused.
    model.clear();
    require(!model.finished() && model.threads().empty() && model.names().size() == 0U, "clear forgets the trace");
    build_basic(model);
    require(model.total_weight() == 100, "the cleared model builds again");
}

void thread_nodes() {
    TraceModel model{};
    const ThreadIndex first = lane_of(model, 1);
    const ThreadIndex second = lane_of(model, 2);
    require(lane_of(model, 1) == first, "the same pid and tid give the same lane");
    model.set_thread_name(first, name_of(model, "main thread"));
    const NameId work = name_of(model, "work");
    require(model.add_span(first, 0, 40, work), "first lane");
    require(model.add_span(second, 10, 60, work), "second lane");
    model.finish();
    require(model.children(flame_graph::root_node).size() == 2U, "one node per thread under the root");
    const std::optional<CallNodeIndex> named = child_named(model, flame_graph::root_node, "main thread");
    const std::optional<CallNodeIndex> unnamed = child_named(model, flame_graph::root_node, "thread 2");
    require(named.has_value() && unnamed.has_value(), "thread nodes take the thread's name or its tid");
    require(model.node(*named).synthetic && model.node(*named).total == 40, "a thread node sums its spans");
    require(model.total_weight() == 100, "the root sums the threads");
    require(model.functions().size() == 1U && function_named(model, "work").total == 100,
            "thread nodes have no rows; work sums over both");
    require(model.first_time() == 0 && model.last_time() == 70, "the extent over both lanes");
}

void search_flags() {
    TraceModel model{};
    build_basic(model);
    SearchFlags flags{};
    model.search("PAR", flags);
    require(flags.active() && flags.summary.names_matched == 1U, "one name holds 'par' (case folded)");
    require(flags.summary.nodes_matched == 1U && flags.summary.matched_weight == 70, "the merged parse node");
    model.search("a", flags);
    // main, parse and the root's "all" hold an 'a'; the root is no node match.
    require(flags.summary.names_matched == 3U, "three names hold 'a'");
    require(flags.summary.nodes_matched == 2U, "main and parse");
    require(flags.summary.matched_weight == 100, "parse lies under main: counted once");
    model.search("", flags);
    require(!flags.active() && flags.summary.nodes_matched == 0U, "an empty query clears the search");
}

void string_table() {
    StringTable table{};
    const std::optional<NameId> first = table.intern("alpha");
    const std::optional<NameId> again = table.intern("alpha");
    require(first.has_value() && again.has_value() && *first == *again, "one identity per text");
    require(!table.find("beta").has_value(), "an unknown text is not found");
    std::string name{};
    for (std::size_t index = 0U; index < 3000U; ++index) {
        name.assign("name_");
        name.append(std::to_string(index));
        require(table.intern(name).has_value(), "names past the first table size");
    }
    require(table.size() == 3001U && table.statistics().growths > 0U, "the table grew");
    require(table.text(*table.find("name_2999")) == "name_2999", "text after growth");
    // A long name is cut at a UTF-8 boundary: 4095 'x' and a two-byte é.
    std::string long_name(StringTable::maximum_name_bytes - 1U, 'x');
    long_name.append("\xc3\xa9tail");
    const std::optional<NameId> cut = table.intern(long_name);
    require(cut.has_value() && table.text(*cut).size() == StringTable::maximum_name_bytes - 1U,
            "the cut does not split the two-byte scalar");
    table.clear();
    require(table.size() == 0U && !table.find("alpha").has_value(), "clear forgets");
}

void formats_and_labels() {
    flame_graph::FormattedNumber number{};
    flame_graph::format_weight(1'500'000, flame_graph::TraceUnit::nanoseconds, number);
    require(number.text() == "1.5 ms", "milliseconds");
    flame_graph::format_weight(830, flame_graph::TraceUnit::nanoseconds, number);
    require(number.text() == "830 ns", "nanoseconds");
    flame_graph::format_weight(12, flame_graph::TraceUnit::samples, number);
    require(number.text() == "12 samples", "samples");
    flame_graph::format_share(1, 8, number);
    require(number.text() == "12.5%", "a share");
    flame_graph::format_share(1, 0, number);
    require(number.text() == "0%", "no whole");
    std::string label{};
    require(flame_graph::cut_label("abcdefghij", 100.0, 50.0, 10.0, label), "a cut fits four scalars");
    require(label == std::string("abcd") + std::string(flame_graph::label_ellipsis), "four scalars and an ellipsis");
    require(!flame_graph::cut_label("abcdefghij", 100.0, 25.0, 10.0, label), "fewer than two scalars: nothing");
    require(flame_graph::cut_label("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", 40.0, 30.0, 0.0, label) &&
                label == std::string("\xc3\xa9\xc3\xa9\xc3\xa9") + std::string(flame_graph::label_ellipsis),
            "cuts count scalars, not bytes");
}

} // namespace

int main() {
    try {
        tree_and_layout();
        name_order_and_offsets();
        recursion_counted_once();
        clipping_and_refusals();
        thread_nodes();
        search_flags();
        string_table();
        formats_and_labels();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "model_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("model_tests: passed");
    return 0;
}
