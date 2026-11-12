#include "row_layout.hpp"

#include <span>
#include <stdexcept>

namespace chat_stress {

namespace gf = gui_forms;

namespace {

constexpr std::size_t scratch_bytes_per_byte = sizeof(gf::LineBreakOpportunity) + sizeof(double);
// One call, and one more after growing to what the first said it needs
// (storage of the stated sizes always succeeds).
constexpr int layout_attempts = 2;

} // namespace

// -- ParagraphWrapper -------------------------------------------------------------

ParagraphWrapper::ParagraphWrapper(std::size_t text_capacity) {
    buffers_.reserve(text_capacity, 0U);
    per_byte_capacity_ = text_capacity;
    storage_bytes_ = text_capacity * scratch_bytes_per_byte;
}

LaidOutParagraph ParagraphWrapper::lay_out(gf::TextMetricsProvider* provider, std::string_view text,
                                           const gf::ParagraphStyle& style, std::vector<gf::ParagraphLine>& lines) {
    ++counters_.paragraphs;
    LaidOutParagraph result{};
    for (int attempt = 0; attempt < layout_attempts; ++attempt) {
        // The scratch is the shared buffers'; the line records are the slot's.
        gf::ParagraphStorage storage = buffers_.storage();
        storage.lines = std::span<gf::ParagraphLine>(lines.data(), lines.size());
        const gf::ParagraphLayoutResult laid = gf::lay_out_paragraph(provider, text, style, storage);
        const gf::ParagraphMetrics* metrics = laid.metrics();
        if (metrics != nullptr) {
            result.line_count = (*metrics).line_count;
            result.height = (*metrics).height;
            counters_.lines += (*metrics).line_count;
            counters_.shaping_calls += (*metrics).shaping_calls;
            counters_.shaped_bytes += (*metrics).shaped_bytes;
            counters_.overflowing_lines += (*metrics).overflowing_lines;
            return result;
        }
        const gf::ParagraphStorageRequirement* required = laid.required_storage();
        if (required == nullptr) break;
        ++counters_.storage_retries;
        if ((*required).per_byte > per_byte_capacity_) {
            buffers_.reserve((*required).per_byte, 0U);
            per_byte_capacity_ = (*required).per_byte;
            storage_bytes_ = per_byte_capacity_ * scratch_bytes_per_byte;
        }
        if ((*required).lines > lines.size()) lines.resize((*required).lines);
    }
    ++counters_.refusals;
    return result;
}

gf::ParagraphStyle body_style(const RowFonts& fonts, double text_width) noexcept {
    gf::ParagraphStyle style{};
    style.font = fonts.body;
    style.line_box = fonts.body_line;
    style.width = text_width;
    style.white_space = gf::ParagraphWhiteSpace::preserve_line_ends;
    style.overflow_wrap = gf::ParagraphOverflowWrap::break_word;
    style.fit = gf::ParagraphFit::verified;
    style.alignment = gf::TextAlignment::start;
    style.direction = gf::TextDirection::left_to_right;
    return style;
}

double RowGeometry::text_width(double row_width) noexcept {
    double width = row_width - text_x() - margin_x;
    if (!(width >= 1.0)) width = 1.0;
    return width;
}

// -- RowLayoutRing ------------------------------------------------------------------

RowLayoutRing::RowLayoutRing(std::size_t capacity, std::size_t text_capacity) {
    if (capacity == 0U) throw std::invalid_argument("row layout ring: zero capacity");
    entries_.resize(capacity);
    for (RowLayoutEntry& entry : entries_) {
        entry.text.reserve(text_capacity);
        entry.lines.resize(reserved_lines);
    }
}

RowLayoutEntry& RowLayoutRing::slot(MessageId message) noexcept {
    const std::size_t index = static_cast<std::size_t>(message.value % entries_.size());
    return entries_[index];
}

bool RowLayoutRing::matches(const RowLayoutEntry& entry, MessageId message, MessageId previous, double width,
                            std::uint64_t revision, std::uint64_t font_generation) noexcept {
    const bool same = entry.valid && entry.message == message && entry.previous == previous &&
                      entry.width == width && entry.revision == revision && entry.font_generation == font_generation;
    if (same) {
        ++counters_.hits;
    } else {
        ++counters_.misses;
        if (entry.valid) ++counters_.replacements;
    }
    return same;
}

void RowLayoutRing::invalidate_all() noexcept {
    for (RowLayoutEntry& entry : entries_) entry.valid = false;
}

void RowLayoutRing::invalidate(MessageId message) noexcept {
    RowLayoutEntry& entry = slot(message);
    if (entry.message == message) entry.valid = false;
}

std::size_t RowLayoutRing::storage_bytes() const noexcept {
    std::size_t bytes = entries_.capacity() * sizeof(RowLayoutEntry);
    for (const RowLayoutEntry& entry : entries_) {
        bytes += entry.text.capacity();
        bytes += entry.lines.capacity() * sizeof(gf::ParagraphLine);
    }
    return bytes;
}

} // namespace chat_stress
