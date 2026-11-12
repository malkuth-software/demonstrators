// Row layout without a text engine: GUI.Forms' paragraph layout through the
// app's ParagraphWrapper with the renderer-free estimates (provider null),
// cluster breaks for long tokens, line storage growth, and the layout ring.

#include "../src/row_layout.hpp"
#include "test_support.hpp"

#include <gui_forms/typography/types/typography_types.hpp>

#include <cmath>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gf = gui_forms;
using chat_stress::LaidOutParagraph;
using chat_stress::MessageId;
using chat_stress::ParagraphWrapper;
using chat_stress::RowFonts;
using chat_stress::RowLayoutEntry;
using chat_stress::RowLayoutRing;
using chat_stress_tests::require;

namespace {

constexpr double width_tolerance = 1e-6;

RowFonts estimate_fonts() {
    RowFonts fonts{};
    fonts.body.role = gf::FontRole::content;
    fonts.body.size = 14.0;
    fonts.body_line = gf::paragraph_line_box(nullptr, fonts.body, std::nullopt);
    fonts.generation = 1U;
    return fonts;
}

double estimated_width(std::string_view text, gf::FontSpec font) {
    const gf::ResolvedTextLayout layout = gf::estimate_text_layout_utf8(text, font);
    return layout.logical_size.width;
}

std::string_view line_text(std::string_view text, const gf::ParagraphLine& line) {
    const std::size_t start = line.bytes.start.value();
    const std::size_t end = line.bytes.end.value();
    return text.substr(start, end - start);
}

// The lines cover the text in order: each starts where the previous one's
// next line starts, and the last ends the text.
void require_coverage(std::string_view text, const std::vector<gf::ParagraphLine>& lines, std::size_t count) {
    std::size_t next = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        const gf::ParagraphLine& line = lines[index];
        require(line.bytes.start.value() == next, "each line starts where the previous one left off");
        require(line.bytes.start.value() <= line.bytes.end.value(), "a line's range is ordered");
        require(line.bytes.end.value() <= line.next_line_start.value(), "hanging spaces follow the line");
        next = line.next_line_start.value();
    }
    require(next == text.size(), "the lines cover the text");
}

void lines_fit_and_cover() {
    const RowFonts fonts = estimate_fonts();
    ParagraphWrapper wrapper(4'000U);
    std::vector<gf::ParagraphLine> lines(64U);
    const std::string text = "Temjun fozoeth, bra qui gri, pavel dorshi an eth ul bracen dai fo gri hal is jun kor "
                             "lin mo nel or pa qui ros sa tem u vir zo. Kalo mi ren tas vel dor shi an eth.";
    const double width = 200.0;
    const gf::ParagraphStyle style = chat_stress::body_style(fonts, width);
    const LaidOutParagraph laid = wrapper.lay_out(nullptr, text, style, lines);
    require(laid.line_count > 1U, "a long paragraph wraps");
    require_coverage(text, lines, laid.line_count);
    for (std::size_t index = 0U; index < laid.line_count; ++index) {
        const std::string_view run = line_text(text, lines[index]);
        require(!run.empty() && run.back() != ' ', "trailing spaces hang outside the line");
        require(lines[index].width <= width + width_tolerance, "every line fits");
        require(std::fabs(estimated_width(run, fonts.body) - lines[index].width) <= width_tolerance,
                "a line's width is its run's measured width");
    }
    const double expected_height = static_cast<double>(laid.line_count) * fonts.body_line.height;
    require(std::fabs(laid.height - expected_height) <= width_tolerance, "the height is the line boxes");
    require(wrapper.counters().shaping_calls > 0U, "the layout's shaping is counted");
    require(wrapper.counters().storage_retries == 0U, "the reserved storage was enough");

    const LaidOutParagraph empty = wrapper.lay_out(nullptr, std::string_view{}, style, lines);
    require(empty.line_count == 0U && empty.height == 0.0, "an empty text has no lines");
}

void long_tokens_break_between_clusters() {
    const RowFonts fonts = estimate_fonts();
    ParagraphWrapper wrapper(4'000U);
    std::vector<gf::ParagraphLine> lines(64U);
    const std::string text = "see https://example.test/abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz now";
    const double width = 120.0;
    const LaidOutParagraph laid = wrapper.lay_out(nullptr, text, chat_stress::body_style(fonts, width), lines);
    require(laid.line_count > 2U, "the URL is broken across lines");
    require_coverage(text, lines, laid.line_count);
    require(wrapper.counters().overflowing_lines == 0U, "break_word makes every line fit");
    // A width below one character still makes progress, a cluster a line.
    const LaidOutParagraph tiny = wrapper.lay_out(nullptr, "abc", chat_stress::body_style(fonts, 0.5), lines);
    require(tiny.line_count == 3U, "a character per line at a tiny width");
}

void line_storage_grows_once() {
    const RowFonts fonts = estimate_fonts();
    ParagraphWrapper wrapper(4'000U);
    std::vector<gf::ParagraphLine> lines(2U);
    std::string text{};
    for (int word = 0; word < 40; ++word) text.append("word ");
    const LaidOutParagraph laid = wrapper.lay_out(nullptr, text, chat_stress::body_style(fonts, 60.0), lines);
    require(laid.line_count > 2U, "more lines than the slot held");
    require(lines.size() >= laid.line_count, "the slot's records grew to the layout");
    require(wrapper.counters().storage_retries == 1U, "one retry after growing");
    require_coverage(text, lines, laid.line_count);
}

void ring() {
    RowLayoutRing ring(4U, 1'000U);
    require(ring.capacity() == 4U, "ring capacity");
    const std::size_t bytes = ring.storage_bytes();
    require(bytes >= 4U * 1'000U, "every slot's text is reserved");
    RowLayoutEntry& slot = ring.slot(MessageId{6U});
    require(slot.lines.size() == RowLayoutRing::reserved_lines, "every slot's line records are reserved");
    require(&slot == &ring.slot(MessageId{2U}), "direct mapped: 6 and 2 share a slot of four");
    require(&slot != &ring.slot(MessageId{7U}), "consecutive messages do not collide");
    require(!ring.matches(slot, MessageId{6U}, MessageId{5U}, 300.0, 0U, 1U), "an empty slot misses");
    slot.valid = true;
    slot.message = MessageId{6U};
    slot.previous = MessageId{5U};
    slot.width = 300.0;
    slot.revision = 0U;
    slot.font_generation = 1U;
    slot.text.assign(900U, 'x');
    require(ring.matches(slot, MessageId{6U}, MessageId{5U}, 300.0, 0U, 1U), "the same layout hits");
    require(!ring.matches(slot, MessageId{6U}, MessageId{5U}, 301.0, 0U, 1U), "another width misses");
    require(!ring.matches(slot, MessageId{6U}, MessageId{4U}, 300.0, 0U, 1U), "another predecessor misses");
    require(!ring.matches(slot, MessageId{6U}, MessageId{5U}, 300.0, 1U, 1U), "an edit misses");
    require(ring.counters().hits == 1U && ring.counters().misses == 4U, "ring counters");
    require(ring.storage_bytes() == bytes, "refilling a slot within its reserve allocates nothing");
    ring.invalidate(MessageId{2U});
    require(slot.valid, "invalidating another message leaves the slot");
    ring.invalidate(MessageId{6U});
    require(!slot.valid, "invalidating the message clears the slot");
}

} // namespace

int main() {
    try {
        lines_fit_and_cover();
        long_tokens_break_between_clusters();
        line_storage_grows_once();
        ring();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "row_layout_tests: %s\n", failure.what());
        return 1;
    }
    std::printf("row_layout_tests: ok\n");
    return 0;
}
