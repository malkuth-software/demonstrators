#pragma once

// The node graph window's content (NODE_GRAPH_SPEC section 3): the canvas
// (GraphCanvas over GUI.Forms' NodeCanvas), a status line, the add-node
// palette (a VirtualRowsView of kinds, filtered to those that accept a
// dragged link's type) and the editors the canvas hosts (one TextBox per
// kind, made the first time a node of that kind is edited and reused
// after: spec 4.3, "created once per kind").
//
// Ownership: GraphRoot is a control (shared, owned by its window). It owns
// the document and the evaluator by value, declared before the children
// that borrow them, and its children. The editors are held here but are
// never this control's children: the canvas makes the open one its own
// child for the edit (NodeEditRequest::editor) and removes it at end_edit.
// The palette's row targets and the canvas's edit target are this
// control's members and are unbound in the destructor.
//
// Editing: double-click, F2 or open_edit asks the canvas, which asks this
// control's edit target for the kind's editor and opens an undo group;
// Enter commits the text (set_title or the kind's primary property) and
// closes the group as one step, Escape cancels (the group rolled back),
// and losing focus commits. `editing_` is cleared before the canvas ends
// the edit, so the focus loss that removing the editor causes commits
// nothing twice.
//
// One thread: the window's.

#include "evaluator.hpp"
#include "graph_canvas.hpp"
#include "graph_document.hpp"
#include "kinds.hpp"

#include <gui_forms/components/context_menu/context_menu.hpp>
#include <gui_forms/control.hpp>
#include <gui_forms/controls/label.hpp>
#include <gui_forms/controls/text_box.hpp>
#include <gui_forms/controls/virtual_rows_view.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace node_graph {

// The most kinds the palette lists and the editors cover.
inline constexpr std::size_t maximum_kinds = 32U;

struct GraphRootCounters final {
    std::uint64_t edit_hosts_created{};
    std::uint64_t edits_opened{};
    std::uint64_t edits_declined{};
    std::uint64_t edits_committed{};
    std::uint64_t edits_cancelled{};
    std::uint64_t edits_unchanged{};
    // Edits the canvas ended itself (its graph replaced, the window gone).
    std::uint64_t edits_ended_by_canvas{};
    std::uint64_t palette_opened{};
    std::uint64_t palette_insertions{};
    std::uint64_t palette_links{};
    std::uint64_t saves{};
    std::uint64_t save_failures{};
    std::uint64_t status_updates{};
};

// The edit in progress: the node, whether it renames (the caption, or a
// kind without a primary property), and the kind whose editor it uses.
struct OpenEdit final {
    gui_forms::NodeId node{};
    bool title{};
    KindIndex kind{};
};

class GraphRoot final : public gui_forms::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    static constexpr double status_height = 22.0;
    static constexpr double palette_width = 220.0;
    static constexpr double palette_row_height = 22.0;
    static constexpr double palette_rows_shown = 12.0;

    explicit GraphRoot(gui_forms::StableId stable_id);
    ~GraphRoot() override;
    GraphRoot(const GraphRoot&) = delete;
    GraphRoot& operator=(const GraphRoot&) = delete;
    void initialize_control_tree();

    // -- parts ----------------------------------------------------------------------------
    [[nodiscard]] GraphDocument& document() noexcept { return document_; }
    [[nodiscard]] Evaluator& evaluator() noexcept { return evaluator_; }
    [[nodiscard]] GraphCanvas& canvas() const noexcept { return *canvas_; }
    [[nodiscard]] const std::shared_ptr<GraphCanvas>& canvas_pointer() const noexcept { return canvas_; }
    [[nodiscard]] gui_forms::VirtualRowsView& palette() const noexcept { return *palette_; }
    [[nodiscard]] gui_forms::Label& status() const noexcept { return *status_; }
    [[nodiscard]] gui_forms::ContextMenu& menu() noexcept { return menu_; }
    [[nodiscard]] GraphRootCounters counters() const noexcept { return counters_; }

    // -- operations (the canvas's requests, tests and workloads) --------------------------
    // Asks the canvas to edit the node (it calls back for the editor); false
    // for a stale node or while the canvas refuses (a gesture, an edit).
    bool open_edit(gui_forms::NodeId node, gui_forms::NodeEditPart part);
    // The open editor, or null.
    [[nodiscard]] gui_forms::TextBox* edit_host() const noexcept;
    [[nodiscard]] const std::optional<OpenEdit>& open_edit_state() const noexcept { return editing_; }
    // Commits or cancels the open edit (what Enter and Escape do).
    void commit_edit(const std::string& text);
    void cancel_edit();
    void open_palette(const gui_forms::NodePaletteRequest& request);
    void close_palette();
    [[nodiscard]] bool palette_open() const noexcept { return palette_request_.has_value(); }
    // The kinds the palette lists now, in its row order.
    [[nodiscard]] const std::vector<KindIndex>& palette_kinds() const& noexcept { return palette_kinds_; }
    const std::vector<KindIndex>& palette_kinds() const&& = delete;
    // Inserts palette row `row` at the request's point, connected to the
    // request's port when there was one, as one undo step.
    bool insert_from_palette(std::size_t row);
    // Saves to the document's path; false with none or on failure.
    bool save();
    void refresh_status();

    // -- Control ---------------------------------------------------------------------------
    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;

private:
    // The canvas's edit target (NodeCanvasCallbacks::edit, through
    // GraphCanvas::set_edit_target): hands back the kind's editor.
    void provide_editor(gui_forms::NodeEditRequest& request);
    // Canvas events.
    void on_canvas_palette(const gui_forms::NodePaletteRequest& request);
    void on_canvas_edit_ended(const gui_forms::NodeEditEnded& ended);
    void on_canvas_edited(const gui_forms::NodeGraphEdited& edited);
    void on_canvas_selection(const gui_forms::NodeCanvasSelection& selection);
    void on_canvas_view(const gui_forms::SceneViewTransform& view);
    void on_save_requested();
    void on_context_request(gui_forms::ContextMenuRequest& request);
    void on_menu_item(const gui_forms::MenuItemInvocation& invocation);
    // Editor events (every editor's; only the open one's text is read).
    void on_host_committed(const std::string& text);
    void on_host_cancelled();
    void on_host_focus(bool focused);
    // Palette events and row targets.
    void on_palette_activated(gui_forms::VirtualRowIndex row);
    void measure_palette_row(gui_forms::VirtualRowMeasure& row);
    void paint_palette_row(gui_forms::VirtualRowPaint& row);
    void describe_palette_row(gui_forms::VirtualRowDescription& row);

    void rebuild_menu();

    // Declared before the children, which borrow them.
    GraphDocument document_;
    Evaluator evaluator_;
    std::shared_ptr<GraphCanvas> canvas_{};
    std::shared_ptr<gui_forms::Label> status_{};
    std::shared_ptr<gui_forms::VirtualRowsView> palette_{};
    std::array<std::shared_ptr<gui_forms::TextBox>, maximum_kinds> edit_hosts_{};
    gui_forms::ContextMenu menu_;
    std::optional<OpenEdit> editing_{};
    std::string edit_before_{};
    std::optional<gui_forms::NodePaletteRequest> palette_request_{};
    std::vector<KindIndex> palette_kinds_{};
    gui_forms::WorldPoint menu_world_{};
    std::string status_text_{};
    GraphRootCounters counters_{};
};

} // namespace node_graph
