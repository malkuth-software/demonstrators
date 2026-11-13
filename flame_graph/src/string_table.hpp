#pragma once

// Interned frame names: each distinct text gets one NameId, its bytes kept
// once in an arena. A trace names the same functions millions of times, so
// the loaders intern every name as they read it and the model, the views
// and the table carry 32-bit identities only.
//
// Storage: one byte arena (`arena_`) and one span record per name, so a
// name's bytes stay at a fixed offset while the arena grows (views returned
// by text() are invalidated by growth, offsets are not). Lookup is an open
// addressing table of name indices plus one (0 is empty), at most half
// full, doubled when it would pass that.
//
// Ownership: the table owns everything it holds; text() returns a borrow
// valid until the next intern or clear. One thread at a time.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace flame_graph {

struct NameId final {
    std::uint32_t value{};
    friend bool operator==(NameId left, NameId right) noexcept { return left.value == right.value; }
    friend bool operator!=(NameId left, NameId right) noexcept { return left.value != right.value; }
};

struct StringTableStatistics final {
    std::uint64_t interned{};
    std::uint64_t lookups{};
    std::uint64_t probes{};
    std::uint64_t growths{};
};

class StringTable final {
public:
    // The longest name kept; longer texts are cut at a UTF-8 boundary.
    static constexpr std::size_t maximum_name_bytes = 4096U;
    // The most names; intern refuses past it (the loaders report it).
    static constexpr std::size_t maximum_names = 16U * 1024U * 1024U;

    StringTable();
    StringTable(const StringTable&) = delete;
    StringTable& operator=(const StringTable&) = delete;

    // Room for `names` names of `bytes` bytes in all, so that loading
    // allocates once per array.
    void reserve(std::size_t names, std::size_t bytes);
    // The text's identity, adding it when new; none when the table holds
    // maximum_names already or the arena would pass 4 GiB.
    [[nodiscard]] std::optional<NameId> intern(std::string_view text);
    [[nodiscard]] std::optional<NameId> find(std::string_view text) const noexcept;
    // The name's bytes; empty for an identity this table did not give.
    [[nodiscard]] std::string_view text(NameId name) const& noexcept;
    std::string_view text(NameId name) const&& = delete;
    [[nodiscard]] std::size_t size() const noexcept { return spans_.size(); }
    [[nodiscard]] std::size_t arena_bytes() const noexcept { return arena_.size(); }
    [[nodiscard]] const StringTableStatistics& statistics() const noexcept { return statistics_; }
    // Forgets every name, keeping the storage.
    void clear() noexcept;

private:
    struct NameSpan final {
        std::uint32_t offset{};
        std::uint32_t length{};
        std::uint64_t hash{};
    };

    [[nodiscard]] static std::uint64_t hash_of(std::string_view text) noexcept;
    // The slot holding `text`, or the empty slot where it would go.
    [[nodiscard]] std::size_t slot_of(std::string_view text, std::uint64_t hash) const noexcept;
    void grow_slots();

    std::string arena_{};
    std::vector<NameSpan> spans_{};
    // Power-of-two length; each entry a name index plus one, 0 when empty.
    std::vector<std::uint32_t> slots_{};
    mutable StringTableStatistics statistics_{};
};

} // namespace flame_graph
