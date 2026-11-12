#pragma once

// The app's document: one GUI.Forms NodeGraph and what the app adds around
// it (NODE_GRAPH_SPEC section 5): fresh node keys, the socket-type and
// cycle checks a connection needs before NodeGraph::connect, load and save
// to files with a size limit, autosave to a sidecar every N edits, and the
// counters the workloads record.
//
// Undo and redo are NodeGraph's own log (owner decision 3 option (a), now in
// GUI.Forms): this class only groups a gesture's operations into one step
// with begin_gesture/end_gesture. The `.graph` text is NodeGraph's
// `gui-forms-graph 1` format, read with its tolerant load; the app neither
// parses nor writes the format itself.
//
// Ownership: the document owns its graph. Canvases, the evaluator and the
// workloads borrow it and must not outlive it. One thread: the window's.

#include "kinds.hpp"

#include <gui_forms/controls/node_graph.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace node_graph {

// The largest .graph file the app reads (checked against the file's size
// before anything is allocated for it).
inline constexpr std::uintmax_t maximum_document_bytes = 256U * 1024U * 1024U;

enum class ConnectStatus : std::uint8_t {
    connected,
    // The output's type cannot feed the input's (Conversion::refused).
    refused_type,
    // The link would close a cycle (the kind table declares the graph
    // acyclic; spec 4.1).
    refused_cycle,
    // NodeGraph refused it; `graph` says why.
    refused_by_graph,
};

struct ConnectResult final {
    ConnectStatus status{ConnectStatus::connected};
    gui_forms::GraphStatus graph{gui_forms::GraphStatus::ok};
    gui_forms::LinkId link{};
};

enum class FileStatus : std::uint8_t { ok, cannot_open, too_large, read_failed, write_failed, refused_document };

// A file operation's result: the status and, for refused_document, where
// load stopped.
struct FileResult final {
    FileStatus status{FileStatus::ok};
    gui_forms::GraphLoadResult load{};
};

// What a load found besides errors: nodes whose kind the table lacks (kept
// verbatim, their outputs evaluate to `error`) and nodes with properties
// their kind does not declare (kept and written back).
struct LoadReport final {
    std::size_t unknown_kinds{};
    std::size_t unknown_properties{};
    // The first few keys of nodes with unknown kinds, for the status line.
    std::vector<std::string> unknown_kind_keys{};
};

// Counters (they only grow).
struct DocumentCounters final {
    std::uint64_t nodes_added{};
    std::uint64_t nodes_removed{};
    std::uint64_t moves{};
    std::uint64_t connects{};
    std::uint64_t connects_refused_type{};
    std::uint64_t connects_refused_cycle{};
    std::uint64_t connects_refused_graph{};
    std::uint64_t disconnects{};
    std::uint64_t property_edits{};
    std::uint64_t title_edits{};
    std::uint64_t undos{};
    std::uint64_t redos{};
    std::uint64_t gestures{};
    std::uint64_t loads{};
    std::uint64_t saves{};
    std::uint64_t autosaves{};
    std::uint64_t cycle_checks{};
    std::uint64_t cycle_check_visits{};
    std::uint64_t topology_builds{};
    std::uint64_t lamp_writes{};
};

// The outgoing links of every node slot, in compressed rows: the targets of
// slot s are targets[offsets[s] .. offsets[s + 1]) (node slot indices).
// Rebuilt when the graph's revision moves on; its storage is reused.
struct Topology final {
    std::vector<std::uint32_t> offsets{};
    std::vector<std::uint32_t> targets{};
    std::uint64_t revision{};
};

class GraphDocument final {
public:
    GraphDocument();
    GraphDocument(const GraphDocument&) = delete;
    GraphDocument& operator=(const GraphDocument&) = delete;

    [[nodiscard]] const gui_forms::NodeGraph& graph() const noexcept { return graph_; }
    // Mutable access for the stress generator and tests only; the app's own
    // edits go through the operations below so that they are counted.
    [[nodiscard]] gui_forms::NodeGraph& graph_for_generation() noexcept { return graph_; }

    // -- edits (each one undo step unless inside a gesture) -----------------------
    [[nodiscard]] gui_forms::GraphResult add_node(KindIndex kind, gui_forms::WorldPoint top_left);
    // Removes each live node of `nodes` (stale ones are skipped) as one step.
    std::size_t remove_nodes(std::span<const gui_forms::NodeId> nodes);
    [[nodiscard]] gui_forms::GraphResult move_nodes(std::span<const gui_forms::NodeId> nodes, gui_forms::WorldPoint delta);
    [[nodiscard]] ConnectResult connect(gui_forms::PortRef from, gui_forms::PortRef to);
    [[nodiscard]] gui_forms::GraphResult disconnect(gui_forms::LinkId link);
    [[nodiscard]] gui_forms::GraphResult set_property(gui_forms::NodeId node, std::string_view name, std::string_view value);
    [[nodiscard]] gui_forms::GraphResult set_title(gui_forms::NodeId node, std::string_view title);
    [[nodiscard]] gui_forms::GraphResult undo();
    [[nodiscard]] gui_forms::GraphResult redo();
    // One gesture, one undo step (spec 5): every edit between the calls.
    // Gestures nest as NodeGraph's groups do.
    void begin_gesture() noexcept;
    [[nodiscard]] gui_forms::GraphResult end_gesture() noexcept;
    // Ends the open gesture and takes its step back, for a gesture the user
    // cancelled (Escape during a drag). NodeGraph has no rollback for an open
    // group (GAPS.md G5), so the reverted step stays as a redo step.
    void cancel_gesture();

    // Writes a port's lamp (evaluation state: not recorded for undo, not
    // saved) when it differs from the record's; true when it wrote.
    bool set_lamp(gui_forms::PortRef port, gui_forms::PortLamp lamp);

    // Whether connect would accept the pair (types and cycle), without
    // changing anything.
    [[nodiscard]] ConnectStatus check_connection(gui_forms::PortRef from, gui_forms::PortRef to);

    // -- documents ------------------------------------------------------------------
    // Writes the canonical document into `out` (resized to fit; its storage
    // is reused across calls).
    void save_text(std::string& out);
    // Replaces the graph with the document (NodeGraph::load: validated
    // first, the graph unchanged on refusal) and fills `report`.
    [[nodiscard]] gui_forms::GraphLoadResult load_text(std::string_view text, LoadReport& report);
    [[nodiscard]] FileResult load_file(const std::filesystem::path& path, LoadReport& report);
    [[nodiscard]] FileResult save_file(const std::filesystem::path& path);
    // The file the document was loaded from or last saved to (empty: none).
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    // Autosave to "<path>.autosave" after every `edits` edits (zero: off).
    // Effort-bounded, not timed (spec 5).
    void set_autosave_interval(std::uint32_t edits) noexcept { autosave_interval_ = edits; }

    // -- reading ----------------------------------------------------------------------
    // The kind of a live node, or none (stale, or a kind the table lacks).
    [[nodiscard]] std::optional<KindIndex> kind_of(gui_forms::NodeId node) const noexcept;
    // The outgoing links per node slot, current with the graph.
    [[nodiscard]] const Topology& topology();
    [[nodiscard]] DocumentCounters counters() const noexcept { return counters_; }

private:
    // The next key "n<k>" no live node uses; empty when the counter is spent.
    [[nodiscard]] bool next_key(std::string& out);
    void edited();
    void rebuild_topology();
    // Whether `target` can be reached from `start` along links.
    [[nodiscard]] bool reaches(std::uint32_t start, std::uint32_t target);

    gui_forms::NodeGraph graph_{};
    Topology topology_{};
    // Reused work storage: the node spec for add_node, the reachability
    // walk's stack and visit marks, and the key text.
    gui_forms::NodeSpec spec_{};
    std::vector<std::uint32_t> walk_stack_{};
    std::vector<std::uint32_t> walk_marks_{};
    std::uint32_t walk_stamp_{};
    std::string key_{};
    std::string save_buffer_{};
    std::filesystem::path path_{};
    std::uint64_t next_key_number_{1U};
    std::uint32_t autosave_interval_{};
    std::uint32_t edits_since_autosave_{};
    std::size_t gesture_depth_{};
    // At the outermost begin_gesture: the undo log's size, so that
    // cancel_gesture knows whether the gesture recorded a step.
    std::size_t gesture_steps_before_{};
    std::size_t gesture_bytes_before_{};
    DocumentCounters counters_{};
};

} // namespace node_graph
