#pragma once

// Row layout for the chat log (CHAT_STRESS_SPEC section 4.3): wrapping a
// message's body at the column width with GUI.Forms' text metrics, the row
// geometry, and the bounded ring of row layouts the view keeps for the rows
// in and near the viewport.
//
// Public API used: SurfaceRoot::fill_line_break_opportunities_utf8,
// SurfaceRoot::measure_text_extent_utf8, SurfaceRoot::resolve_text_layout_utf8,
// line_box_metrics, and the renderer-free estimates for a window without a
// text engine. GUI.Forms has no public "wrap this paragraph into my
// storage" call (TextBlock's breaker is private), so the greedy breaker
// below re-measures each candidate line; GAPS.md 3 records what that costs.

#include "chat_types.hpp"

#include <gui_forms/surface_root/surface_root.hpp>
#include <gui_forms/typography/types/typography_types.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chat_stress {

// One wrapped line: a byte range of the row's text, trailing spaces
// excluded, and its measured width in logical units.
struct WrappedLine final {
    std::uint32_t begin{};
    std::uint32_t end{};
    double width{};
};

// The wrapper's work counters (they only grow).
struct WrapCounters final {
    std::uint64_t paragraphs{};
    std::uint64_t lines{};
    std::uint64_t measure_calls{};
    std::uint64_t measured_bytes{};
    std::uint64_t inside_word_breaks{};
};

// Greedy line breaking at the window's break opportunities (UAX #14 with the
// bundled engine, white space with the estimate). A word wider than the line
// is broken inside, at the longest character prefix that fits (at least one
// character), as the spec's long URLs need.
class TextWrapper final {
public:
    // Reserves the opportunity scratch for a text of `capacity` bytes.
    explicit TextWrapper(std::size_t capacity);

    // Fills `lines` (cleared first, capacity reused) with `text` wrapped at
    // `width` in `font`, measured by `root` (null: the renderer-free
    // estimate). Returns the line count: zero for an empty text. A width
    // that is not positive puts each break opportunity's run on its own line.
    std::size_t wrap(const gui_forms::SurfaceRoot* root, std::string_view text, gui_forms::FontSpec font,
                     double width, std::vector<WrappedLine>& lines);

    // One run's width (counted as a measure call).
    [[nodiscard]] double measure(const gui_forms::SurfaceRoot* root, std::string_view text, gui_forms::FontSpec font);

    [[nodiscard]] WrapCounters counters() const noexcept { return counters_; }
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

private:
    // The end (exclusive) of the longest character prefix of
    // [begin, limit) that fits `width`, at least one character.
    std::size_t break_inside(const gui_forms::SurfaceRoot* root, std::string_view text, std::size_t begin,
                             std::size_t limit, gui_forms::FontSpec font, double width);

    std::vector<gui_forms::LineBreakOpportunity> opportunities_{};
    WrapCounters counters_{};
};

// The line box of a font: height and baseline from its top.
[[nodiscard]] gui_forms::LineBoxMetrics font_line_box(const gui_forms::SurfaceRoot* root, gui_forms::FontSpec font);

// The fonts a row is set in and their line boxes, resolved once per theme
// and text engine (RowFonts::generation changes when any of them does).
struct RowFonts final {
    gui_forms::FontSpec body{};
    gui_forms::FontSpec name{};
    gui_forms::FontSpec meta{};
    gui_forms::FontSpec initials{};
    gui_forms::LineBoxMetrics body_line{};
    gui_forms::LineBoxMetrics name_line{};
    gui_forms::LineBoxMetrics meta_line{};
    gui_forms::LineBoxMetrics initials_line{};
    std::uint64_t generation{};
};

// Row geometry in logical units (section 4.3). Left to right; the painter
// mirrors x for right-to-left rows.
struct RowGeometry final {
    static constexpr double margin_x = 12.0;
    static constexpr double avatar = 40.0;
    static constexpr double avatar_gap = 10.0;
    static constexpr double header_top = 10.0;
    static constexpr double continuation_top = 2.0;
    static constexpr double bottom = 4.0;
    static constexpr double day_band = 28.0;
    // A continuation merges with the previous row when the author is the
    // same and it came within this long.
    static constexpr std::int64_t continuation_ms = 5 * 60 * 1000;

    [[nodiscard]] static double text_x() noexcept { return margin_x + avatar + avatar_gap; }
    // The body's wrap width at a row width; never below one unit.
    [[nodiscard]] static double text_width(double row_width) noexcept;
};

// One cached row layout: everything paint and describe read, filled once
// when the row is laid out at a width and reused until its message, its
// predecessor, its edit revision, the width or the fonts change.
struct RowLayoutEntry final {
    bool valid{};
    MessageId message{};
    // The previous row's message (header grouping depends on it), or the
    // message itself for row 0.
    MessageId previous{};
    double width{};
    std::uint64_t revision{};
    std::uint64_t font_generation{};

    std::string text{};
    std::vector<WrappedLine> lines{};
    UserId author{};
    std::int64_t timestamp_ms{};
    bool header{};
    bool day_separator{};
    bool edited{};
    bool deleted{};
    double height{};
};

// The ring's counters (they only grow).
struct RingCounters final {
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t replacements{};
};

// A direct-mapped ring of row layouts keyed by message identity: message m
// lives in slot m mod capacity, so any run of up to `capacity` consecutive
// rows never collides (the viewport and its overscan are such a run until
// rows are erased). Every slot's storage is reserved at construction
// (maximum_text_bytes of text, a line vector), so steady scrolling refills
// slots in place without allocating. One thread.
class RowLayoutRing final {
public:
    static constexpr std::size_t default_capacity = 256U;
    static constexpr std::size_t reserved_lines = 64U;

    // Throws std::invalid_argument for a capacity of zero.
    RowLayoutRing(std::size_t capacity, std::size_t text_capacity);

    // The slot message `message` maps to (it may hold another message).
    [[nodiscard]] RowLayoutEntry& slot(MessageId message) noexcept;
    // Whether `entry` already holds this layout; counts a hit or a miss.
    [[nodiscard]] bool matches(const RowLayoutEntry& entry, MessageId message, MessageId previous, double width,
                               std::uint64_t revision, std::uint64_t font_generation) noexcept;
    // Forgets every slot (fonts or width changed wholesale).
    void invalidate_all() noexcept;
    // Forgets the slot holding `message`, if it does.
    void invalidate(MessageId message) noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept { return entries_.size(); }
    [[nodiscard]] RingCounters counters() const noexcept { return counters_; }
    // Reserved bytes: the entries and their text and line storage.
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

private:
    std::vector<RowLayoutEntry> entries_{};
    RingCounters counters_{};
};

} // namespace chat_stress
