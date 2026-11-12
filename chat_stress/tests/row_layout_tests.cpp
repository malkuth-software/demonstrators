// Row layout without a text engine: the wrapper with the renderer-free
// estimate (SurfaceRoot null), inside-word breaking, and the layout ring.

#include "../src/row_layout.hpp"
#include "test_support.hpp"

#include <gui_forms/typography/types/typography_types.hpp>

#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace gf = gui_forms;
using chat_stress::MessageId;
using chat_stress::RowLayoutEntry;
using chat_stress::RowLayoutRing;
using chat_stress::TextWrapper;
using chat_stress::WrappedLine;
using chat_stress_tests::require;

namespace {

gf::FontSpec body_font() {
    gf::FontSpec font{};
    font.role = gf::FontRole::content;
    font.size = 14.0;
    return font;
}

double estimated_width(std::string_view text) {
    const gf::ResolvedTextLayout layout = gf::estimate_text_layout_utf8(text, body_font());
    return layout.logical_size.width;
}

void lines_fit_and_cover() {
    TextWrapper wrapper(4'000U);
    std::vector<WrappedLine> lines{};
    lines.reserve(64U);
    const std::string text = "Temjun fozoeth, bra qui gri, pavel dorshi an eth ul bracen dai fo gri hal is jun kor "
                             "lin mo nel or pa qui ros sa tem u vir zo. Kalo mi ren tas vel dor shi an eth.";
    const double width = 200.0;
    const std::size_t count = wrapper.wrap(nullptr, text, body_font(), width, lines);
    require(count > 1U && count == lines.size(), "a long paragraph wraps");
    std::size_t covered = 0U;
    for (const WrappedLine& line : lines) {
        require(line.begin < line.end, "no empty line");
        require(line.begin >= covered, "lines in order");
        const std::string_view run = std::string_view(text).substr(line.begin, line.end - line.begin);
        require(estimated_width(run) <= width + 1e-9, "every line fits");
        require(run.front() != ' ' && run.back() != ' ', "lines are trimmed");
        // Only spaces lie between consecutive lines.
        for (std::size_t byte = covered; byte < line.begin; ++byte) require(text[byte] == ' ', "gaps are spaces");
        covered = line.end;
    }
    require(covered == text.size(), "the lines cover the text");
    require(wrapper.wrap(nullptr, std::string_view{}, body_font(), width, lines) == 0U && lines.empty(),
            "an empty text has no lines");
}

void long_tokens_break_inside() {
    TextWrapper wrapper(4'000U);
    std::vector<WrappedLine> lines{};
    const std::string text = "see https://example.test/abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz now";
    const double width = 120.0;
    static_cast<void>(wrapper.wrap(nullptr, text, body_font(), width, lines));
    require(wrapper.counters().inside_word_breaks > 0U, "the URL breaks inside");
    std::size_t covered = 0U;
    for (const WrappedLine& line : lines) {
        const std::string_view run = std::string_view(text).substr(line.begin, line.end - line.begin);
        const bool single_character = line.end - line.begin == 1U;
        require(single_character || estimated_width(run) <= width + 1e-9, "inside breaks fit");
        covered = line.end;
    }
    require(covered == text.size(), "the broken token is covered");
    // A width below one character still makes progress, a character a line.
    static_cast<void>(wrapper.wrap(nullptr, "abc", body_font(), 0.5, lines));
    require(lines.size() == 3U, "a character per line at a tiny width");
}

void ring() {
    RowLayoutRing ring(4U, 1'000U);
    require(ring.capacity() == 4U, "ring capacity");
    const std::size_t bytes = ring.storage_bytes();
    require(bytes >= 4U * 1'000U, "every slot's text is reserved");
    RowLayoutEntry& slot = ring.slot(MessageId{6U});
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
        long_tokens_break_inside();
        ring();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "row_layout_tests: %s\n", failure.what());
        return 1;
    }
    std::printf("row_layout_tests: ok\n");
    return 0;
}
