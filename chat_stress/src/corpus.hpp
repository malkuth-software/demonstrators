#pragma once

// The chat corpus (CHAT_STRESS_SPEC section 3): every generated message is a
// pure function of (seed, index), so ten million messages hold no text. The
// resident state is the user table, the timestamp block table (one entry per
// 4,096 messages), the edit overlay, the append count and the erased ranges.
//
// The generator is first-party and fixed: changing any constant here changes
// the golden values in tests/corpus_tests.cpp and tools/corpus_reference.py,
// which mirrors it line for line.
//
// One thread. The timestamp and author caches are mutable (logically const
// reads that remember work); nothing here is shared between threads.

#include "chat_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chat_stress {

// -- the hash ---------------------------------------------------------------

// SplitMix64's finalizer: a bijective 64-bit mix.
[[nodiscard]] std::uint64_t mix64(std::uint64_t value) noexcept;

// The fields a message draws from the hash; each is independent of the
// others, so any field of any message costs O(1) hash calls (the timestamp
// excepted, see Corpus::timestamp_ms).
enum class CorpusField : std::uint64_t {
    author_flag = 1U,
    author_pick = 2U,
    weight = 3U,
    length = 4U,
    script = 5U,
    url = 6U,
    block_span = 7U,
    user_hue = 9U,
    user_pattern = 10U,
    // The text and name streams draw text_stream + k for k = 0, 1, 2, ...
    text_stream = 64U,
    name_stream = 128U,
};

// The hash of one field of one message: mix64(seed ^ mix64(index ^ (field *
// 0xD1B54A32D192ED03))), all arithmetic modulo 2^64.
[[nodiscard]] std::uint64_t field_hash(std::uint64_t seed, std::uint64_t index, std::uint64_t field) noexcept;

// The seed the user table draws from: the corpus seed with "users" in its
// top bytes, so a user's fields never repeat a message's.
[[nodiscard]] std::uint64_t user_seed(std::uint64_t seed) noexcept;

// -- configuration ----------------------------------------------------------

struct CorpusOptions final {
    std::uint64_t seed{default_seed};
    std::uint64_t messages{10'000'000U};
    std::uint32_t users{5'000U};
    // 15 % of messages mix Greek, Cyrillic, CJK and Arabic words (section 3.2).
    bool mixed_scripts{};
};

// Bounds the generator promises.
inline constexpr std::uint64_t maximum_messages = 1'000'000'000U;
inline constexpr std::uint32_t maximum_users = 65'000U;
inline constexpr std::uint64_t timestamp_block_messages = 4'096U;
inline constexpr std::uint16_t minimum_text_bytes = 100U;
inline constexpr std::uint16_t maximum_text_bytes = 1'000U;
// An author repeats for at most this many messages before a new draw is
// forced, which bounds the walk back that finds a message's author.
inline constexpr std::uint64_t author_run_limit = 64U;
// The longest text the overlay holds for one message (compose box input).
inline constexpr std::size_t maximum_overlay_text_bytes = 4'000U;
// The most overlay records (edits, deletions, typed messages) the workloads
// make; more is refused.
inline constexpr std::size_t maximum_overlay_records = 200'000U;

// -- users ------------------------------------------------------------------

struct UserRecord final {
    std::string display_name{};
    // The first letter of each word of the name, ASCII upper case.
    char initials[3]{};
    std::uint16_t hue{};
    // 15 bits: the 5 x 5 avatar pattern's left three columns.
    std::uint16_t pattern{};
};

// -- counters ---------------------------------------------------------------

// Work counters (structured instrumentation, section 6). They only grow.
struct CorpusCounters final {
    std::uint64_t timestamp_hash_calls{};
    std::uint64_t timestamp_block_totals{};
    std::uint64_t author_walk_steps{};
    std::uint64_t texts_generated{};
    std::uint64_t text_bytes_generated{};
    std::uint64_t overlay_lookups{};
};

// -- the corpus -------------------------------------------------------------

class Corpus final {
public:
    // Builds the user table and the timestamp block table: O(users) and
    // O(messages / 4096) work, nothing per message. Throws
    // std::invalid_argument for a message count above maximum_messages or a
    // user count of zero or above maximum_users.
    explicit Corpus(CorpusOptions options);

    [[nodiscard]] std::uint64_t seed() const noexcept { return options_.seed; }
    [[nodiscard]] const CorpusOptions& options() const noexcept { return options_; }
    // Messages generated at start (N).
    [[nodiscard]] std::uint64_t generated_count() const noexcept { return options_.messages; }
    // Messages that exist: N plus the appended ones, erased ones included.
    [[nodiscard]] std::uint64_t message_count() const noexcept { return message_count_; }
    // Rows: messages less the erased ones.
    [[nodiscard]] std::uint64_t row_count() const noexcept { return message_count_ - erased_total_; }

    // The message at a row. Precondition: row < row_count() (checked by the
    // callers that take rows from the view).
    [[nodiscard]] MessageId message_of_row(RowIndex row) const noexcept;
    // The row showing a message, or the next surviving row when it was
    // erased; row_count() past the end.
    [[nodiscard]] RowIndex row_of_message(MessageId message) const noexcept;

    // -- fields (overlay first, then the generator) --------------------------
    [[nodiscard]] UserId author(MessageId message) const;
    [[nodiscard]] std::int64_t timestamp_ms(MessageId message) const;
    // Writes the message's text into `text`, replacing its contents and
    // reusing its capacity: at most maximum_text_bytes bytes for a generated
    // message, so a buffer reserved to that never allocates.
    void message_text(MessageId message, std::string& text) const;
    [[nodiscard]] bool edited(MessageId message) const;
    [[nodiscard]] bool deleted(MessageId message) const;
    // Zero for a message never edited; then one more per edit. Layout caches
    // key on it.
    [[nodiscard]] std::uint64_t edit_revision(MessageId message) const;
    [[nodiscard]] const UserRecord& user(UserId user) const;
    [[nodiscard]] std::uint32_t user_count() const noexcept { return options_.users; }

    // -- generator, without the overlay (golden-tested) -----------------------
    [[nodiscard]] UserId generated_author(std::uint64_t index) const;
    [[nodiscard]] std::int64_t generated_timestamp_ms(std::uint64_t index) const;
    [[nodiscard]] std::uint16_t generated_length(std::uint64_t index) const noexcept;
    void generated_text(std::uint64_t index, std::string& text) const;

    // -- mutations ------------------------------------------------------------
    // Appends `count` generated messages (the generator at index >= N).
    // Throws std::length_error past maximum_messages, appending nothing.
    void append_generated(std::uint64_t count);
    // Appends a message typed by the local user. The text is cut to
    // maximum_overlay_text_bytes at a character boundary. Throws
    // std::length_error when the overlay is full, appending nothing.
    MessageId append_local(std::string_view text, std::int64_t now_ms);
    // Replaces a message's text (an edit). Same limits as append_local.
    void edit(MessageId message, std::string_view text, std::int64_t now_ms);
    // Replaces a message's text with the deletion notice; its row stays.
    void tombstone(MessageId message, std::int64_t now_ms);
    // Removes rows [first, first + count) for good (the moderation range
    // delete, workload 7.7). Throws std::out_of_range for a range past the
    // end, removing nothing.
    void erase_rows(RowIndex first, std::uint64_t count);

    [[nodiscard]] CorpusCounters counters() const noexcept { return counters_; }
    [[nodiscard]] std::size_t overlay_size() const noexcept { return overlay_.size(); }
    [[nodiscard]] std::size_t erased_range_count() const noexcept { return erased_.size(); }
    // Bytes of resident corpus state (capacities, not live lengths).
    [[nodiscard]] std::size_t resident_bytes() const noexcept;

    // The deletion notice a tombstone shows.
    static constexpr std::string_view deleted_text = "message deleted";

private:
    // One record of the edit overlay, sorted by message.
    struct EditRecord final {
        MessageId message{};
        std::string text{};
        std::int64_t edited_ms{};
        std::uint64_t revision{};
        bool deleted{};
        // A message typed by the local user: author and time are the record's.
        bool local{};
        std::int64_t local_timestamp_ms{};
    };
    // A run of erased messages, sorted, merged, disjoint.
    struct ErasedRange final {
        std::uint64_t first{};
        std::uint64_t count{};
        // Messages erased before this range.
        std::uint64_t erased_before{};
    };

    void build_users();
    void extend_block_table(std::uint64_t message_count);
    [[nodiscard]] std::uint64_t hash(std::uint64_t index, CorpusField field) const noexcept;
    [[nodiscard]] std::uint32_t message_weight(std::uint64_t index) const noexcept;
    [[nodiscard]] std::int64_t block_span_ms(std::uint64_t block) const noexcept;
    [[nodiscard]] std::uint32_t block_weight_total(std::uint64_t block) const;
    [[nodiscard]] const EditRecord* find_record(MessageId message) const;
    // The record for `message`, inserted when absent. Throws
    // std::length_error when the overlay is full.
    EditRecord& record_for(MessageId message);

    CorpusOptions options_{};
    std::uint64_t message_count_{};
    std::uint64_t erased_total_{};

    std::vector<UserRecord> users_{};
    // Zipf (exponent 1.1) cumulative thresholds over 2^24, one per
    // generated user, the last equal to 2^24.
    std::vector<std::uint32_t> author_thresholds_{};

    // Block b's first timestamp; one entry more than the blocks in use.
    std::vector<std::int64_t> block_start_ms_{};
    // Block b's weight total, zero until first needed (a total is never
    // zero: every weight is at least one).
    mutable std::vector<std::uint32_t> block_weight_total_{};
    // The last prefix computed, so sequential reads walk forward from it.
    mutable std::uint64_t cursor_index_{};
    mutable std::uint64_t cursor_prefix_{};
    mutable bool cursor_valid_{};

    std::vector<EditRecord> overlay_{};
    std::vector<ErasedRange> erased_{};
    mutable CorpusCounters counters_{};
};

// Civil date of a UTC time: year, month (1-12), day (1-31), and the time of
// day. Integer arithmetic only (Howard Hinnant's days-from-civil inverse).
struct CivilTime final {
    std::int64_t year{};
    std::uint32_t month{};
    std::uint32_t day{};
    std::uint32_t hour{};
    std::uint32_t minute{};
    std::uint32_t weekday{};   // 0 = Sunday
};
[[nodiscard]] CivilTime civil_time_of(std::int64_t utc_ms) noexcept;
// The UTC day number of a time (days since 1970-01-01, floor).
[[nodiscard]] std::int64_t utc_day_of(std::int64_t utc_ms) noexcept;

} // namespace chat_stress
