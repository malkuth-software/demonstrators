#pragma once

// The chat window's content (CHAT_STRESS_SPEC section 4.1): the chat log (a
// GUI.Forms VirtualRowsView painted by ChatRows), a "new messages" badge, the
// compose box (TextBox) and a status line with live counters. It owns the
// interaction: selection, Enter to edit one's own message, the context menu,
// drag selection of a range of messages (DragTracker, with auto-scroll), and
// the append, edit, delete and erase operations the producers and workloads
// call.
//
// Ownership: ChatRoot is a control (shared, owned by its window); it owns
// its children, the atlas, the row painter, the menu and the drag tracker;
// it borrows the corpus, which must outlive it. The row painter's targets
// are unbound in the destructor, before the painter goes.
//
// One thread: the window's.

#include "avatar_atlas.hpp"
#include "chat_rows.hpp"
#include "chat_types.hpp"
#include "corpus.hpp"

#include <gui_forms/components/context_menu/context_menu.hpp>
#include <gui_forms/control.hpp>
#include <gui_forms/controls/label.hpp>
#include <gui_forms/controls/text_box.hpp>
#include <gui_forms/controls/virtual_rows_view.hpp>
#include <gui_forms/drag/drag_tracker/drag_tracker.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace chat_stress {

// Counters ChatRoot keeps for its own operations (they only grow).
struct ChatRootCounters final {
    std::uint64_t appended_generated{};
    std::uint64_t appended_local{};
    std::uint64_t edits{};
    std::uint64_t deletions{};
    std::uint64_t erased_rows{};
    std::uint64_t erase_operations{};
    std::uint64_t jumps{};
    std::uint64_t drag_sessions{};
    std::uint64_t drag_scroll_steps{};
    std::uint64_t badge_updates{};
    std::uint64_t status_updates{};
};

class ChatRoot final : public gui_forms::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    static constexpr double compose_height = 34.0;
    static constexpr double status_height = 22.0;
    static constexpr double badge_height = 22.0;

    ChatRoot(gui_forms::StableId stable_id, Corpus& corpus);
    ~ChatRoot() override;
    ChatRoot(const ChatRoot&) = delete;
    ChatRoot& operator=(const ChatRoot&) = delete;
    // Makes the children (make_control calls it once the control is shared).
    void initialize_control_tree();

    // -- operations (producers, workloads, the compose box) -------------------
    // Appends `count` generated messages at the end. Following the tail keeps
    // the view at the bottom; otherwise the badge counts them.
    void append_generated(std::uint64_t count);
    MessageId append_local(const std::string& text);
    void edit_row(RowIndex row, const std::string& text);
    void delete_row(RowIndex row);
    void erase_rows(RowIndex first, std::uint64_t count);
    // Scrolls message `message` (or the next surviving one) to the top and
    // selects it; false past the end.
    bool go_to_message(MessageId message);
    // The test time source: when set, typed messages and edits are stamped
    // with it instead of the system clock.
    void set_test_time(std::optional<std::int64_t> now_ms) noexcept { test_time_ms_ = now_ms; }
    // Rewrites the status line (also done after every operation).
    void refresh_status();

    // -- parts ---------------------------------------------------------------------
    [[nodiscard]] gui_forms::VirtualRowsView& view() const noexcept { return *view_; }
    [[nodiscard]] const std::shared_ptr<gui_forms::VirtualRowsView>& view_pointer() const noexcept { return view_; }
    [[nodiscard]] gui_forms::TextBox& compose() const noexcept { return *compose_; }
    [[nodiscard]] const std::shared_ptr<gui_forms::TextBox>& compose_pointer() const noexcept { return compose_; }
    [[nodiscard]] gui_forms::Label& status() const noexcept { return *status_; }
    [[nodiscard]] ChatRows& rows() noexcept { return rows_; }
    [[nodiscard]] const ChatRows& rows() const noexcept { return rows_; }
    [[nodiscard]] AvatarAtlas& atlas() noexcept { return atlas_; }
    [[nodiscard]] const AvatarAtlas& atlas() const noexcept { return atlas_; }
    [[nodiscard]] Corpus& corpus() const noexcept { return corpus_; }
    [[nodiscard]] gui_forms::ContextMenu& menu() noexcept { return menu_; }
    [[nodiscard]] std::optional<RowRange> range() const noexcept { return rows_.range(); }
    [[nodiscard]] std::uint64_t pending_new_messages() const noexcept { return pending_new_; }
    // The message being edited from the compose box, or none.
    [[nodiscard]] std::optional<MessageId> editing() const noexcept { return editing_; }
    [[nodiscard]] ChatRootCounters counters() const noexcept { return counters_; }
    [[nodiscard]] gui_forms::DragTrackerStatistics drag_statistics() const noexcept;

    // -- Control ---------------------------------------------------------------------
    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    // Places the children itself (arrange_self, then each child's slot
    // through set_child_layout): the log fills what the badge, the compose
    // box and the status line leave.
    void arrange(gui_forms::Rect final_bounds) override;

private:
    // Event handlers (named targets with this control as context).
    void on_compose_committed(const std::string& text);
    void on_compose_cancelled();
    void on_row_activated(gui_forms::VirtualRowIndex row);
    void on_selection_changed(const gui_forms::VirtualRowSelection& selection);
    void on_context_request(gui_forms::ContextMenuRequest& request);
    void on_menu_item(const gui_forms::MenuItemInvocation& invocation);
    void on_view_mouse_down(const gui_forms::MouseEventArgs& arguments);
    void on_view_mouse_move(const gui_forms::MouseEventArgs& arguments);
    void on_view_mouse_up(const gui_forms::MouseEventArgs& arguments);
    void on_view_wheel(const gui_forms::MouseEventArgs& arguments);
    void on_view_key(gui_forms::KeyEventArgs& arguments);

    // Drag selection targets (DragTrackerCallbacks).
    void plan_drag(gui_forms::DragPlan& plan);
    void preview_drag(const gui_forms::DragTrackerFrame& frame);
    void finish_drag(const gui_forms::DragTrackerFrame& frame);
    void announce_drag(gui_forms::DragAnnouncementRequest& request);
    void scroll_drag(gui_forms::DragAutoScrollStep& step);

    // The view's viewport in window coordinates.
    [[nodiscard]] gui_forms::Rect viewport_in_window() const;
    // The row under a window point, clamped into the viewport vertically.
    [[nodiscard]] std::optional<RowIndex> row_at_window_point(gui_forms::Point window_point) const;
    void update_badge();
    [[nodiscard]] std::int64_t now_ms() const;
    void rebuild_menu(std::optional<RowIndex> row);

    Corpus& corpus_;
    AvatarAtlas atlas_;
    ChatRows rows_;
    std::shared_ptr<gui_forms::VirtualRowsView> view_{};
    std::shared_ptr<gui_forms::Label> badge_{};
    std::shared_ptr<gui_forms::TextBox> compose_{};
    std::shared_ptr<gui_forms::Label> status_{};
    gui_forms::ContextMenu menu_;
    std::optional<RowIndex> menu_row_{};
    std::optional<MessageId> editing_{};
    std::optional<std::int64_t> test_time_ms_{};
    std::uint64_t pending_new_{};
    // The drag selection: the row the press was on.
    RowIndex drag_anchor_{};
    std::string drag_announcement_{};
    std::string status_text_{};
    ChatRootCounters counters_{};
    // Declared last: it borrows this control and ends first.
    std::unique_ptr<gui_forms::DragTracker> drag_{};
};

} // namespace chat_stress
