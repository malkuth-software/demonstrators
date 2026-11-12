#include "chat_view.hpp"

#include <gui_forms/event.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string_view>
#include <vector>

namespace chat_stress {

namespace gf = gui_forms;

namespace {

constexpr std::string_view menu_edit = "chat-stress.menu.edit";
constexpr std::string_view menu_delete = "chat-stress.menu.delete";
constexpr std::string_view menu_select_none = "chat-stress.menu.select-none";

void append_count(std::string& out, std::uint64_t value) {
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    if (written > 0) out.append(buffer, static_cast<std::size_t>(written));
}

} // namespace

ChatRoot::ChatRoot(gf::StableId stable_id, Corpus& corpus)
    : gf::Control(std::move(stable_id)), corpus_(corpus), rows_(corpus, atlas_), menu_("chat-stress.menu") {
    status_text_.reserve(256U);
}

ChatRoot::~ChatRoot() {
    // The view may outlive this control (a shared child): unbind the row
    // painter's targets before the painter goes.
    rows_.detach();
}

void ChatRoot::initialize_control_tree() {
    view_ = gf::make_control<gf::VirtualRowsView>(gf::StableId("chat-stress.log"));
    badge_ = gf::make_control<gf::Label>(gf::StableId("chat-stress.badge"));
    compose_ = gf::make_control<gf::TextBox>(gf::StableId("chat-stress.compose"));
    status_ = gf::make_control<gf::Label>(gf::StableId("chat-stress.status"));
    (*compose_).accepts_enter = true;
    (*compose_).accepts_escape = true;
    (*compose_).placeholder_text = std::string("Message");

    // Estimate for unmeasured rows (decision 5, one global estimate): a
    // header row with the mean 550-byte body at about 75 bytes a line.
    (*view_).set_estimated_row_height(120.0);
    (*view_).set_follow_tail(true);
    (*view_).set_measured_row_budget(measured_row_budget);
    (*view_).set_selection_mode(gf::ListSelectionMode::multiple_extended);
    (*view_).reset_rows(static_cast<std::size_t>(corpus_.row_count()));
    rows_.attach(*view_);

    add_child(view_);
    add_child(badge_);
    add_child(compose_);
    add_child(status_);

    gf::on((*compose_).committed(), *this, &ChatRoot::on_compose_committed);
    gf::on((*compose_).cancelled(), *this, &ChatRoot::on_compose_cancelled);
    gf::on((*view_).row_activated(), *this, &ChatRoot::on_row_activated);
    gf::on((*view_).selection_changed(), *this, &ChatRoot::on_selection_changed);
    gf::on((*view_).context_menu_requested(), *this, &ChatRoot::on_context_request);
    gf::on((*view_).scrolled(), *this, &ChatRoot::on_view_scrolled);
    gf::on(menu_.item_invoked(), *this, &ChatRoot::on_menu_item);
    rebuild_menu(std::nullopt);
    refresh_status();
}

// -- layout -------------------------------------------------------------------------

gf::Size ChatRoot::measure(gf::Size available) {
    return available;
}

void ChatRoot::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    const double width = final_bounds.width;
    double bottom = final_bounds.height;
    bottom -= status_height;
    const gf::Rect status{0.0, bottom, width, status_height};
    bottom -= compose_height;
    const gf::Rect compose{0.0, bottom, width, compose_height};
    double badge_size = 0.0;
    if (pending_new_ > 0U) badge_size = badge_height;
    bottom -= badge_size;
    const gf::Rect badge{0.0, bottom, width, badge_size};
    double log_height = bottom;
    if (log_height < 0.0) log_height = 0.0;
    const gf::Rect log{0.0, 0.0, width, log_height};
    // Publish each child's slot; the window arranges the children in the
    // same descent, right after this control. A child's own arrange is not
    // called here: the window would arrange it again from its slot (the
    // requested bounds when none was published), undoing the placement.
    set_child_layout(view_, log);
    set_child_layout(badge_, badge);
    set_child_layout(compose_, compose);
    set_child_layout(status_, status);
}

// -- operations ---------------------------------------------------------------------

std::int64_t ChatRoot::now_ms() const {
    if (test_time_ms_.has_value()) return *test_time_ms_;
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::chrono::milliseconds since = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return static_cast<std::int64_t>(since.count());
}

void ChatRoot::append_generated(std::uint64_t count) {
    if (count == 0U) return;
    const std::size_t at = (*view_).row_count();
    corpus_.append_generated(count);
    (*view_).insert_rows(gf::VirtualRowIndex{at}, static_cast<std::size_t>(count));
    counters_.appended_generated += count;
    if (!(*view_).following_tail()) pending_new_ += count;
    update_badge();
    refresh_status();
}

MessageId ChatRoot::append_local(const std::string& text) {
    const std::size_t at = (*view_).row_count();
    const MessageId message = corpus_.append_local(text, now_ms());
    (*view_).insert_rows(gf::VirtualRowIndex{at}, 1U);
    ++counters_.appended_local;
    // One's own message brings the view to the end, as chat clients do.
    (*view_).scroll_to_row(gf::VirtualRowIndex{at}, gf::VirtualRowAlignment::end);
    pending_new_ = 0U;
    update_badge();
    refresh_status();
    return message;
}

void ChatRoot::edit_row(RowIndex row, const std::string& text) {
    if (row.value >= corpus_.row_count()) return;
    const MessageId message = corpus_.message_of_row(row);
    corpus_.edit(message, text, now_ms());
    rows_.forget(message);
    // Measured again at once when in view (the anchor holds) and repainted.
    (*view_).invalidate_rows(gf::VirtualRowIndex{static_cast<std::size_t>(row.value)}, 1U);
    ++counters_.edits;
    refresh_status();
}

void ChatRoot::delete_row(RowIndex row) {
    if (row.value >= corpus_.row_count()) return;
    const MessageId message = corpus_.message_of_row(row);
    corpus_.tombstone(message, now_ms());
    rows_.forget(message);
    (*view_).invalidate_rows(gf::VirtualRowIndex{static_cast<std::size_t>(row.value)}, 1U);
    ++counters_.deletions;
    refresh_status();
}

void ChatRoot::erase_rows(RowIndex first, std::uint64_t count) {
    if (count == 0U || first.value >= corpus_.row_count()) return;
    const std::uint64_t available = corpus_.row_count() - first.value;
    const std::uint64_t erased = std::min(count, available);
    corpus_.erase_rows(first, erased);
    (*view_).erase_rows(gf::VirtualRowIndex{static_cast<std::size_t>(first.value)}, static_cast<std::size_t>(erased));
    // The row now at `first` has a new predecessor: its header may change.
    if (first.value < corpus_.row_count()) {
        (*view_).invalidate_rows(gf::VirtualRowIndex{static_cast<std::size_t>(first.value)}, 1U);
    }
    counters_.erased_rows += erased;
    ++counters_.erase_operations;
    refresh_status();
}

bool ChatRoot::go_to_message(MessageId message) {
    const RowIndex row = corpus_.row_of_message(message);
    if (row.value >= corpus_.row_count()) return false;
    const gf::VirtualRowIndex target{static_cast<std::size_t>(row.value)};
    (*view_).scroll_to_row(target, gf::VirtualRowAlignment::start);
    (*view_).set_selected_row(target);
    ++counters_.jumps;
    update_badge();
    refresh_status();
    return true;
}

void ChatRoot::update_badge() {
    if ((*view_).following_tail()) pending_new_ = 0U;
    std::string text{};
    if (pending_new_ > 0U) {
        append_count(text, pending_new_);
        text.append(" new messages");
    }
    if ((*badge_).text.read() != text) {
        const bool shown_before = !(*badge_).text.read().empty();
        (*badge_).text = text;
        ++counters_.badge_updates;
        // The badge takes room only while it has something to say.
        if (shown_before != !text.empty()) invalidate(gf::Dirty::arrange);
    }
}

void ChatRoot::refresh_status() {
    const gf::VirtualRowsStatistics statistics = (*view_).statistics();
    const RingCounters ring = rows_.ring_counters();
    const gf::ImageAtlasStatistics atlas = atlas_.statistics();
    status_text_.clear();
    append_count(status_text_, corpus_.row_count());
    status_text_.append(" messages | measured ");
    append_count(status_text_, statistics.rows_measured);
    status_text_.append(" | runs ");
    append_count(status_text_, statistics.index_runs);
    status_text_.append(" depth ");
    append_count(status_text_, statistics.index_depth);
    status_text_.append(" | index bytes ");
    append_count(status_text_, statistics.index_bytes);
    status_text_.append(" | ring ");
    append_count(status_text_, ring.hits);
    status_text_.push_back('/');
    append_count(status_text_, ring.misses);
    status_text_.append(" | avatars ");
    append_count(status_text_, atlas.hits);
    status_text_.push_back('/');
    append_count(status_text_, atlas.assignments + atlas.replacements);
    status_text_.push_back('/');
    append_count(status_text_, atlas.replacements);
    const std::size_t selected = (*view_).selected_row_count();
    if (selected > 1U) {
        status_text_.append(" | ");
        append_count(status_text_, selected);
        status_text_.append(" selected");
    }
    if ((*view_).following_tail()) status_text_.append(" | following");
    (*status_).text = status_text_;
    ++counters_.status_updates;
}

// -- compose box --------------------------------------------------------------------

void ChatRoot::on_compose_committed(const std::string& text) {
    if (text.empty()) return;
    if (editing_.has_value()) {
        const RowIndex row = corpus_.row_of_message(*editing_);
        editing_.reset();
        edit_row(row, text);
    } else {
        static_cast<void>(append_local(text));
    }
    (*compose_).text = std::string{};
}

void ChatRoot::on_compose_cancelled() {
    if (!editing_.has_value()) return;
    editing_.reset();
    (*compose_).text = std::string{};
}

// -- selection, activation, menu -----------------------------------------------------

void ChatRoot::on_row_activated(gf::VirtualRowIndex row) {
    // Enter on one's own message opens it for editing in the compose box.
    const RowIndex position{row.value};
    if (position.value >= corpus_.row_count()) return;
    const MessageId message = corpus_.message_of_row(position);
    if (corpus_.author(message) != local_user || corpus_.deleted(message)) return;
    std::string text{};
    corpus_.message_text(message, text);
    editing_ = message;
    (*compose_).text = text;
    gf::SurfaceRoot* root = attached_surface_root();
    if (root != nullptr) static_cast<void>((*root).request_focus(compose_));
}

void ChatRoot::on_selection_changed(const gf::VirtualRowSelection& selection) {
    static_cast<void>(selection);
    refresh_status();
}

void ChatRoot::rebuild_menu(std::optional<RowIndex> row) {
    bool own = false;
    bool live = false;
    if (row.has_value() && (*row).value < corpus_.row_count()) {
        const MessageId message = corpus_.message_of_row(*row);
        live = !corpus_.deleted(message);
        own = live && corpus_.author(message) == local_user;
    }
    std::vector<gf::MenuItemSpec> items{};
    items.reserve(3U);
    gf::MenuItemSpec edit(std::string(menu_edit), gf::MenuItemKind::command, {}, "Edit message");
    edit.enabled = own;
    gf::MenuItemSpec remove(std::string(menu_delete), gf::MenuItemKind::command, {}, "Delete message");
    remove.enabled = live;
    gf::MenuItemSpec none(std::string(menu_select_none), gf::MenuItemKind::command, {}, "Select none");
    none.enabled = (*view_).selected_row_count() > 0U;
    items.push_back(std::move(edit));
    items.push_back(std::move(remove));
    items.push_back(std::move(none));
    menu_.set_items(std::move(items));
}

void ChatRoot::on_context_request(gf::ContextMenuRequest& request) {
    std::optional<RowIndex> row{};
    gf::Point window_position = request.window_position;
    if (request.origin == gf::ContextRequestOrigin::pointer) {
        const std::optional<gf::VirtualRowIndex> hit = (*view_).row_at(request.location);
        if (hit.has_value()) row = RowIndex{(*hit).value};
    } else {
        // Shift+F10 or the Menu key: the selected row, the menu at its top left.
        const std::optional<gf::VirtualRowIndex> selected = (*view_).selected_row();
        if (selected.has_value()) {
            row = RowIndex{(*selected).value};
            const gf::Rect bounds = (*view_).row_bounds(*selected);
            window_position = (*view_).point_to_window({bounds.x + RowGeometry::text_x(), bounds.y});
        }
    }
    menu_row_ = row;
    rebuild_menu(row);
    const gf::MenuOpenMode mode = request.origin == gf::ContextRequestOrigin::pointer ? gf::MenuOpenMode::pointer
                                                                                      : gf::MenuOpenMode::keyboard;
    menu_.show(view_, window_position, mode);
    request.handled = true;
}

void ChatRoot::on_menu_item(const gf::MenuItemInvocation& invocation) {
    if (invocation.item_id == menu_select_none) {
        (*view_).clear_selection();
        return;
    }
    if (!menu_row_.has_value()) return;
    const RowIndex row = *menu_row_;
    menu_row_.reset();
    if (row.value >= corpus_.row_count()) return;
    if (invocation.item_id == menu_delete) {
        delete_row(row);
    } else if (invocation.item_id == menu_edit) {
        on_row_activated(gf::VirtualRowIndex{static_cast<std::size_t>(row.value)});
    }
}

void ChatRoot::on_view_scrolled(const gf::VirtualRowScroll& scroll) {
    // Back at the end (by wheel, keys, the scroll bar or a drag), the badge
    // clears; anywhere else it keeps counting.
    static_cast<void>(scroll);
    ++counters_.scroll_reports;
    update_badge();
}

} // namespace chat_stress
