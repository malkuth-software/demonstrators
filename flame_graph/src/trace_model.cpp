#include "trace_model.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace flame_graph {

namespace {

constexpr std::size_t initial_child_slots = 4096U;
constexpr std::uint32_t no_row = 0U;

std::uint64_t child_key_hash(CallNodeIndex parent, NameId name) noexcept {
    // A 64-bit mix (splitmix64's finaliser) of the packed pair.
    std::uint64_t key = (static_cast<std::uint64_t>(parent.value) << 32U) | name.value;
    key ^= key >> 30U;
    key *= 0xbf58476d1ce4e5b9ULL;
    key ^= key >> 27U;
    key *= 0x94d049bb133111ebULL;
    key ^= key >> 31U;
    return key;
}

char fold_ascii(char byte) noexcept {
    if (byte >= 'A' && byte <= 'Z') return static_cast<char>(byte - 'A' + 'a');
    return byte;
}

// Whether `folded_query` (already folded) occurs in `text`, folding as it
// compares.
bool contains_folded(std::string_view text, std::string_view folded_query) noexcept {
    if (folded_query.size() > text.size()) return false;
    const std::size_t last = text.size() - folded_query.size();
    for (std::size_t at = 0U; at <= last; ++at) {
        std::size_t matched = 0U;
        while (matched < folded_query.size() && fold_ascii(text[at + matched]) == folded_query[matched]) ++matched;
        if (matched == folded_query.size()) return true;
    }
    return false;
}

} // namespace

// Start order, longer spans first on equal starts, so that an enclosing
// span comes before the spans it encloses.
struct TraceModel::SpanStartOrder final {
    bool operator()(const TraceSpan& left, const TraceSpan& right) const noexcept {
        if (left.start != right.start) return left.start < right.start;
        return left.duration > right.duration;
    }
};

// Children by name text, then by index for equal texts (none: names are
// interned, so equal text is equal identity, which one parent holds once).
struct TraceModel::ChildNameOrder final {
    const TraceModel* model{};
    bool operator()(std::uint32_t left, std::uint32_t right) const noexcept {
        const std::string_view left_text = (*model).names_.text((*model).nodes_[left].name);
        const std::string_view right_text = (*model).names_.text((*model).nodes_[right].name);
        if (left_text != right_text) return left_text < right_text;
        return left < right;
    }
};

TraceModel::TraceModel() {
    child_slots_.assign(initial_child_slots, 0U);
    threads_.reserve(64U);
    open_.reserve(maximum_depth);
}

void TraceModel::clear() noexcept {
    names_.clear();
    unit_ = TraceUnit::nanoseconds;
    threads_.clear();
    span_count_ = 0U;
    last_thread_.reset();
    nodes_.clear();
    children_.clear();
    functions_.clear();
    function_rows_.clear();
    std::fill(child_slots_.begin(), child_slots_.end(), 0U);
    first_time_ = 0;
    last_time_ = 0;
    finished_ = false;
    statistics_ = TraceStatistics{};
}

void TraceModel::reserve_spans(std::size_t spans) {
    // Spread evenly over the lanes made so far (a share each, one
    // allocation per lane); before any lane exists there is nothing to
    // reserve into.
    const std::size_t capped = std::min(spans, maximum_spans);
    if (threads_.empty()) return;
    const std::size_t share = capped / threads_.size() + 1U;
    for (ThreadLane& lane : threads_) lane.spans.reserve(share);
}

std::optional<ThreadIndex> TraceModel::thread(std::int64_t pid, std::int64_t tid) {
    // Events cluster by thread: the last lane is tried first.
    if (last_thread_.has_value()) {
        const ThreadLane& last = threads_[*last_thread_];
        if (last.pid == pid && last.tid == tid) return ThreadIndex{*last_thread_};
    }
    for (std::size_t index = 0U; index < threads_.size(); ++index) {
        if (threads_[index].pid == pid && threads_[index].tid == tid) {
            last_thread_ = static_cast<std::uint32_t>(index);
            return ThreadIndex{static_cast<std::uint32_t>(index)};
        }
    }
    if (threads_.size() >= maximum_threads) return std::nullopt;
    ThreadLane lane{};
    lane.pid = pid;
    lane.tid = tid;
    threads_.push_back(std::move(lane));
    last_thread_ = static_cast<std::uint32_t>(threads_.size() - 1U);
    return ThreadIndex{*last_thread_};
}

void TraceModel::set_thread_name(ThreadIndex thread, NameId name) {
    if (thread.value >= threads_.size()) return;
    threads_[thread.value].name = name;
}

bool TraceModel::add_span(ThreadIndex thread, std::int64_t start, std::int64_t duration, NameId name) {
    if (thread.value >= threads_.size() || duration < 0 || span_count_ >= maximum_spans) return false;
    // The end must be representable.
    if (start > std::numeric_limits<std::int64_t>::max() - duration) return false;
    TraceSpan span{};
    span.start = start;
    span.duration = duration;
    span.name = name;
    threads_[thread.value].spans.push_back(span);
    ++span_count_;
    return true;
}

// -- the call tree ------------------------------------------------------------------------

void TraceModel::grow_child_slots() {
    std::vector<std::uint32_t> grown(child_slots_.size() * 2U, 0U);
    const std::size_t mask = grown.size() - 1U;
    for (const std::uint32_t entry : child_slots_) {
        if (entry == 0U) continue;
        const CallNode& node = nodes_[entry - 1U];
        std::size_t slot = static_cast<std::size_t>(child_key_hash(node.parent, node.name)) & mask;
        while (grown[slot] != 0U) slot = (slot + 1U) & mask;
        grown[slot] = entry;
    }
    child_slots_.swap(grown);
}

CallNodeIndex TraceModel::child_of(CallNodeIndex parent, NameId name) {
    const std::size_t mask = child_slots_.size() - 1U;
    std::size_t slot = static_cast<std::size_t>(child_key_hash(parent, name)) & mask;
    while (child_slots_[slot] != 0U) {
        ++statistics_.child_probes;
        const std::uint32_t index = child_slots_[slot] - 1U;
        const CallNode& existing = nodes_[index];
        if (existing.parent == parent && existing.name == name && index != root_node.value) {
            return CallNodeIndex{index};
        }
        slot = (slot + 1U) & mask;
    }
    CallNode node{};
    node.name = name;
    node.parent = parent;
    node.depth = static_cast<std::uint16_t>(nodes_[parent.value].depth + 1U);
    nodes_.push_back(node);
    const std::uint32_t index = static_cast<std::uint32_t>(nodes_.size() - 1U);
    child_slots_[slot] = index + 1U;
    if (nodes_.size() * 2U > child_slots_.size()) grow_child_slots();
    return CallNodeIndex{index};
}

void TraceModel::build_lane(ThreadLane& lane) {
    std::sort(lane.spans.begin(), lane.spans.end(), SpanStartOrder{});
    open_.clear();
    lane.max_depth = 0U;
    lane.first_start = lane.spans.empty() ? 0 : lane.spans.front().start;
    lane.last_end = lane.first_start;
    for (TraceSpan& span : lane.spans) {
        while (!open_.empty() && span.start >= open_.back().end) open_.pop_back();
        if (!open_.empty() && span.end() > open_.back().end) {
            // Outlives its parent: clipped to it (a well-formed trace nests).
            span.duration = open_.back().end - span.start;
            ++statistics_.overlaps_clipped;
        }
        if (open_.size() >= maximum_depth) {
            // Dropped: marked by a negative duration, no node;
            // group_by_depth leaves it out.
            span.duration = -1;
            ++statistics_.spans_dropped_depth;
            continue;
        }
        const CallNodeIndex parent = open_.empty() ? lane.node : open_.back().node;
        const CallNodeIndex node = child_of(parent, span.name);
        CallNode& record = nodes_[node.value];
        record.total += span.duration;
        ++record.calls;
        span.depth = static_cast<std::uint16_t>(open_.size());
        span.node = node;
        lane.max_depth = std::max(lane.max_depth, span.depth);
        lane.last_end = std::max(lane.last_end, span.end());
        open_.push_back(OpenSpan{span.end(), node});
    }
    statistics_.max_depth = std::max(statistics_.max_depth, lane.max_depth);
}

void TraceModel::group_by_depth(ThreadLane& lane) {
    const std::size_t rows = static_cast<std::size_t>(lane.max_depth) + 1U;
    counts_.assign(rows + 1U, 0U);
    std::size_t kept = 0U;
    for (const TraceSpan& span : lane.spans) {
        if (span.duration < 0) continue;
        ++counts_[static_cast<std::size_t>(span.depth) + 1U];
        ++kept;
    }
    for (std::size_t row = 0U; row < rows; ++row) counts_[row + 1U] += counts_[row];
    lane.depth_start.assign(counts_.begin(), counts_.end());
    regrouped_.resize(kept);
    // Placed in start order, so each depth keeps its start order.
    for (const TraceSpan& span : lane.spans) {
        if (span.duration < 0) continue;
        const std::size_t at = counts_[span.depth];
        regrouped_[at] = span;
        ++counts_[span.depth];
    }
    lane.spans.swap(regrouped_);
    statistics_.spans += kept;
}

void TraceModel::build_children() {
    const std::size_t count = nodes_.size();
    counts_.assign(count + 1U, 0U);
    for (std::size_t index = 1U; index < count; ++index) ++counts_[nodes_[index].parent.value + 1U];
    for (std::size_t index = 0U; index < count; ++index) counts_[index + 1U] += counts_[index];
    children_.resize(counts_[count]);
    for (std::size_t index = 0U; index < count; ++index) {
        nodes_[index].first_child = counts_[index];
        nodes_[index].child_count = 0U;
    }
    for (std::size_t index = 1U; index < count; ++index) {
        CallNode& parent = nodes_[nodes_[index].parent.value];
        children_[parent.first_child + parent.child_count] = static_cast<std::uint32_t>(index);
        ++parent.child_count;
    }
    const ChildNameOrder order{this};
    for (const CallNode& node : nodes_) {
        const std::vector<std::uint32_t>::iterator first = children_.begin() + node.first_child;
        std::sort(first, first + node.child_count, order);
    }
}

void TraceModel::sum_totals() {
    // A child's index is above its parent's (made after it), so a pass from
    // the last index down sees every child before its parent.
    for (std::size_t index = nodes_.size(); index > 0U; --index) {
        CallNode& node = nodes_[index - 1U];
        std::int64_t children_total = 0;
        for (std::uint32_t at = 0U; at < node.child_count; ++at) children_total += nodes_[children_[node.first_child + at]].total;
        if (node.synthetic) {
            node.total = children_total;
            node.self = 0;
        } else {
            node.self = std::max<std::int64_t>(0, node.total - children_total);
        }
    }
}

void TraceModel::lay_out() {
    // Parents before children (index order): each node places its children.
    for (const CallNode& node : nodes_) {
        std::int64_t offset = node.offset;
        for (std::uint32_t at = 0U; at < node.child_count; ++at) {
            CallNode& child = nodes_[children_[node.first_child + at]];
            child.offset = offset;
            offset += child.total;
        }
    }
}

void TraceModel::build_functions() {
    const std::size_t name_count = names_.size();
    function_rows_.assign(name_count, no_row);
    functions_.clear();
    on_stack_.assign(name_count, 0U);
    // Depth-first with an explicit stack of (node, next child position)
    // pairs flattened into walk_: enter adds, exit releases the name.
    walk_.clear();
    walk_.push_back(root_node.value);
    walk_.push_back(0U);
    while (!walk_.empty()) {
        const std::uint32_t position = walk_.back();
        const std::uint32_t index = walk_[walk_.size() - 2U];
        const CallNode& node = nodes_[index];
        if (position == 0U && !node.synthetic) {
            // Entering a function node.
            std::uint32_t& row = function_rows_[node.name.value];
            if (row == no_row) {
                functions_.push_back(FunctionRow{node.name, 0, 0, 0U});
                row = static_cast<std::uint32_t>(functions_.size());
            }
            FunctionRow& function = functions_[row - 1U];
            function.self += node.self;
            function.calls += node.calls;
            if (on_stack_[node.name.value] == 0U) function.total += node.total;
            ++on_stack_[node.name.value];
        }
        if (position < node.child_count) {
            walk_.back() = position + 1U;
            walk_.push_back(children_[node.first_child + position]);
            walk_.push_back(0U);
            continue;
        }
        if (!node.synthetic) --on_stack_[node.name.value];
        walk_.pop_back();
        walk_.pop_back();
    }
    statistics_.functions = functions_.size();
}

void TraceModel::finish() {
    nodes_.clear();
    std::fill(child_slots_.begin(), child_slots_.end(), 0U);
    statistics_ = TraceStatistics{};
    first_time_ = 0;
    last_time_ = 0;
    CallNode root{};
    root.name = names_.intern("all").value_or(NameId{});
    root.synthetic = true;
    nodes_.push_back(root);
    const bool thread_nodes = threads_.size() > 1U;
    std::string label{};
    bool any = false;
    for (ThreadLane& lane : threads_) {
        if (lane.name.has_value()) {
            lane.label = *lane.name;
        } else {
            label.assign("thread ");
            label.append(std::to_string(lane.tid));
            lane.label = names_.intern(label).value_or(NameId{});
        }
        lane.node = root_node;
        if (thread_nodes) {
            lane.node = child_of(root_node, lane.label);
            nodes_[lane.node.value].synthetic = true;
        }
        build_lane(lane);
        group_by_depth(lane);
        if (lane.spans.empty()) continue;
        first_time_ = any ? std::min(first_time_, lane.first_start) : lane.first_start;
        last_time_ = any ? std::max(last_time_, lane.last_end) : lane.last_end;
        any = true;
    }
    build_children();
    sum_totals();
    lay_out();
    build_functions();
    statistics_.nodes = nodes_.size();
    finished_ = true;
}

// -- reading ------------------------------------------------------------------------------

std::span<const std::uint32_t> TraceModel::children(CallNodeIndex index) const& noexcept {
    if (index.value >= nodes_.size()) return std::span<const std::uint32_t>();
    const CallNode& node = nodes_[index.value];
    return std::span<const std::uint32_t>(children_.data() + node.first_child, node.child_count);
}

std::optional<std::size_t> TraceModel::function_of(NameId name) const noexcept {
    if (name.value >= function_rows_.size() || function_rows_[name.value] == no_row) return std::nullopt;
    return static_cast<std::size_t>(function_rows_[name.value] - 1U);
}

void TraceModel::search(std::string_view query, SearchFlags& out) const {
    const std::string_view kept = query.substr(0U, std::min(query.size(), maximum_query_bytes));
    out.query.assign(kept);
    for (char& byte : out.query) byte = fold_ascii(byte);
    out.summary = SearchSummary{};
    out.names.assign(names_.size(), 0U);
    out.covered.assign(nodes_.size(), 0U);
    if (out.query.empty()) return;
    for (std::uint32_t name = 0U; name < names_.size(); ++name) {
        if (!contains_folded(names_.text(NameId{name}), out.query)) continue;
        out.names[name] = 1U;
        ++out.summary.names_matched;
    }
    // Parents before children (index order): a node is covered when it or
    // an ancestor matched; the weight counts each covered subtree once, at
    // its top.
    for (std::size_t index = 0U; index < nodes_.size(); ++index) {
        const CallNode& node = nodes_[index];
        const bool matched = !node.synthetic && out.names[node.name.value] != 0U;
        const bool parent_covered = index != 0U && out.covered[node.parent.value] != 0U;
        if (matched) ++out.summary.nodes_matched;
        if (matched && !parent_covered) out.summary.matched_weight += node.total;
        out.covered[index] = (matched || parent_covered) ? 1U : 0U;
    }
}

} // namespace flame_graph
