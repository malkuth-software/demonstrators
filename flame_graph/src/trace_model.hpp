#pragma once

// The loaded trace (the flame graph demonstrator's model): spans per
// thread, the merged call tree a flame graph draws, its layout, the
// per-function table, and search. The loaders fill it (trace_loader.hpp),
// then finish() derives everything else once; the views only read it.
//
// Units: span times are integers in the trace's unit, nanoseconds for a
// Chrome trace or sample positions for a folded-stack file (TraceUnit);
// call-tree weights are the same unit (a node's total is the sum of its
// spans' durations).
//
// finish(), in order:
// 1. each lane's spans sorted by start (longer first on ties) and walked
//    with a stack of open ends: depth, and the call-tree node each span
//    merges into (a child of its enclosing span's node with the same
//    name). A span that outlives its parent is clipped to it and counted
//    (overlaps); one deeper than maximum_depth is dropped and counted;
// 2. each lane's spans regrouped by depth (start order kept), with the
//    row starts in `depth_start`, so a view finds a row's visible spans by
//    binary search (spans of one depth in one lane never overlap);
// 3. children in compressed rows, each node's sorted by name (the flame
//    graph convention: alphabetical, so equal stacks line up);
// 4. totals of the root and thread nodes as sums of their children; self
//    = total - children's totals for the others;
// 5. layout: each node's offset from the root's left edge (its earlier
//    siblings' totals after its parent's offset);
// 6. function rows: per name, self and calls summed, total summed once per
//    stack (a recursive function's nested calls are not counted twice).
// When the trace has more than one thread, each thread is a node under
// the root (named after the thread); with one, its spans sit under the
// root directly.
//
// Ownership: the model owns everything it holds. Spans, nodes and rows are
// borrowed by the views until the next clear or finish. One thread: the
// window's (the loaders run there too).

#include "string_table.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <vector>

namespace flame_graph {

enum class TraceUnit : std::uint8_t { nanoseconds, samples };

struct ThreadIndex final {
    std::uint32_t value{};
    friend bool operator==(ThreadIndex left, ThreadIndex right) noexcept { return left.value == right.value; }
};

struct CallNodeIndex final {
    std::uint32_t value{};
    friend bool operator==(CallNodeIndex left, CallNodeIndex right) noexcept { return left.value == right.value; }
    friend bool operator!=(CallNodeIndex left, CallNodeIndex right) noexcept { return left.value != right.value; }
};

inline constexpr CallNodeIndex root_node{0U};

struct TraceSpan final {
    std::int64_t start{};
    std::int64_t duration{};
    NameId name{};
    std::uint16_t depth{};
    // The call-tree node it merged into (after finish).
    CallNodeIndex node{};
    [[nodiscard]] std::int64_t end() const noexcept { return start + duration; }
};

struct ThreadLane final {
    std::int64_t pid{};
    std::int64_t tid{};
    // The trace's own name for the thread (Chrome "thread_name" metadata).
    std::optional<NameId> name{};
    // What the views show: the name, or "thread <tid>" (set by finish).
    NameId label{};
    // After finish: grouped by depth, start order within a depth.
    std::vector<TraceSpan> spans{};
    // Spans of depth d are spans[depth_start[d] .. depth_start[d + 1]);
    // max_depth + 2 entries after finish (an empty lane has {0, 0}).
    std::vector<std::uint32_t> depth_start{};
    std::uint16_t max_depth{};
    std::int64_t first_start{};
    std::int64_t last_end{};
    // The lane's node in the call tree: its thread node, or the root.
    CallNodeIndex node{};
};

struct CallNode final {
    NameId name{};
    CallNodeIndex parent{};
    // Children are children()[first_child .. first_child + child_count).
    std::uint32_t first_child{};
    std::uint32_t child_count{};
    std::int64_t total{};
    std::int64_t self{};
    // From the root's left edge, in weight units.
    std::int64_t offset{};
    std::uint64_t calls{};
    std::uint16_t depth{};
    // The root, or a thread node: not a function (no table row).
    bool synthetic{};
};

struct FunctionRow final {
    NameId name{};
    // Once per stack: nested recursive calls are not added again.
    std::int64_t total{};
    std::int64_t self{};
    std::uint64_t calls{};
};

struct TraceStatistics final {
    std::size_t spans{};
    std::size_t spans_dropped_depth{};
    std::size_t overlaps_clipped{};
    std::size_t nodes{};
    std::size_t child_probes{};
    std::size_t functions{};
    std::uint16_t max_depth{};
};

// What a search found: names containing the query (ASCII case folded),
// nodes with those names, and the weight under them counted once (a match
// inside a matched subtree adds nothing).
struct SearchSummary final {
    std::size_t names_matched{};
    std::size_t nodes_matched{};
    std::int64_t matched_weight{};
};

// A search's output storage, reused between searches: one flag per name,
// and one per node (whether the node or an ancestor matched).
struct SearchFlags final {
    std::vector<std::uint8_t> names{};
    std::vector<std::uint8_t> covered{};
    std::string query{};
    SearchSummary summary{};
    [[nodiscard]] bool active() const noexcept { return !query.empty(); }
    [[nodiscard]] bool matches(NameId name) const noexcept {
        return name.value < names.size() && names[name.value] != 0U;
    }
};

class TraceModel final {
public:
    static constexpr std::size_t maximum_depth = 1024U;
    static constexpr std::size_t maximum_threads = 4096U;
    static constexpr std::size_t maximum_spans = 64U * 1024U * 1024U;
    // The longest search query kept.
    static constexpr std::size_t maximum_query_bytes = 256U;

    TraceModel();
    TraceModel(const TraceModel&) = delete;
    TraceModel& operator=(const TraceModel&) = delete;

    // -- building ---------------------------------------------------------------------
    // Forgets the trace, keeping the storage.
    void clear() noexcept;
    void reserve_spans(std::size_t spans);
    void set_unit(TraceUnit unit) noexcept { unit_ = unit; }
    [[nodiscard]] StringTable& names() & noexcept { return names_; }
    // The lane of (pid, tid), added when new; none past maximum_threads.
    [[nodiscard]] std::optional<ThreadIndex> thread(std::int64_t pid, std::int64_t tid);
    void set_thread_name(ThreadIndex thread, NameId name);
    // False (nothing added) past maximum_spans, for a stale thread or a
    // negative duration.
    bool add_span(ThreadIndex thread, std::int64_t start, std::int64_t duration, NameId name);
    // Derives depths, the call tree, its layout and the function rows (see
    // the file comment). Call once after loading; again only after clear.
    void finish();

    // -- reading ----------------------------------------------------------------------
    [[nodiscard]] TraceUnit unit() const noexcept { return unit_; }
    [[nodiscard]] const StringTable& names() const& noexcept { return names_; }
    const StringTable& names() const&& = delete;
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] std::span<const ThreadLane> threads() const& noexcept {
        return std::span<const ThreadLane>(threads_.data(), threads_.size());
    }
    std::span<const ThreadLane> threads() const&& = delete;
    [[nodiscard]] std::span<const CallNode> nodes() const& noexcept {
        return std::span<const CallNode>(nodes_.data(), nodes_.size());
    }
    std::span<const CallNode> nodes() const&& = delete;
    [[nodiscard]] const CallNode& node(CallNodeIndex index) const& noexcept { return nodes_[index.value]; }
    const CallNode& node(CallNodeIndex index) const&& = delete;
    // The node's children in name order (indices into nodes()).
    [[nodiscard]] std::span<const std::uint32_t> children(CallNodeIndex index) const& noexcept;
    std::span<const std::uint32_t> children(CallNodeIndex index) const&& = delete;
    [[nodiscard]] std::span<const FunctionRow> functions() const& noexcept {
        return std::span<const FunctionRow>(functions_.data(), functions_.size());
    }
    std::span<const FunctionRow> functions() const&& = delete;
    // The function row of `name`, or none.
    [[nodiscard]] std::optional<std::size_t> function_of(NameId name) const noexcept;
    [[nodiscard]] std::int64_t total_weight() const noexcept { return nodes_.empty() ? 0 : nodes_[0].total; }
    // The earliest start and latest end over every lane (0, 0 when empty).
    [[nodiscard]] std::int64_t first_time() const noexcept { return first_time_; }
    [[nodiscard]] std::int64_t last_time() const noexcept { return last_time_; }
    [[nodiscard]] const TraceStatistics& statistics() const noexcept { return statistics_; }

    // Flags the names containing `query` (ASCII letters case folded; an
    // empty query matches nothing and clears the search) and the nodes
    // under them; fills out.summary. The query is cut at
    // maximum_query_bytes.
    void search(std::string_view query, SearchFlags& out) const;

private:
    struct OpenSpan final {
        std::int64_t end{};
        CallNodeIndex node{};
    };
    struct SpanStartOrder;
    struct ChildNameOrder;

    // The child of `parent` named `name`, added when new.
    CallNodeIndex child_of(CallNodeIndex parent, NameId name);
    void grow_child_slots();
    void build_lane(ThreadLane& lane);
    void group_by_depth(ThreadLane& lane);
    void build_children();
    void sum_totals();
    void lay_out();
    void build_functions();

    StringTable names_{};
    TraceUnit unit_{TraceUnit::nanoseconds};
    std::vector<ThreadLane> threads_{};
    std::size_t span_count_{};
    std::optional<std::uint32_t> last_thread_{};
    std::vector<CallNode> nodes_{};
    std::vector<std::uint32_t> children_{};
    std::vector<FunctionRow> functions_{};
    // Per name: its function row plus one, 0 for none.
    std::vector<std::uint32_t> function_rows_{};
    // (parent, name) -> node index plus one, 0 empty; at most half full.
    std::vector<std::uint32_t> child_slots_{};
    std::int64_t first_time_{};
    std::int64_t last_time_{};
    bool finished_{};
    TraceStatistics statistics_{};

    // Work storage for finish(), reused.
    std::vector<OpenSpan> open_{};
    std::vector<TraceSpan> regrouped_{};
    std::vector<std::uint32_t> counts_{};
    std::vector<std::uint32_t> on_stack_{};
    std::vector<std::uint32_t> walk_{};
};

} // namespace flame_graph
