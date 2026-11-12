#pragma once

// Row layout for the chat log (CHAT_STRESS_SPEC section 4.3): a message's
// body wrapped at the column width by GUI.Forms' public paragraph layout
// (gui_forms/text/paragraph_layout: lay_out_paragraph into caller storage),
// the row geometry, and the bounded ring of row layouts kept for the rows in
// and near the viewport.
//
// Storage: the paragraph scratch (break opportunities and cluster advances,
// one element per byte) is one ParagraphBuffers reserved for the longest
// message; each ring slot owns its line records, reserved once, and the
// layout writes into them directly. A layout that needs more lines than a
// slot holds grows that slot once and lays out again (counted).

#include "chat_types.hpp"

#include <gui_forms/text/paragraph_layout/paragraph_layout.hpp>
#include <gui_forms/typography/types/typography_types.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chat_stress {

// The paragraph layout's work, summed over every call (they only grow).
// shaping_calls and shaped_bytes are ParagraphMetrics' own figures.
struct TextLayoutCounters final {
    std::uint64_t paragraphs{};
    std::uint64_t lines{};
    std::uint64_t shaping_calls{};
    std::uint64_t shaped_bytes{};
    std::uint64_t overflowing_lines{};
    // Calls repeated after growing storage (storage_too_small), and refused
    // calls (invalid UTF-8 or font; the row then shows no body lines).
    std::uint64_t storage_retries{};
    std::uint64_t refusals{};
};

// One paragraph's layout into a slot's line records.
struct LaidOutParagraph final {
    std::size_t line_count{};
    double height{};
};

// The paragraph scratch and the layout call with its retry. One thread.
class ParagraphWrapper final {
public:
    // Reserves the scratch for texts up to `text_capacity` bytes.
    explicit ParagraphWrapper(std::size_t text_capacity);

    // Lays `text` out in `style` into `lines` (resized only when a layout
    // needs more records than it has), through `provider` (null: the
    // renderer-free estimates). A refused request leaves zero lines.
    LaidOutParagraph lay_out(gui_forms::TextMetricsProvider* provider, std::string_view text,
                             const gui_forms::ParagraphStyle& style, std::vector<gui_forms::ParagraphLine>& lines);

    [[nodiscard]] TextLayoutCounters counters() const noexcept { return counters_; }
    // The scratch's bytes at capacity.
    [[nodiscard]] std::size_t storage_bytes() const noexcept { return storage_bytes_; }

private:
    gui_forms::ParagraphBuffers buffers_{};
    std::size_t per_byte_capacity_{};
    std::size_t storage_bytes_{};
    TextLayoutCounters counters_{};
};

// The fonts a row is set in and their line boxes (paragraph_line_box),
// resolved once per theme and text engine (RowFonts::generation changes
// when any of them does).
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

// The body paragraph's style: preserved line ends, long words (URLs) broken
// between clusters, verified fit (the lines Painter draws).
[[nodiscard]] gui_forms::ParagraphStyle body_style(const RowFonts& fonts, double text_width) noexcept;

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
    // Line records at capacity; the first line_count are this layout's.
    std::vector<gui_forms::ParagraphLine> lines{};
    std::size_t line_count{};
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
// (text_capacity bytes of text, reserved_lines line records), so steady
// scrolling refills slots in place without allocating. One thread.
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
