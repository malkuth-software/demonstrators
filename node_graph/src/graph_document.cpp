#include "graph_document.hpp"

#include <charconv>
#include <fstream>
#include <limits>
#include <system_error>

namespace node_graph {

namespace gf = gui_forms;

namespace {

constexpr std::size_t reported_unknown_keys = 8U;

bool live_node(const gf::NodeGraph& graph, gf::NodeId node) noexcept {
    const bool live = graph.node(node) != nullptr;
    return live;
}

} // namespace

GraphDocument::GraphDocument() {
    key_.reserve(24U);
    spec_.ports.reserve(maximum_kind_ports);
    spec_.properties.reserve(maximum_kind_properties);
}

// -- keys and bookkeeping -----------------------------------------------------------

bool GraphDocument::next_key(std::string& out) {
    // Each live key can collide once, so the loop ends within node_count + 1
    // steps unless the counter is spent.
    while (next_key_number_ < std::numeric_limits<std::uint64_t>::max()) {
        char digits[24]{};
        const std::to_chars_result written = std::to_chars(digits, digits + sizeof(digits), next_key_number_);
        ++next_key_number_;
        out.assign("n");
        out.append(digits, static_cast<std::size_t>(written.ptr - digits));
        const gf::NodeId existing = graph_.find_node(out);
        if (!live_node(graph_, existing)) return true;
    }
    out.clear();
    return false;
}

void GraphDocument::edited() {
    if (autosave_interval_ == 0U || path_.empty()) return;
    ++edits_since_autosave_;
    if (edits_since_autosave_ < autosave_interval_) return;
    edits_since_autosave_ = 0U;
    std::filesystem::path sidecar = path_;
    sidecar += ".autosave";
    save_text(save_buffer_);
    std::ofstream stream(sidecar, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) return;
    stream.write(save_buffer_.data(), static_cast<std::streamsize>(save_buffer_.size()));
    if (stream.good()) ++counters_.autosaves;
}

std::optional<KindIndex> GraphDocument::kind_of(gf::NodeId node) const noexcept {
    const gf::NodeRecord* record = graph_.node(node);
    if (record == nullptr) return std::nullopt;
    const std::optional<KindIndex> kind = find_kind((*record).kind);
    return kind;
}

// -- edits --------------------------------------------------------------------------------

gf::GraphResult GraphDocument::add_node(KindIndex kind, gf::WorldPoint top_left) {
    if (!next_key(key_)) return gf::GraphResult{gf::GraphStatus::capacity, {}, {}};
    fill_node_spec(kind_at(kind), key_, top_left, spec_);
    const gf::GraphResult result = graph_.add_node(spec_);
    if (result.status == gf::GraphStatus::ok) {
        ++counters_.nodes_added;
        edited();
    }
    return result;
}

std::size_t GraphDocument::remove_nodes(std::span<const gf::NodeId> nodes) {
    std::size_t removed = 0U;
    graph_.begin_undo_group();
    for (const gf::NodeId node : nodes) {
        if (!live_node(graph_, node)) continue;
        const gf::GraphResult result = graph_.remove_node(node);
        if (result.status == gf::GraphStatus::ok) ++removed;
    }
    static_cast<void>(graph_.end_undo_group());
    counters_.nodes_removed += removed;
    if (removed > 0U) edited();
    return removed;
}

gf::GraphResult GraphDocument::move_nodes(std::span<const gf::NodeId> nodes, gf::WorldPoint delta) {
    const gf::GraphResult result = graph_.move_nodes(nodes, delta);
    if (result.status == gf::GraphStatus::ok) {
        ++counters_.moves;
        // A drag's frames join one record; count the edit once per gesture
        // step rather than per frame for autosave.
        if (gesture_depth_ == 0U) edited();
    }
    return result;
}

bool GraphDocument::set_lamp(gf::PortRef port, gf::PortLamp lamp) {
    const gf::PortRecord* record = graph_.port(port);
    if (record == nullptr || (*record).lamp == lamp) return false;
    const gf::GraphResult result = graph_.set_port_lamp(port, lamp);
    if (result.status != gf::GraphStatus::ok) return false;
    ++counters_.lamp_writes;
    return true;
}

ConnectStatus GraphDocument::check_connection(gf::PortRef from, gf::PortRef to) {
    const gf::PortRecord* from_port = graph_.port(from);
    const gf::PortRecord* to_port = graph_.port(to);
    if (from_port == nullptr || to_port == nullptr) return ConnectStatus::refused_by_graph;
    if ((*from_port).direction != gf::PortDirection::output || (*to_port).direction != gf::PortDirection::input) {
        return ConnectStatus::refused_by_graph;
    }
    if (!connectable((*from_port).socket_type, (*to_port).socket_type)) return ConnectStatus::refused_type;
    if (from.node == to.node) return ConnectStatus::refused_by_graph;
    // A link from -> to closes a cycle when `from` is reachable from `to`.
    if (reaches(to.node.index, from.node.index)) return ConnectStatus::refused_cycle;
    return ConnectStatus::connected;
}

ConnectResult GraphDocument::connect(gf::PortRef from, gf::PortRef to) {
    ConnectResult result{};
    const ConnectStatus checked = check_connection(from, to);
    if (checked == ConnectStatus::refused_type) {
        ++counters_.connects_refused_type;
        result.status = checked;
        return result;
    }
    if (checked == ConnectStatus::refused_cycle) {
        ++counters_.connects_refused_cycle;
        result.status = checked;
        return result;
    }
    const gf::GraphResult made = graph_.connect(from, to);
    result.graph = made.status;
    result.link = made.link;
    if (made.status != gf::GraphStatus::ok) {
        ++counters_.connects_refused_graph;
        result.status = ConnectStatus::refused_by_graph;
        return result;
    }
    ++counters_.connects;
    edited();
    result.status = ConnectStatus::connected;
    return result;
}

gf::GraphResult GraphDocument::disconnect(gf::LinkId link) {
    const gf::GraphResult result = graph_.disconnect(link);
    if (result.status == gf::GraphStatus::ok) {
        ++counters_.disconnects;
        edited();
    }
    return result;
}

gf::GraphResult GraphDocument::set_property(gf::NodeId node, std::string_view name, std::string_view value) {
    const gf::GraphResult result = graph_.set_property(node, name, value);
    if (result.status == gf::GraphStatus::ok) {
        ++counters_.property_edits;
        edited();
    }
    return result;
}

gf::GraphResult GraphDocument::set_title(gf::NodeId node, std::string_view title) {
    const gf::GraphResult result = graph_.set_title(node, title);
    if (result.status == gf::GraphStatus::ok) {
        ++counters_.title_edits;
        edited();
    }
    return result;
}

gf::GraphResult GraphDocument::undo() {
    const gf::GraphResult result = graph_.undo();
    if (result.status == gf::GraphStatus::ok) {
        ++counters_.undos;
        edited();
    }
    return result;
}

gf::GraphResult GraphDocument::redo() {
    const gf::GraphResult result = graph_.redo();
    if (result.status == gf::GraphStatus::ok) {
        ++counters_.redos;
        edited();
    }
    return result;
}

void GraphDocument::begin_gesture() noexcept {
    if (gesture_depth_ == 0U) {
        gesture_steps_before_ = graph_.undo_steps();
        gesture_bytes_before_ = graph_.undo_bytes();
    }
    ++gesture_depth_;
    graph_.begin_undo_group();
}

gf::GraphResult GraphDocument::end_gesture() noexcept {
    const gf::GraphResult result = graph_.end_undo_group();
    if (result.status == gf::GraphStatus::ok && gesture_depth_ > 0U) {
        --gesture_depth_;
        if (gesture_depth_ == 0U) {
            ++counters_.gestures;
            edited();
        }
    }
    return result;
}

void GraphDocument::cancel_gesture() {
    const bool outermost = gesture_depth_ == 1U;
    const gf::GraphResult ended = end_gesture();
    if (ended.status != gf::GraphStatus::ok || !outermost) return;
    // The step count stays put when the bound dropped the oldest step, so
    // the log's byte estimate is compared too.
    const bool recorded = graph_.undo_steps() != gesture_steps_before_ || graph_.undo_bytes() != gesture_bytes_before_;
    if (recorded) static_cast<void>(graph_.undo());
}

// -- topology ------------------------------------------------------------------------------

const Topology& GraphDocument::topology() {
    if (topology_.revision != graph_.revision()) rebuild_topology();
    return topology_;
}

void GraphDocument::rebuild_topology() {
    const std::span<const gf::NodeRecord> nodes = graph_.node_slots();
    const std::span<const gf::LinkRecord> links = graph_.link_slots();
    const std::size_t slots = nodes.size();
    topology_.offsets.assign(slots + 1U, 0U);
    // Count, then turn counts into row starts, then place.
    for (const gf::LinkRecord& link : links) {
        if (!link.live) continue;
        ++topology_.offsets[static_cast<std::size_t>(link.from.node.index) + 1U];
    }
    for (std::size_t slot = 0U; slot < slots; ++slot) topology_.offsets[slot + 1U] += topology_.offsets[slot];
    topology_.targets.resize(topology_.offsets[slots]);
    walk_stack_.assign(slots, 0U);
    for (const gf::LinkRecord& link : links) {
        if (!link.live) continue;
        const std::size_t from = link.from.node.index;
        const std::size_t at = static_cast<std::size_t>(topology_.offsets[from]) + walk_stack_[from];
        topology_.targets[at] = link.to.node.index;
        ++walk_stack_[from];
    }
    walk_stack_.clear();
    topology_.revision = graph_.revision();
    ++counters_.topology_builds;
}

bool GraphDocument::reaches(std::uint32_t start, std::uint32_t target) {
    const Topology& rows = topology();
    const std::size_t slots = rows.offsets.empty() ? 0U : rows.offsets.size() - 1U;
    if (start >= slots || target >= slots) return false;
    if (walk_marks_.size() < slots) walk_marks_.resize(slots, 0U);
    // A fresh stamp marks this walk's visits; at wrap-around every mark is
    // cleared so that an old stamp cannot read as visited.
    if (walk_stamp_ == std::numeric_limits<std::uint32_t>::max()) {
        walk_marks_.assign(walk_marks_.size(), 0U);
        walk_stamp_ = 0U;
    }
    ++walk_stamp_;
    ++counters_.cycle_checks;
    // Explicit stack, never recursion (graphs are runtime-shaped): each slot
    // is pushed at most once, so the stack holds at most `slots` entries.
    walk_stack_.clear();
    walk_stack_.push_back(start);
    walk_marks_[start] = walk_stamp_;
    while (!walk_stack_.empty()) {
        const std::uint32_t slot = walk_stack_.back();
        walk_stack_.pop_back();
        ++counters_.cycle_check_visits;
        if (slot == target) return true;
        const std::uint32_t first = rows.offsets[slot];
        const std::uint32_t last = rows.offsets[static_cast<std::size_t>(slot) + 1U];
        for (std::uint32_t at = first; at < last; ++at) {
            const std::uint32_t next = rows.targets[at];
            if (walk_marks_[next] == walk_stamp_) continue;
            walk_marks_[next] = walk_stamp_;
            walk_stack_.push_back(next);
        }
    }
    return false;
}

// -- documents ------------------------------------------------------------------------------

void GraphDocument::save_text(std::string& out) {
    const gf::GraphSaveResult measured = graph_.save(std::span<char>());
    out.resize(measured.required);
    const gf::GraphSaveResult saved = graph_.save(std::span<char>(out.data(), out.size()));
    out.resize(saved.written);
    ++counters_.saves;
}

gf::GraphLoadResult GraphDocument::load_text(std::string_view text, LoadReport& report) {
    report.unknown_kinds = 0U;
    report.unknown_properties = 0U;
    report.unknown_kind_keys.clear();
    const gf::GraphLoadResult loaded = graph_.load(text);
    if (loaded.error != gf::GraphLoadError::ok) return loaded;
    ++counters_.loads;
    gesture_depth_ = 0U;
    edits_since_autosave_ = 0U;
    for (const gf::NodeRecord& node : graph_.node_slots()) {
        if (!node.live) continue;
        const std::optional<KindIndex> kind = find_kind(node.kind);
        if (!kind.has_value()) {
            ++report.unknown_kinds;
            if (report.unknown_kind_keys.size() < reported_unknown_keys) report.unknown_kind_keys.push_back(node.key);
            continue;
        }
        const KindDescriptor& descriptor = kind_at(*kind);
        for (const gf::NodeProperty& property : node.properties) {
            if (!kind_default(descriptor, property.name).has_value()) ++report.unknown_properties;
        }
    }
    return loaded;
}

FileResult GraphDocument::load_file(const std::filesystem::path& path, LoadReport& report) {
    FileResult result{};
    std::error_code error{};
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        result.status = FileStatus::cannot_open;
        return result;
    }
    if (size > maximum_document_bytes) {
        result.status = FileStatus::too_large;
        return result;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        result.status = FileStatus::cannot_open;
        return result;
    }
    std::string text{};
    text.resize(static_cast<std::size_t>(size));
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (static_cast<std::uintmax_t>(stream.gcount()) != size) {
        result.status = FileStatus::read_failed;
        return result;
    }
    result.load = load_text(text, report);
    if (result.load.error != gf::GraphLoadError::ok) {
        result.status = FileStatus::refused_document;
        return result;
    }
    path_ = path;
    return result;
}

FileResult GraphDocument::save_file(const std::filesystem::path& path) {
    FileResult result{};
    save_text(save_buffer_);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        result.status = FileStatus::cannot_open;
        return result;
    }
    stream.write(save_buffer_.data(), static_cast<std::streamsize>(save_buffer_.size()));
    stream.flush();
    if (!stream.good()) {
        result.status = FileStatus::write_failed;
        return result;
    }
    path_ = path;
    return result;
}

} // namespace node_graph
