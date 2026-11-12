// The corpus generator: golden values (tools/corpus_reference.py computes
// them from the same definition), determinism, bounds, the timestamp closed
// form, the erase map and the overlay. No GUI.Forms window is involved.

#include "../src/corpus.hpp"
#include "test_support.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

using chat_stress::Corpus;
using chat_stress::CorpusOptions;
using chat_stress::MessageId;
using chat_stress::RowIndex;
using chat_stress::UserId;
using chat_stress_tests::require;

namespace {

struct GoldenMessage final {
    std::uint64_t index;
    std::uint32_t author;
    std::int64_t timestamp_ms;
    std::uint32_t length;
    std::uint32_t bytes;
    std::string_view head;
};

struct GoldenUser final {
    std::uint32_t user;
    std::string_view name;
    std::uint32_t hue;
    std::uint32_t pattern;
};

// python3 -B tools/corpus_reference.py --cpp (default seed, 5,000 users, latin).
constexpr std::array<GoldenMessage, 9> golden_messages{{
    {0U, 14U, 1577836800000, 347U, 347U, "Temjun fozoeth, bra qui gri, pav"},
    {1U, 1500U, 1577837058235, 991U, 991U, "Ismo linkor ethsa mi, bra moethi"},
    {2U, 1500U, 1577837063273, 834U, 834U, "Mirosdor is iskorvel korlinshi, "},
    {63U, 14U, 1577839748919, 490U, 490U, "Velsaancen grika nelpagri, brada"},
    {64U, 2022U, 1577839751439, 454U, 454U, "Vir pakatem mo, orzo lois, shimo"},
    {4095U, 3U, 1578188607007, 231U, 231U, "Linjun temsatas fo renviru shiro"},
    {4096U, 76U, 1578189228032, 349U, 349U, "Mishi pagri dai nelbratas ucen o"},
    {123456U, 13U, 1585640450151, 120U, 120U, "Quijunzo mi minel cenzo, shi, an"},
    {9999999U, 5U, 2272720143231, 607U, 607U, "Gribravel kor hal hallin mi, ren"},
}};

constexpr std::array<GoldenUser, 3> golden_users{{
    {1U, "Temgri Cenka", 309U, 20271U},
    {2U, "Issa Zokor", 332U, 17091U},
    {5000U, "Zotem Junmo", 103U, 12864U},
}};

void golden_values() {
    const Corpus corpus(CorpusOptions{});
    std::string text{};
    text.reserve(chat_stress::maximum_text_bytes);
    for (const GoldenMessage& golden : golden_messages) {
        require(corpus.generated_author(golden.index).value == golden.author, "golden author");
        require(corpus.generated_timestamp_ms(golden.index) == golden.timestamp_ms, "golden timestamp");
        require(corpus.generated_length(golden.index) == golden.length, "golden length");
        corpus.generated_text(golden.index, text);
        require(text.size() == golden.bytes, "golden text size");
        require(std::string_view(text).substr(0U, golden.head.size()) == golden.head, "golden text head");
    }
    for (const GoldenUser& golden : golden_users) {
        const chat_stress::UserRecord& user = corpus.user(UserId{golden.user});
        require(user.display_name == golden.name, "golden user name");
        require(user.hue == golden.hue, "golden user hue");
        require(user.pattern == golden.pattern, "golden user pattern");
    }
}

void determinism_and_bounds() {
    CorpusOptions options{};
    options.messages = 100'000U;
    options.mixed_scripts = true;
    const Corpus first(options);
    const Corpus second(options);
    std::string a{};
    std::string b{};
    a.reserve(chat_stress::maximum_text_bytes);
    b.reserve(chat_stress::maximum_text_bytes);
    const std::size_t capacity = a.capacity();
    for (std::uint64_t index = 0U; index < 5'000U; ++index) {
        first.generated_text(index, a);
        second.generated_text(index, b);
        require(a == b, "the same seed gives the same text");
        require(a.size() <= first.generated_length(index), "text within its length");
        require(a.size() + 4U >= chat_stress::minimum_text_bytes, "text near its minimum");
        require(first.generated_author(index) == second.generated_author(index), "same author");
        const std::uint32_t author = first.generated_author(index).value;
        require(author >= 1U && author <= options.users, "generated authors are 1..users");
    }
    require(a.capacity() == capacity, "a reserved buffer never grows");
    const CorpusOptions other{0x1234U, 100'000U, 5'000U, false};
    const Corpus third(other);
    third.generated_text(7U, b);
    first.generated_text(7U, a);
    require(a != b, "another seed gives other text");
    // The author walk is bounded by author_run_limit.
    const chat_stress::CorpusCounters before = first.counters();
    static_cast<void>(first.generated_author(99'999U));
    const chat_stress::CorpusCounters after = first.counters();
    require(after.author_walk_steps - before.author_walk_steps < chat_stress::author_run_limit, "bounded author walk");
}

void timestamps() {
    CorpusOptions options{};
    options.messages = 20'000U;
    const Corpus sequential(options);
    const Corpus random_access(options);
    std::int64_t previous = 0;
    for (std::uint64_t index = 0U; index < 20'000U; ++index) {
        const std::int64_t time = sequential.generated_timestamp_ms(index);
        require(time >= previous, "timestamps never go back");
        previous = time;
    }
    // Random access (no cursor help) gives the same values.
    const std::array<std::uint64_t, 5> probes{19'999U, 4'096U, 4'095U, 0U, 12'345U};
    for (const std::uint64_t index : probes) {
        require(random_access.generated_timestamp_ms(index) == sequential.generated_timestamp_ms(index),
                "random access equals sequential");
    }
    const chat_stress::CivilTime epoch = chat_stress::civil_time_of(chat_stress::corpus_epoch_ms);
    require(epoch.year == 2020 && epoch.month == 1U && epoch.day == 1U && epoch.weekday == 3U,
            "2020-01-01 was a Wednesday");
    const chat_stress::CivilTime leap = chat_stress::civil_time_of(1'582'934'400'000 + 3'723'000);
    require(leap.year == 2020 && leap.month == 2U && leap.day == 29U && leap.hour == 1U && leap.minute == 2U,
            "2020-02-29 01:02");
}

// Brute force: the surviving messages in order.
std::vector<std::uint64_t> surviving(std::uint64_t count, const std::vector<bool>& erased) {
    std::vector<std::uint64_t> rows{};
    for (std::uint64_t message = 0U; message < count; ++message) {
        if (!erased[message]) rows.push_back(message);
    }
    return rows;
}

void erase_map() {
    CorpusOptions options{};
    options.messages = 1'000U;
    Corpus corpus(options);
    std::vector<bool> erased(1'000U, false);
    struct EraseStep final {
        std::uint64_t row;
        std::uint64_t count;
    };
    const std::array<EraseStep, 6> steps{{{10U, 5U}, {10U, 1U}, {0U, 3U}, {500U, 100U}, {5U, 600U}, {0U, 1U}}};
    for (const EraseStep& step : steps) {
        std::vector<std::uint64_t> rows = surviving(1'000U, erased);
        for (std::uint64_t offset = 0U; offset < step.count; ++offset) erased[rows[step.row + offset]] = true;
        corpus.erase_rows(RowIndex{step.row}, step.count);
        rows = surviving(1'000U, erased);
        require(corpus.row_count() == rows.size(), "row count after erase");
        for (std::uint64_t row = 0U; row < rows.size(); ++row) {
            require(corpus.message_of_row(RowIndex{row}).value == rows[row], "message of row after erase");
            require(corpus.row_of_message(MessageId{rows[row]}).value == row, "row of message after erase");
        }
    }
    bool refused = false;
    try {
        corpus.erase_rows(RowIndex{corpus.row_count()}, 1U);
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused, "an erase past the end is refused");
}

void overlay() {
    CorpusOptions options{};
    options.messages = 100U;
    Corpus corpus(options);
    std::string text{};
    corpus.edit(MessageId{5U}, "changed", 42);
    corpus.message_text(MessageId{5U}, text);
    require(text == "changed" && corpus.edited(MessageId{5U}) && corpus.edit_revision(MessageId{5U}) == 1U,
            "an edit replaces the text");
    corpus.tombstone(MessageId{5U}, 43);
    corpus.message_text(MessageId{5U}, text);
    require(text == Corpus::deleted_text && corpus.deleted(MessageId{5U}), "a tombstone keeps the row");
    require(corpus.row_count() == 100U, "rows stay after a tombstone");
    const MessageId typed = corpus.append_local("hello", 7'000);
    require(typed.value == 100U && corpus.row_count() == 101U, "a typed message is appended");
    require(corpus.author(typed) == chat_stress::local_user && corpus.timestamp_ms(typed) == 7'000,
            "a typed message is the local user's, at its time");
    corpus.append_generated(10U);
    require(corpus.message_count() == 111U, "generated appends continue the sequence");
    const std::uint32_t author = corpus.author(MessageId{110U}).value;
    require(author >= 1U, "appended messages are generated");
}

} // namespace

int main() {
    try {
        golden_values();
        determinism_and_bounds();
        timestamps();
        erase_map();
        overlay();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "corpus_tests: %s\n", failure.what());
        return 1;
    }
    std::printf("corpus_tests: ok\n");
    return 0;
}
