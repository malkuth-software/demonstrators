#pragma once

// The node graph window's content (NODE_GRAPH_SPEC section 3): the canvas,
// a status line, the add-node palette (a VirtualRowsView of kinds, filtered
// to those that accept a dragged link's type) and the edit hosts (one
// TextBox per kind, made the first time a node of that kind is edited and
// reused after: spec 4.3, "created once per kind").
//
// Ownership: GraphRoot is a control (shared, owned by its window). It owns
// the document and the evaluator by value, declared before the children
// that borrow them, and its children; the palette's row targets are this
// control's members and are unbound in the destructor.
//
// Edit hosts: Enter commits the text as one undo step (set_title or the
// kind's primary property), Escape cancels without touching the document,
// and losing focus commits. `editing_` is cleared before the host is
// hidden, so the focus loss that hiding causes commits nothing twice.
//
// One thread: the window's.

#include "evaluator.hpp"
#include "graph_document.hpp"
#include "kinds.hpp"
#include "node_canvas_view.hpp"

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

// The most kinds the palette lists and the edit hosts cover.
inline constexpr std::size_t maximum_kinds = 32U;

struct GraphRootCounters final {
    std::uint64_t edit_hosts_created{};
    std::uint64_t edits_opened{};
    std::uint64_t edits_committed{};
    std::uint64_t edits_cancelled{};
    std::uint64_t edits_unchanged{};
    std::uint64_t palette_opened{};
    std::uint64_t palette_insertions{};
    std::uint64_t palette_links{};
    std::uint64_t saves{};
    std::uint64_t save_failures{};
    std::uint64_t status_updates{};
};

class GraphRoot final : public gui_forms::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    static constexpr double status_height = 22.0;
    static constexpr double palette_width = 220.0;
    static constexpr double palette_row_height = 22.0;
    static constexpr double palette_rows_shown = 12.0;
    static constexpr double edit_host_height = 24.0;

    explicit GraphRoot(gui_forms::StableId stable_id);
    ~GraphRoot() override;
    GraphRoot(const GraphRoot&) = delete;
    GraphRoot& operator=(const GraphRoot&) = delete;
    void initialize_control_tree();

    // -- parts ----------------------------------------------------------------------------
    [[nodiscard]] GraphDocument& document() noexcept { return document_; }
    [[nodiscard]] Evaluator& evaluator() noexcept { return evaluator_; }
    [[nodiscard]] NodeCanvasView& canvas() const noexcept { return *canvas_; }
    [[nodiscard]] const std::shared_ptr<NodeCanvasView>& canvas_pointer() const noexcept { return canvas_; }
    [[nodiscard]] gui_forms::VirtualRowsView& palette() const noexcept { return *palette_; }
    [[nodiscard]] gui_forms::Label& status() const noexcept { return *status_; }
    [[nodiscard]] gui_forms::ContextMenu& menu() noexcept { return menu_; }
    [[nodiscard]] GraphRootCounters counters() const noexcept { return counters_; }

    // -- operations (the canvas's requests, tests and workloads) --------------------------
    // Opens the kind's edit host over the node; false for a stale node.
    bool open_edit(const EditRequest& request);
    // The open edit host, or null.
    [[nodiscard]] gui_forms::TextBox* edit_host() const noexcept;
    // Commits or cancels the open edit (what Enter and Escape do).
    void commit_edit(const std::string& text);
    void cancel_edit();
    void open_palette(const PaletteRequest& request);
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
    // Canvas events.
    void on_edit_requested(const EditRequest& request);
    void on_palette_requested(const PaletteRequest& request);
    void on_canvas_changed();
    void on_save_requested();
    void on_context_request(gui_forms::ContextMenuRequest& request);
    void on_menu_item(const gui_forms::MenuItemInvocation& invocation);
    // Edit host events (the open host's).
    void on_host_committed(const std::string& text);
    void on_host_cancelled();
    void on_host_focus(bool focused);
    // Palette events and row targets.
    void on_palette_activated(gui_forms::VirtualRowIndex row);
    void measure_palette_row(gui_forms::VirtualRowMeasure& row);
    void paint_palette_row(gui_forms::VirtualRowPaint& row);
    void describe_palette_row(gui_forms::VirtualRowDescription& row);

    void rebuild_menu();
    void close_edit_host();
    [[nodiscard]] gui_forms::Rect edit_host_rect() const;

    // Declared before the children, which borrow them.
    GraphDocument document_;
    Evaluator evaluator_;
    std::shared_ptr<NodeCanvasView> canvas_{};
    std::shared_ptr<gui_forms::Label> status_{};
    std::shared_ptr<gui_forms::VirtualRowsView> palette_{};
    std::array<std::shared_ptr<gui_forms::TextBox>, maximum_kinds> edit_hosts_{};
    gui_forms::ContextMenu menu_;
    std::optional<EditRequest> editing_{};
    std::optional<KindIndex> editing_kind_{};
    std::string edit_before_{};
    std::optional<PaletteRequest> palette_request_{};
    gui_forms::Point palette_origin_{};
    std::vector<KindIndex> palette_kinds_{};
    gui_forms::WorldPoint menu_world_{};
    std::string status_text_{};
    GraphRootCounters counters_{};
};

} // namespace node_graph
