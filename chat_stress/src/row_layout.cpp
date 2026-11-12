#include "row_layout.hpp"

#include <optional>
#include <span>
#include <stdexcept>

namespace chat_stress {

namespace gf = gui_forms;

namespace {

constexpr unsigned char continuation_mask = 0xC0U;
constexpr unsigned char continuation_bits = 0x80U;

bool is_break(gf::LineBreakOpportunity opportunity) noexcept {
    const bool breaks =
        opportunity == gf::LineBreakOpportunity::allowed || opportunity == gf::LineBreakOpportunity::mandatory;
    return breaks;
}

bool is_space(char byte) noexcept {
    const bool space = byte == ' ' || byte == '\n' || byte == '\r' || byte == '\t';
    return space;
}

bool starts_character(std::string_view text, std::size_t position) noexcept {
    if (position >= text.size()) return true;
    const unsigned char byte = static_cast<unsigned char>(text[position]);
    const bool starts = (byte & continuation_mask) != continuation_bits;
    return starts;
}

// The end of [begin, end) with trailing spaces dropped, never before begin.
std::size_t trim_end(std::string_view text, std::size_t begin, std::size_t end) noexcept {
    std::size_t visible = end;
    while (visible > begin && is_space(text[visible - 1U])) --visible;
    return visible;
}

} // namespace

// -- TextWrapper ------------------------------------------------------------------

TextWrapper::TextWrapper(std::size_t capacity) { opportunities_.reserve(capacity); }

double TextWrapper::measure(const gf::SurfaceRoot* root, std::string_view text, gf::FontSpec font) {
    ++counters_.measure_calls;
    counters_.measured_bytes += text.size();
    if (root != nullptr) {
        const gf::TextExtent extent = (*root).measure_text_extent_utf8(text, font);
        return extent.logical_size.width;
    }
    const gf::ResolvedTextLayout estimate = gf::estimate_text_layout_utf8(text, font);
    return estimate.logical_size.width;
}

std::size_t TextWrapper::break_inside(const gf::SurfaceRoot* root, std::string_view text, std::size_t begin,
                                      std::size_t limit, gf::FontSpec font, double width) {
    // The first character always goes on the line.
    std::size_t first_end = begin + 1U;
    while (first_end < limit && !starts_character(text, first_end)) ++first_end;
    std::size_t low = first_end;   // fits (by rule)
    std::size_t high = limit;      // may not fit
    // Binary search over byte positions, snapped to character starts.
    while (low < high) {
        std::size_t middle = low + (high - low + 1U) / 2U;
        while (middle > low && !starts_character(text, middle)) --middle;
        if (middle == low) break;
        const double run = measure(root, text.substr(begin, middle - begin), font);
        if (run <= width) {
            low = middle;
        } else {
            high = middle - 1U;
            while (high > low && !starts_character(text, high)) --high;
        }
    }
    ++counters_.inside_word_breaks;
    return low;
}

std::size_t TextWrapper::wrap(const gf::SurfaceRoot* root, std::string_view text, gf::FontSpec font, double width,
                              std::vector<WrappedLine>& lines) {
    lines.clear();
    ++counters_.paragraphs;
    const std::size_t size = text.size();
    if (size == 0U) return 0U;
    opportunities_.resize(size);
    const std::span<gf::LineBreakOpportunity> opportunities(opportunities_.data(), size);
    gf::TextResolutionStatus status = gf::TextResolutionStatus::invalid_request;
    if (root != nullptr) {
        status = (*root).fill_line_break_opportunities_utf8(text, opportunities);
    } else {
        status = gf::estimate_line_break_opportunities_utf8(text, opportunities);
    }
    if (status == gf::TextResolutionStatus::invalid_request) {
        // Invalid UTF-8 (the corpus never makes it; typed text is checked by
        // the text box): one unbreakable run, broken inside by width.
        for (std::size_t byte = 0U; byte + 1U < size; ++byte) {
            opportunities_[byte] = gf::LineBreakOpportunity::prohibited;
        }
        opportunities_[size - 1U] = gf::LineBreakOpportunity::mandatory;
    }

    std::size_t line_start = 0U;
    while (line_start < size) {
        while (line_start < size && text[line_start] == ' ') ++line_start;
        if (line_start >= size) break;
        std::size_t fit_end = 0U;
        std::size_t fit_visible = 0U;
        double fit_width = 0.0;
        std::size_t first_candidate = size;
        for (std::size_t byte = line_start; byte < size; ++byte) {
            const gf::LineBreakOpportunity opportunity = opportunities_[byte];
            if (!is_break(opportunity)) continue;
            const std::size_t candidate_end = byte + 1U;
            if (first_candidate == size) first_candidate = candidate_end;
            const std::size_t visible = trim_end(text, line_start, candidate_end);
            const double run = measure(root, text.substr(line_start, visible - line_start), font);
            if (run > width) break;
            fit_end = candidate_end;
            fit_visible = visible;
            fit_width = run;
            if (opportunity == gf::LineBreakOpportunity::mandatory) break;
        }
        WrappedLine line{};
        if (fit_end == 0U) {
            // The first run alone is wider than the line: break inside it.
            const std::size_t limit = trim_end(text, line_start, first_candidate);
            std::size_t end = limit;
            if (limit > line_start) end = break_inside(root, text, line_start, limit, font, width);
            if (end <= line_start) end = first_candidate;   // a run of spaces only
            line.begin = static_cast<std::uint32_t>(line_start);
            line.end = static_cast<std::uint32_t>(end);
            line.width = measure(root, text.substr(line_start, end - line_start), font);
            line_start = end;
        } else {
            line.begin = static_cast<std::uint32_t>(line_start);
            line.end = static_cast<std::uint32_t>(fit_visible);
            line.width = fit_width;
            line_start = fit_end;
        }
        lines.push_back(line);
        ++counters_.lines;
    }
    return lines.size();
}

std::size_t TextWrapper::storage_bytes() const noexcept {
    const std::size_t bytes = opportunities_.capacity() * sizeof(gf::LineBreakOpportunity);
    return bytes;
}

gf::LineBoxMetrics font_line_box(const gf::SurfaceRoot* root, gf::FontSpec font) {
    constexpr std::string_view probe = "Hg";
    if (root != nullptr) {
        const gf::ResolvedTextLayout layout = (*root).resolve_text_layout_utf8(probe, font);
        const gf::LineBoxMetrics metrics = gf::line_box_metrics(layout, std::nullopt);
        return metrics;
    }
    const gf::ResolvedTextLayout estimate = gf::estimate_text_layout_utf8(probe, font);
    const gf::LineBoxMetrics metrics = gf::line_box_metrics(estimate, std::nullopt);
    return metrics;
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
        entry.lines.reserve(reserved_lines);
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
        bytes += entry.lines.capacity() * sizeof(WrappedLine);
    }
    return bytes;
}

} // namespace chat_stress
