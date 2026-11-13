#pragma once

// The viewer's colours and number formats, shared by the three views.
//
// Frame colours follow flamegraph.pl's "hot" palette: a warm colour chosen
// by the name's hash, so a function keeps its colour across views and
// loads. The views paint custom content, and a consumer control cannot read
// theme roles for it (GAPS.md F7), so every colour here is a constant.
//
// Formats write into caller storage (a fixed array) so painting allocates
// nothing.

#include "string_table.hpp"
#include "trace_model.hpp"

#include <gui_forms/types/paint_types/paint_types.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace flame_graph {

inline constexpr gui_forms::Color view_background = gui_forms::Color::rgba(250, 250, 248);
inline constexpr gui_forms::Color lane_header_fill = gui_forms::Color::rgba(232, 232, 228);
inline constexpr gui_forms::Color ruler_fill = gui_forms::Color::rgba(240, 240, 236);
inline constexpr gui_forms::Color rule_ink = gui_forms::Color::rgba(200, 200, 196);
inline constexpr gui_forms::Color label_ink = gui_forms::Color::rgba(28, 28, 26);
inline constexpr gui_forms::Color quiet_ink = gui_forms::Color::rgba(110, 110, 106);
inline constexpr gui_forms::Color search_fill = gui_forms::Color::rgba(230, 60, 220);
inline constexpr gui_forms::Color dimmed_fill = gui_forms::Color::rgba(222, 222, 218);
inline constexpr gui_forms::Color merged_fill = gui_forms::Color::rgba(186, 150, 120);
inline constexpr gui_forms::Color selection_ink = gui_forms::Color::rgba(20, 60, 200);
inline constexpr gui_forms::Color related_ink = gui_forms::Color::rgba(40, 120, 230);

// The hot colour of one name.
[[nodiscard]] gui_forms::Color frame_color(std::string_view name) noexcept;
// One colour per name of `names`, into `out` (resized; reserved by the
// caller across loads).
void fill_frame_colors(const StringTable& names, std::vector<gui_forms::Color>& out);

// Formatted text in caller storage: `text` views `storage`.
struct FormattedNumber final {
    std::array<char, 40> storage{};
    std::size_t length{};
    [[nodiscard]] std::string_view text() const& noexcept { return std::string_view(storage.data(), length); }
    std::string_view text() const&& = delete;
};

// A weight in the trace's unit: "1.25 ms", "830 us", "12 ns" for
// nanoseconds; "1234 samples" for samples.
void format_weight(std::int64_t weight, TraceUnit unit, FormattedNumber& out) noexcept;
// A share of a whole: "12.5%" ("0%" when the whole is not positive).
void format_share(std::int64_t part, std::int64_t whole, FormattedNumber& out) noexcept;
void format_count(std::uint64_t count, FormattedNumber& out) noexcept;

// The horizontal ellipsis, U+2026, appended to a cut label.
inline constexpr std::string_view label_ellipsis = "\xe2\x80\xa6";

// A label cut to fit `available` units: the prefix of `text` estimated to
// fit before an ellipsis of `ellipsis_width`, from the text's measured
// `full_width` spread evenly over its scalars (the Painter has no elided
// text, GAPS.md F2, so this estimate can overrun by part of a glyph; the
// caller keeps a margin). Into `out`; false when fewer than two scalars
// fit (draw nothing).
bool cut_label(std::string_view text, double full_width, double available, double ellipsis_width,
               std::string& out);

} // namespace flame_graph
