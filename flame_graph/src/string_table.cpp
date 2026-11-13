#include "string_table.hpp"

#include <algorithm>
#include <limits>

namespace flame_graph {

namespace {

constexpr std::size_t initial_slots = 1024U;

// The longest prefix of `text` of at most `limit` bytes that does not end
// inside a UTF-8 sequence.
std::string_view utf8_prefix(std::string_view text, std::size_t limit) noexcept {
    if (text.size() <= limit) return text;
    std::size_t length = limit;
    while (length > 0U && (static_cast<unsigned char>(text[length]) & 0xc0U) == 0x80U) --length;
    return text.substr(0U, length);
}

} // namespace

StringTable::StringTable() {
    slots_.assign(initial_slots, 0U);
}

void StringTable::reserve(std::size_t names, std::size_t bytes) {
    const std::size_t capped = std::min(names, maximum_names);
    spans_.reserve(capped);
    arena_.reserve(bytes);
    // Kept at most half full: the power of two at or above twice the names.
    std::size_t wanted = initial_slots;
    while (wanted < capped * 2U) wanted *= 2U;
    if (wanted > slots_.size()) {
        while (slots_.size() < wanted) grow_slots();
    }
}

std::uint64_t StringTable::hash_of(std::string_view text) noexcept {
    // FNV-1a, 64 bits.
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char byte : text) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::size_t StringTable::slot_of(std::string_view text, std::uint64_t hash) const noexcept {
    const std::size_t mask = slots_.size() - 1U;
    std::size_t slot = static_cast<std::size_t>(hash) & mask;
    // Linear probing; the table is at most half full, so an empty slot is
    // always reached.
    while (true) {
        ++statistics_.probes;
        const std::uint32_t entry = slots_[slot];
        if (entry == 0U) return slot;
        const NameSpan& span = spans_[entry - 1U];
        if (span.hash == hash && span.length == text.size()) {
            const std::string_view held(arena_.data() + span.offset, span.length);
            if (held == text) return slot;
        }
        slot = (slot + 1U) & mask;
    }
}

void StringTable::grow_slots() {
    std::vector<std::uint32_t> grown(slots_.size() * 2U, 0U);
    const std::size_t mask = grown.size() - 1U;
    for (const std::uint32_t entry : slots_) {
        if (entry == 0U) continue;
        std::size_t slot = static_cast<std::size_t>(spans_[entry - 1U].hash) & mask;
        while (grown[slot] != 0U) slot = (slot + 1U) & mask;
        grown[slot] = entry;
    }
    slots_.swap(grown);
    ++statistics_.growths;
}

std::optional<NameId> StringTable::intern(std::string_view text) {
    const std::string_view kept = utf8_prefix(text, maximum_name_bytes);
    const std::uint64_t hash = hash_of(kept);
    ++statistics_.lookups;
    std::size_t slot = slot_of(kept, hash);
    if (slots_[slot] != 0U) return NameId{slots_[slot] - 1U};
    // Offsets are 32-bit: the arena stays under 4 GiB.
    const std::size_t arena_limit = std::numeric_limits<std::uint32_t>::max();
    if (spans_.size() >= maximum_names || kept.size() > arena_limit - arena_.size()) return std::nullopt;
    // Grown before inserting so the table stays at most half full.
    if ((spans_.size() + 1U) * 2U > slots_.size()) {
        grow_slots();
        slot = slot_of(kept, hash);
    }
    NameSpan span{};
    span.offset = static_cast<std::uint32_t>(arena_.size());
    span.length = static_cast<std::uint32_t>(kept.size());
    span.hash = hash;
    arena_.append(kept);
    spans_.push_back(span);
    const std::uint32_t index = static_cast<std::uint32_t>(spans_.size() - 1U);
    slots_[slot] = index + 1U;
    ++statistics_.interned;
    return NameId{index};
}

std::optional<NameId> StringTable::find(std::string_view text) const noexcept {
    const std::string_view kept = utf8_prefix(text, maximum_name_bytes);
    ++statistics_.lookups;
    const std::size_t slot = slot_of(kept, hash_of(kept));
    if (slots_[slot] == 0U) return std::nullopt;
    return NameId{slots_[slot] - 1U};
}

std::string_view StringTable::text(NameId name) const& noexcept {
    if (name.value >= spans_.size()) return std::string_view();
    const NameSpan& span = spans_[name.value];
    const std::string_view result(arena_.data() + span.offset, span.length);
    return result;
}

void StringTable::clear() noexcept {
    arena_.clear();
    spans_.clear();
    std::fill(slots_.begin(), slots_.end(), 0U);
}

} // namespace flame_graph
