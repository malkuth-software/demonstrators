// The chat window's content, headless: an application with no host, a
// window holding ChatRoot, layout and input dispatched by hand. Text is
// measured by GUI.Forms' renderer-free estimate (no text engine is
// attached), so the figures are about bounded work and behaviour, not
// pixels; offscreen_paint_tests paints with the real raster.

#include "../src/chat_view.hpp"
#include "test_support.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/window/window.hpp>

#include <cstdio>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gf = gui_forms;
using chat_stress::ChatRoot;
using chat_stress::Corpus;
using chat_stress::CorpusOptions;
using chat_stress::MessageId;
using chat_stress::RowIndex;
using chat_stress_tests::require;

namespace {

constexpr gf::Size window_size{1280.0, 800.0};

// One window over one corpus; the corpus is declared first so it outlives
// the window that borrows it.
struct Fixture final {
    Corpus corpus;
    std::shared_ptr<ChatRoot> root;
    gf::Window window;

    Fixture(gf::Application& application, std::uint64_t messages)
        : corpus(CorpusOptions{chat_stress::default_seed, messages, 5'000U, false}),
          root(gf::make_control<ChatRoot>(gf::StableId("chat-stress"), corpus)),
          window(application, root, window_size) {
        (*root).set_test_time(chat_stress::corpus_epoch_ms);
        window.perform_layout();
    }
};

void key(gf::Window& window, std::uint32_t physical_key, gf::Modifier modifiers) {
    gf::KeyEvent down{};
    down.action = gf::KeyAction::down;
    down.physical_key = physical_key;
    down.modifiers = modifiers;
    static_cast<void>(window.dispatch_key(down));
    gf::KeyEvent up = down;
    up.action = gf::KeyAction::up;
    static_cast<void>(window.dispatch_key(up));
    window.perform_layout();
}

void pointer(gf::Window& window, gf::PointerAction action, gf::Point position) {
    gf::PointerEvent event{};
    event.action = action;
    event.position = position;
    if (action != gf::PointerAction::move) event.button = gf::PointerButton::primary;
    event.click_count = action == gf::PointerAction::move ? 0U : 1U;
    static_cast<void>(window.dispatch_pointer(event));
}

void startup_is_bounded(gf::Application& application) {
    Fixture fixture(application, 1'000'000U);
    gf::VirtualRowsView& view = (*fixture.root).view();
    const gf::VirtualRowsStatistics statistics = view.statistics();
    require(view.row_count() == 1'000'000U, "the view has every row");
    require(statistics.rows_measured > 0U, "the rows in view are measured");
    // The viewport (about 720 units) and half a viewport of overscan each
    // side over rows at least ~20 units tall: far below this bound.
    require(statistics.rows_measured < 300U, "startup measures only the rows near the viewport");
    require(statistics.index_runs <= 2U * statistics.rows_measured + 2U, "unmeasured rows stay one run");
    require(fixture.corpus.counters().texts_generated < 1'000U, "no text is generated for unseen rows");
}

void follow_tail_and_badge(gf::Application& application) {
    Fixture fixture(application, 100'000U);
    ChatRoot& root = *fixture.root;
    gf::VirtualRowsView& view = root.view();
    require(fixture.window.request_focus(root.view_pointer()), "the log takes focus");
    key(fixture.window, gf::PhysicalKey::end, gf::Modifier::control);
    require(view.following_tail(), "at the end with follow-tail on, the view follows");
    root.append_generated(5U);
    fixture.window.perform_layout();
    require(view.following_tail(), "appends keep a following view at the end");
    require(view.end_visible_row().value == view.row_count(), "the last row is in view");
    require(root.pending_new_messages() == 0U, "no badge while following");
    require(!view.announcement().empty(), "appends are announced politely");

    require(root.go_to_message(MessageId{50'000U}), "jump to the middle");
    fixture.window.perform_layout();
    const gf::VirtualRowIndex first = view.first_visible_row();
    root.append_generated(7U);
    fixture.window.perform_layout();
    require(!view.following_tail(), "scrolled away, the view does not follow");
    require(view.first_visible_row() == first, "appends below do not move the rows in view");
    require(root.pending_new_messages() == 7U, "the badge counts the new messages");

    // Back to the end: the view's scrolled event clears the badge.
    require(fixture.window.request_focus(root.view_pointer()), "the log takes focus again");
    key(fixture.window, gf::PhysicalKey::end, gf::Modifier::control);
    require(view.following_tail(), "at the end again, the view follows");
    require(root.pending_new_messages() == 0U, "returning to the end clears the badge");
    require(root.counters().scroll_reports > 0U, "the scrolled event reached the badge");
}

void anchor_holds_for_edits_above(gf::Application& application) {
    Fixture fixture(application, 100'000U);
    ChatRoot& root = *fixture.root;
    gf::VirtualRowsView& view = root.view();
    require(root.go_to_message(MessageId{40'000U}), "jump");
    fixture.window.perform_layout();
    // Scroll up a little so rows above the anchor are measured.
    view.scroll_to_offset(view.scroll_offset() - 200.0);
    view.scroll_to_offset(view.scroll_offset() + 200.0);
    fixture.window.perform_layout();
    const gf::VirtualRowIndex anchor = view.first_visible_row();
    require(anchor.value > 2U, "rows above the anchor exist");
    const gf::VirtualRowIndex above{anchor.value - 2U};
    require(view.row_measured(above), "the row above is measured");
    const double before = view.row_bounds(anchor).y;
    std::string longer(900U, 'w');
    for (std::size_t byte = 7U; byte < longer.size(); byte += 8U) longer[byte] = ' ';
    root.edit_row(RowIndex{above.value}, longer);
    fixture.window.perform_layout();
    require(view.row_bounds(anchor).y == before, "the anchor row stays where it is on screen");
    require(view.statistics().anchor_corrections > 0U, "the view corrected its offset");
}

void compose_and_edit(gf::Application& application) {
    Fixture fixture(application, 1'000U);
    ChatRoot& root = *fixture.root;
    gf::VirtualRowsView& view = root.view();
    require(fixture.window.request_focus(root.compose_pointer()), "the compose box takes focus");
    root.compose().text = std::string("hello there");
    key(fixture.window, gf::PhysicalKey::enter, gf::Modifier::none);
    require(fixture.corpus.row_count() == 1'001U && view.row_count() == 1'001U, "Enter sends the message");
    const MessageId sent{1'000U};
    require(fixture.corpus.author(sent) == chat_stress::local_user, "the sent message is the local user's");
    require(root.compose().text.read().empty(), "the compose box clears");
    require(view.end_visible_row().value == view.row_count(), "one's own message brings the view to the end");

    view.set_selected_row(gf::VirtualRowIndex{1'000U});
    require(fixture.window.request_focus(root.view_pointer()), "the log takes focus");
    key(fixture.window, gf::PhysicalKey::enter, gf::Modifier::none);
    require(root.editing().has_value() && (*root.editing()) == sent, "Enter on one's own message edits it");
    require(root.compose().text.read() == "hello there", "the compose box holds the text");
    root.compose().text = std::string("hello again");
    key(fixture.window, gf::PhysicalKey::enter, gf::Modifier::none);
    std::string text{};
    fixture.corpus.message_text(sent, text);
    require(text == "hello again" && !root.editing().has_value(), "Enter commits the edit");

    // Enter on someone else's message does not edit.
    view.set_selected_row(gf::VirtualRowIndex{3U});
    require(fixture.window.request_focus(root.view_pointer()), "the log takes focus again");
    key(fixture.window, gf::PhysicalKey::enter, gf::Modifier::none);
    require(!root.editing().has_value(), "only one's own messages are editable");
}

void erase_keeps_view_and_corpus_aligned(gf::Application& application) {
    Fixture fixture(application, 10'000U);
    ChatRoot& root = *fixture.root;
    root.erase_rows(RowIndex{0U}, 10U);
    root.erase_rows(RowIndex{5'000U}, 2'000U);
    fixture.window.perform_layout();
    require(fixture.corpus.row_count() == 7'990U, "rows erased from the corpus");
    require(root.view().row_count() == 7'990U, "and from the view");
    require(fixture.corpus.message_of_row(RowIndex{0U}).value == 10U, "row 0 is message 10");
    require(fixture.corpus.message_of_row(RowIndex{5'000U}).value == 7'010U, "the gap is skipped");
}

void drag_selects_a_range(gf::Application& application) {
    Fixture fixture(application, 10'000U);
    ChatRoot& root = *fixture.root;
    gf::VirtualRowsView& view = root.view();
    require(view.selection_mode() == gf::ListSelectionMode::multiple_extended, "the log selects ranges");
    const gf::VirtualRowIndex first = view.first_visible_row();
    const gf::Rect top_row = view.rectangle_to_window(view.row_bounds(first));
    const gf::VirtualRowIndex third{first.value + 3U};
    const gf::Rect lower_row = view.rectangle_to_window(view.row_bounds(third));
    const gf::Point press{top_row.x + 200.0, top_row.y + top_row.height / 2.0};
    const gf::Point release{lower_row.x + 200.0, lower_row.y + lower_row.height / 2.0};
    pointer(fixture.window, gf::PointerAction::move, press);
    pointer(fixture.window, gf::PointerAction::down, press);
    pointer(fixture.window, gf::PointerAction::move, gf::Point{press.x, press.y + 12.0});
    pointer(fixture.window, gf::PointerAction::move, release);
    pointer(fixture.window, gf::PointerAction::up, release);
    fixture.window.perform_layout();
    const std::span<const gf::VirtualRowRange> ranges = view.selected_ranges();
    require(ranges.size() == 1U, "the drag selected one range");
    require(ranges[0].first == first && ranges[0].count == 4U,
            "the range runs from the pressed row to the row under the pointer");
    require(view.selected_row_count() == 4U, "four rows are selected");
}

void budget_bounds_measured_rows(gf::Application& application) {
    Fixture fixture(application, 1'000'000U);
    ChatRoot& root = *fixture.root;
    gf::VirtualRowsView& view = root.view();
    require(view.measured_row_budget().has_value() &&
                *view.measured_row_budget() == ChatRoot::measured_row_budget,
            "the log keeps a measured-row budget");
    // Jumps far apart measure fresh rows each time, more in all than the budget.
    for (std::uint64_t jump = 0U; jump < 1'200U; ++jump) {
        require(root.go_to_message(MessageId{jump * 800U}), "jump");
        fixture.window.perform_layout();
    }
    const gf::VirtualRowsStatistics statistics = view.statistics();
    require(statistics.rows_measured > ChatRoot::measured_row_budget, "more rows were measured than the budget");
    // At most budget + 1 rows plus the settle's band (a viewport and its
    // overscan: well under a thousand rows here) stay measured.
    require(statistics.index_measured <= ChatRoot::measured_row_budget + 1'000U, "measured rows stay bounded");
    require(statistics.rows_forgotten > 0U && statistics.budget_compactions > 0U, "the budget forgot rows");
    require(statistics.index_bytes > 0U, "the index reports its bytes");
}

void semantics_name_realized_rows(gf::Application& application) {
    Fixture fixture(application, 10'000U);
    gf::VirtualRowsView& view = (*fixture.root).view();
    const std::vector<gf::SemanticNode> children = view.semantic_virtual_children();
    const std::size_t realized = view.realized_item_count();
    require(realized > 0U, "rows are realized");
    require(children.size() == realized + 1U, "the realized rows and the live region");
    require((*fixture.root).rows().counters().rows_described >= realized, "each realized row was described");
}

void same_seed_same_heights(gf::Application& application) {
    Fixture first(application, 50'000U);
    Fixture second(application, 50'000U);
    require((*first.root).view().content_height() == (*second.root).view().content_height(),
            "the same seed lays out the same heights");
}

} // namespace

int main() {
    try {
        gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
        require(static_cast<bool>(created), "the application is made");
        gf::Application& application = *created.application;
        startup_is_bounded(application);
        follow_tail_and_badge(application);
        anchor_holds_for_edits_above(application);
        compose_and_edit(application);
        erase_keeps_view_and_corpus_aligned(application);
        drag_selects_a_range(application);
        budget_bounds_measured_rows(application);
        semantics_name_realized_rows(application);
        same_seed_same_heights(application);
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "chat_view_tests: %s\n", failure.what());
        return 1;
    }
    std::printf("chat_view_tests: ok\n");
    return 0;
}
