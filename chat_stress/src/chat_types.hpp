#pragma once

// Identity and position wrappers for the chat stress app (CHAT_STRESS_SPEC
// section 3.2). A message's identity and a row's position are different
// quantities: once rows are erased they differ, so each has its own type
// and the conversions are named.

#include <cstdint>

namespace chat_stress {

// A message's identity: its index in the generated corpus, then the append
// sequence. Never used as a row position.
struct MessageId final {
    std::uint64_t value{};

    friend bool operator==(MessageId left, MessageId right) noexcept { return left.value == right.value; }
    friend bool operator!=(MessageId left, MessageId right) noexcept { return left.value != right.value; }
    friend bool operator<(MessageId left, MessageId right) noexcept { return left.value < right.value; }
};

// A user's identity in the user table. User 0 is the local user ("You");
// generated users are 1 to the user count.
struct UserId final {
    std::uint32_t value{};

    friend bool operator==(UserId left, UserId right) noexcept { return left.value == right.value; }
    friend bool operator!=(UserId left, UserId right) noexcept { return left.value != right.value; }
};

// A row's position in the chat log, from 0 to row_count() - 1. It equals
// the message's identity until rows are erased.
struct RowIndex final {
    std::uint64_t value{};

    friend bool operator==(RowIndex left, RowIndex right) noexcept { return left.value == right.value; }
    friend bool operator!=(RowIndex left, RowIndex right) noexcept { return left.value != right.value; }
    friend bool operator<(RowIndex left, RowIndex right) noexcept { return left.value < right.value; }
};

// The local user, who types into the compose box.
inline constexpr UserId local_user{0U};

// 2020-01-01T00:00:00Z in milliseconds since the Unix epoch.
inline constexpr std::int64_t corpus_epoch_ms = 1'577'836'800'000;

// The default seed, the bytes of "chatstre".
inline constexpr std::uint64_t default_seed = 0x6368617473747265ULL;

} // namespace chat_stress
