#pragma once

// The chat row painter: the measure, paint and describe targets the chat
// log's VirtualRowsView calls (CHAT_STRESS_SPEC sections 4.3 and 4.5). The
// view keeps the heights, the anchor, follow-tail and the scroll; this
// object turns a row position into a laid-out message and draws it.
//
// Lifetime: the corpus and the atlas are borrowed and must outlive this
// object; the view is observed (attach/detach), and detach() must run
// before this object or the view's other targets go away (ChatRoot's
// destructor does it). One thread: the view's.

#include "avatar_atlas.hpp"
#include "chat_types.hpp"
#include "corpus.hpp"
#include "row_layout.hpp"

#include <gui_forms/controls/virtual_rows_view.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace chat_stress {

// The painter's own work counters (section 6); they only grow.
struct ChatRowsCounters final {
    std::uint64_t rows_measured{};
    std::uint64_t rows_painted{};
    std::uint64_t rows_described{};
    std::uint64_t lines_drawn{};
    std::uint64_t font_generations{};
    std::uint64_t stale_rows_refused{};
};

class ChatRows final {
public:
    // Most bytes of message text in a row's accessible name (the value
    // carries the whole text).
    static constexpr std::size_t accessible_name_text_bytes = 80U;

    ChatRows(Corpus& corpus, AvatarAtlas& atlas);
    ChatRows(const ChatRows&) = delete;
    ChatRows& operator=(const ChatRows&) = delete;

    // Binds this object's targets to `view` (VirtualRowsView::set_row_callbacks).
    void attach(gui_forms::VirtualRowsView& view);
    // Unbinds them (empty delegates) and stops observing the view.
    void detach();

    // -- the view's targets ----------------------------------------------------
    void measure(gui_forms::VirtualRowMeasure& request);
    void paint(gui_forms::VirtualRowPaint& request);
    void describe(gui_forms::VirtualRowDescription& request);

    // The laid-out row, from the ring or laid out now at `row_width`.
    // Precondition: row < corpus row_count().
    const RowLayoutEntry& layout_row(RowIndex row, double row_width);

    // Forgets the cached layout of a message (its text changed).
    void forget(MessageId message) noexcept { ring_.invalidate(message); }
    void forget_all() noexcept { ring_.invalidate_all(); }

    [[nodiscard]] ChatRowsCounters counters() const noexcept { return counters_; }
    [[nodiscard]] RingCounters ring_counters() const noexcept { return ring_.counters(); }
    [[nodiscard]] TextLayoutCounters text_counters() const noexcept { return wrapper_.counters(); }
    [[nodiscard]] std::size_t ring_capacity() const noexcept { return ring_.capacity(); }
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
    [[nodiscard]] const RowFonts& fonts() const noexcept { return fonts_; }

private:
    // Resolves the row fonts from the view's theme and the window's text
    // engine; a change bumps fonts_.generation (every cached layout is then
    // stale).
    void refresh_fonts();
    // The window's text engine, or null (the renderer-free estimates).
    [[nodiscard]] gui_forms::TextMetricsProvider* metrics_provider() const noexcept;
    void paint_day_separator(gui_forms::Painter& painter, const RowLayoutEntry& entry, gui_forms::Rect bounds,
                             gui_forms::Color rule, gui_forms::Color text);
    void paint_avatar(gui_forms::Painter& painter, const RowLayoutEntry& entry, gui_forms::Rect avatar);

    Corpus& corpus_;
    AvatarAtlas& atlas_;
    gui_forms::VirtualRowsView* view_{};
    RowLayoutRing ring_;
    ParagraphWrapper wrapper_;
    RowFonts fonts_{};
    // Formatting scratch, reused (paint and describe never allocate once warm).
    std::string scratch_{};
    ChatRowsCounters counters_{};
};

// Writes "HH:MM" of a UTC time into `out` (replacing its contents).
void format_clock(std::int64_t utc_ms, std::string& out);
// Writes "Mon 2020-01-06" of a UTC time into `out`.
void format_day(std::int64_t utc_ms, std::string& out);

} // namespace chat_stress
