#include "profile_ink.hpp"

#include <cinttypes>
#include <cstdio>

namespace flame_graph {

namespace gf = gui_forms;

namespace {

void store(FormattedNumber& out, int written) noexcept {
    if (written < 0) {
        out.length = 0U;
        return;
    }
    const std::size_t size = static_cast<std::size_t>(written);
    out.length = size < out.storage.size() ? size : out.storage.size() - 1U;
}

} // namespace

gf::Color frame_color(std::string_view name) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char byte : name) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    // flamegraph.pl "hot": red 205-254, green 0-229, blue 0-54, green
    // and blue from separate hash bits.
    const std::uint8_t red = static_cast<std::uint8_t>(205U + (hash % 50U));
    const std::uint8_t green = static_cast<std::uint8_t>(60U + ((hash >> 16U) % 170U));
    const std::uint8_t blue = static_cast<std::uint8_t>((hash >> 32U) % 55U);
    return gf::Color::rgba(red, green, blue);
}

void fill_frame_colors(const StringTable& names, std::vector<gf::Color>& out) {
    out.resize(names.size());
    for (std::size_t index = 0U; index < names.size(); ++index) {
        out[index] = frame_color(names.text(NameId{static_cast<std::uint32_t>(index)}));
    }
}

void format_weight(std::int64_t weight, TraceUnit unit, FormattedNumber& out) noexcept {
    if (unit == TraceUnit::samples) {
        store(out, std::snprintf(out.storage.data(), out.storage.size(), "%" PRId64 " samples", weight));
        return;
    }
    const double value = static_cast<double>(weight);
    const double magnitude = value < 0.0 ? -value : value;
    int written = 0;
    if (magnitude >= 1.0e9) {
        written = std::snprintf(out.storage.data(), out.storage.size(), "%.3g s", value / 1.0e9);
    } else if (magnitude >= 1.0e6) {
        written = std::snprintf(out.storage.data(), out.storage.size(), "%.3g ms", value / 1.0e6);
    } else if (magnitude >= 1.0e3) {
        written = std::snprintf(out.storage.data(), out.storage.size(), "%.3g us", value / 1.0e3);
    } else {
        written = std::snprintf(out.storage.data(), out.storage.size(), "%" PRId64 " ns", weight);
    }
    store(out, written);
}

void format_share(std::int64_t part, std::int64_t whole, FormattedNumber& out) noexcept {
    if (whole <= 0) {
        store(out, std::snprintf(out.storage.data(), out.storage.size(), "0%%"));
        return;
    }
    const double share = 100.0 * static_cast<double>(part) / static_cast<double>(whole);
    store(out, std::snprintf(out.storage.data(), out.storage.size(), "%.1f%%", share));
}

void format_count(std::uint64_t count, FormattedNumber& out) noexcept {
    store(out, std::snprintf(out.storage.data(), out.storage.size(), "%" PRIu64, count));
}

bool cut_label(std::string_view text, double full_width, double available, double ellipsis_width,
               std::string& out) {
    std::size_t scalars = 0U;
    for (const char byte : text) {
        if ((static_cast<unsigned char>(byte) & 0xc0U) != 0x80U) ++scalars;
    }
    if (scalars == 0U || full_width <= 0.0) return false;
    const double average = full_width / static_cast<double>(scalars);
    const double room = available - ellipsis_width;
    if (room <= 0.0) return false;
    const std::size_t fit = static_cast<std::size_t>(room / average);
    if (fit < 2U) return false;
    // The byte length of the first `fit` scalars.
    std::size_t seen = 0U;
    std::size_t length = 0U;
    while (length < text.size()) {
        const bool lead = (static_cast<unsigned char>(text[length]) & 0xc0U) != 0x80U;
        if (lead) {
            if (seen == fit) break;
            ++seen;
        }
        ++length;
    }
    out.assign(text.substr(0U, length));
    out.append(label_ellipsis);
    return true;
}

} // namespace flame_graph
