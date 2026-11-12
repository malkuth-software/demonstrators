#include "corpus.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace chat_stress {

namespace {

constexpr std::uint64_t field_multiplier = 0xD1B54A32D192ED03ULL;
constexpr std::uint64_t user_seed_tag = 0x7573657273000000ULL;   // "users"
constexpr std::uint32_t author_scale = 1U << 24U;
constexpr std::uint64_t continuation_mask = 0xC0U;
constexpr std::uint64_t continuation_bits = 0x80U;

// First-party syllables (nothing transcribed from a corpus).
constexpr std::array<std::string_view, 32> latin_syllables{
    "ka", "lo", "mi", "ren", "tas", "vel", "dor", "shi", "an", "eth", "ul", "bra", "cen", "dai", "fo", "gri",
    "hal", "is", "jun", "kor", "lin", "mo", "nel", "or", "pa", "qui", "ros", "sa", "tem", "u", "vir", "zo"};

// Made-up syllables per script, for the mixed-script variant: Greek,
// Cyrillic, CJK and Arabic (right to left), eight each.
constexpr std::array<std::array<std::string_view, 8>, 4> foreign_syllables{{
    {"\xce\xba\xce\xb1", "\xce\xbb\xce\xbf", "\xce\xbc\xce\xb7", "\xcf\x81\xce\xb5",
     "\xcf\x84\xce\xb1", "\xce\xbd\xce\xb9", "\xcf\x83\xce\xbf", "\xcf\x86\xcf\x85"},
    {"\xd1\x80\xd0\xb0", "\xd0\xbd\xd0\xbe", "\xd0\xb2\xd0\xb8", "\xd0\xba\xd0\xb0",
     "\xd0\xbb\xd0\xbe", "\xd0\xbc\xd0\xb8", "\xd1\x82\xd1\x83", "\xd0\xb6\xd0\xb5"},
    {"\xe5\xb1\xb1", "\xe6\xb0\xb4", "\xe7\x81\xab", "\xe6\x9c\xa8",
     "\xe6\x97\xa5", "\xe6\x9c\x88", "\xe9\xa2\xa8", "\xe8\x8a\xb1"},
    {"\xd8\xb3\xd9\x84", "\xd8\xa7\xd9\x85", "\xd9\x83\xd8\xaa", "\xd8\xa7\xd8\xa8",
     "\xd9\x85\xd8\xb1", "\xd8\xad\xd8\xa8", "\xd9\x86\xd9\x88", "\xd8\xb1\xd9\x8a"},
}};

constexpr std::string_view url_prefix = "https://example.test/";
constexpr std::string_view url_alphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
constexpr std::uint64_t no_url_word = ~static_cast<std::uint64_t>(0U);

// A counter-based stream over one message's text fields: draw k is
// field_hash(seed, index, base + k).
struct HashStream final {
    std::uint64_t seed{};
    std::uint64_t index{};
    std::uint64_t base{};
    std::uint64_t drawn{};

    std::uint64_t next() noexcept {
        const std::uint64_t value = field_hash(seed, index, base + drawn);
        ++drawn;
        return value;
    }
};

char ascii_upper(char letter) noexcept {
    if (letter >= 'a' && letter <= 'z') {
        const char upper = static_cast<char>(letter - 'a' + 'A');
        return upper;
    }
    return letter;
}

void append_latin_word(HashStream& stream, bool capital, std::string& text) {
    const std::uint64_t shape = stream.next();
    std::uint64_t syllables = 1U + shape % 3U;
    if ((shape >> 8U) % 8U == 0U) ++syllables;
    for (std::uint64_t syllable = 0U; syllable < syllables; ++syllable) {
        const std::string_view piece = latin_syllables[stream.next() % latin_syllables.size()];
        const std::size_t first = text.size();
        text.append(piece);
        if (syllable == 0U && capital) text[first] = ascii_upper(text[first]);
    }
}

void append_url(HashStream& stream, std::string& text) {
    text.append(url_prefix);
    const std::uint64_t length = 40U + stream.next() % 51U;
    for (std::uint64_t character = 0U; character < length; ++character) {
        text.push_back(url_alphabet[stream.next() % url_alphabet.size()]);
    }
}

void append_foreign_word(HashStream& stream, std::string& text) {
    const std::uint64_t script = stream.next() % foreign_syllables.size();
    const std::uint64_t syllables = 1U + stream.next() % 3U;
    for (std::uint64_t syllable = 0U; syllable < syllables; ++syllable) {
        text.append(foreign_syllables[script][stream.next() % 8U]);
    }
}

// Cuts `text` to at most `limit` bytes at a character boundary, then drops
// trailing spaces.
void cut_text(std::string& text, std::size_t limit) {
    if (text.size() > limit) {
        std::size_t cut = limit;
        while (cut > 0U && (static_cast<unsigned char>(text[cut]) & continuation_mask) == continuation_bits) --cut;
        text.resize(cut);
    }
    while (!text.empty() && text.back() == ' ') text.pop_back();
}

} // namespace

std::uint64_t mix64(std::uint64_t value) noexcept {
    std::uint64_t z = value + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    const std::uint64_t mixed = z ^ (z >> 31U);
    return mixed;
}

std::uint64_t field_hash(std::uint64_t seed, std::uint64_t index, std::uint64_t field) noexcept {
    const std::uint64_t inner = mix64(index ^ (field * field_multiplier));
    const std::uint64_t outer = mix64(seed ^ inner);
    return outer;
}

std::uint64_t user_seed(std::uint64_t seed) noexcept {
    const std::uint64_t tagged = seed ^ user_seed_tag;
    return tagged;
}

// -- construction -------------------------------------------------------------

Corpus::Corpus(CorpusOptions options) : options_(options) {
    if (options_.messages > maximum_messages) throw std::invalid_argument("chat corpus: too many messages");
    if (options_.users == 0U || options_.users > maximum_users) {
        throw std::invalid_argument("chat corpus: user count out of range");
    }
    message_count_ = options_.messages;
    build_users();
    extend_block_table(message_count_);
}

void Corpus::build_users() {
    const std::uint64_t seed = user_seed(options_.seed);
    // User 0 is the local user; 1..users are generated.
    users_.resize(static_cast<std::size_t>(options_.users) + 1U);
    UserRecord& you = users_[0];
    you.display_name = "You";
    you.initials[0] = 'Y';
    you.hue = 210U;
    you.pattern = 0x5A5AU & 0x7FFFU;
    for (std::uint32_t user = 1U; user <= options_.users; ++user) {
        UserRecord& record = users_[user];
        HashStream stream{seed, user, static_cast<std::uint64_t>(CorpusField::name_stream), 0U};
        record.display_name.reserve(16U);
        for (std::uint32_t word = 0U; word < 2U; ++word) {
            if (word == 1U) record.display_name.push_back(' ');
            for (std::uint32_t syllable = 0U; syllable < 2U; ++syllable) {
                const std::string_view piece = latin_syllables[stream.next() % latin_syllables.size()];
                const std::size_t first = record.display_name.size();
                record.display_name.append(piece);
                if (syllable == 0U) {
                    record.display_name[first] = ascii_upper(record.display_name[first]);
                    record.initials[word] = record.display_name[first];
                }
            }
        }
        record.hue = static_cast<std::uint16_t>(field_hash(seed, user, static_cast<std::uint64_t>(CorpusField::user_hue)) % 360U);
        record.pattern =
            static_cast<std::uint16_t>(field_hash(seed, user, static_cast<std::uint64_t>(CorpusField::user_pattern)) & 0x7FFFU);
    }

    // Zipf weights 1 / (k + 1)^1.1, summed in order and quantised to 2^24
    // (floor(x + 0.5)), so a last-ulp difference in pow between hosts cannot
    // move a threshold unless it straddles a quantum (logged per the FMA and
    // libm policy if a golden test ever disagrees).
    author_thresholds_.resize(options_.users);
    std::vector<double> cumulative(options_.users);
    double total = 0.0;
    for (std::uint32_t rank = 0U; rank < options_.users; ++rank) {
        const double weight = 1.0 / std::pow(static_cast<double>(rank) + 1.0, 1.1);
        total = total + weight;
        cumulative[rank] = total;
    }
    for (std::uint32_t rank = 0U; rank < options_.users; ++rank) {
        const double share = cumulative[rank] / total;
        const double scaled = std::floor(share * static_cast<double>(author_scale) + 0.5);
        author_thresholds_[rank] = static_cast<std::uint32_t>(scaled);
    }
    author_thresholds_.back() = author_scale;
}

void Corpus::extend_block_table(std::uint64_t message_count) {
    // Entries for every block in use plus the end of the last one.
    const std::uint64_t blocks = message_count / timestamp_block_messages + 2U;
    if (block_start_ms_.empty()) block_start_ms_.push_back(corpus_epoch_ms);
    while (block_start_ms_.size() < blocks) {
        const std::uint64_t block = block_start_ms_.size() - 1U;
        const std::int64_t next = block_start_ms_.back() + block_span_ms(block);
        block_start_ms_.push_back(next);
    }
    block_weight_total_.resize(block_start_ms_.size(), 0U);
}

// -- generator ----------------------------------------------------------------

std::uint64_t Corpus::hash(std::uint64_t index, CorpusField field) const noexcept {
    const std::uint64_t value = field_hash(options_.seed, index, static_cast<std::uint64_t>(field));
    return value;
}

std::uint16_t Corpus::generated_length(std::uint64_t index) const noexcept {
    const std::uint64_t span = static_cast<std::uint64_t>(maximum_text_bytes - minimum_text_bytes) + 1U;
    const std::uint64_t length = minimum_text_bytes + hash(index, CorpusField::length) % span;
    return static_cast<std::uint16_t>(length);
}

UserId Corpus::generated_author(std::uint64_t index) const {
    // The author is drawn at the latest index j <= index whose flag says
    // "new author" (40 %, and every author_run_limit-th index), so the walk
    // back is at most author_run_limit - 1 steps.
    std::uint64_t draw = index;
    while (draw % author_run_limit != 0U && hash(draw, CorpusField::author_flag) % 100U < 60U) {
        --draw;
        ++counters_.author_walk_steps;
    }
    const std::uint32_t pick = static_cast<std::uint32_t>(hash(draw, CorpusField::author_pick) >> 40U);
    const std::vector<std::uint32_t>::const_iterator found =
        std::upper_bound(author_thresholds_.begin(), author_thresholds_.end(), pick);
    std::size_t rank = static_cast<std::size_t>(found - author_thresholds_.begin());
    if (rank >= author_thresholds_.size()) rank = author_thresholds_.size() - 1U;
    const UserId author{static_cast<std::uint32_t>(rank) + 1U};
    return author;
}

std::uint32_t Corpus::message_weight(std::uint64_t index) const noexcept {
    const std::uint64_t value = hash(index, CorpusField::weight);
    const std::uint64_t choice = value % 1000U;
    const std::uint64_t spread = value >> 10U;
    std::uint64_t weight = 0U;
    if (choice < 900U) {
        weight = 1U + spread % 20U;
    } else {
        weight = 200U + spread % 801U;
    }
    return static_cast<std::uint32_t>(weight);
}

std::int64_t Corpus::block_span_ms(std::uint64_t block) const noexcept {
    // 4,096 messages over 4096 * [20 s, 120 s].
    const std::uint64_t mean_gap = 20'000U + hash(block, CorpusField::block_span) % 100'001U;
    const std::int64_t span = static_cast<std::int64_t>(timestamp_block_messages * mean_gap);
    return span;
}

std::uint32_t Corpus::block_weight_total(std::uint64_t block) const {
    std::uint32_t& total = block_weight_total_[block];
    if (total == 0U) {
        const std::uint64_t first = block * timestamp_block_messages;
        std::uint32_t sum = 0U;
        for (std::uint64_t index = first; index < first + timestamp_block_messages; ++index) {
            sum += message_weight(index);
        }
        counters_.timestamp_hash_calls += timestamp_block_messages;
        ++counters_.timestamp_block_totals;
        total = sum;
    }
    return total;
}

std::int64_t Corpus::generated_timestamp_ms(std::uint64_t index) const {
    // Closed form (section 3.2): block b spans block_span_ms(b) from its
    // stored start; message i sits at the share of the block's weight total
    // that the weights before it in the block reach. Exact integer
    // arithmetic: span <= 4.9e8 and prefix <= 4.1e6, so the product fits.
    const std::uint64_t block = index / timestamp_block_messages;
    const std::uint64_t first = block * timestamp_block_messages;
    std::uint64_t walked_from = first;
    std::uint64_t prefix = 0U;
    if (cursor_valid_ && cursor_index_ >= first && cursor_index_ <= index) {
        walked_from = cursor_index_;
        prefix = cursor_prefix_;
    }
    for (std::uint64_t position = walked_from; position < index; ++position) {
        prefix += message_weight(position);
    }
    counters_.timestamp_hash_calls += index - walked_from;
    cursor_index_ = index;
    cursor_prefix_ = prefix;
    cursor_valid_ = true;
    const std::uint32_t total = block_weight_total(block);
    const std::int64_t span = block_span_ms(block);
    const std::int64_t offset = span * static_cast<std::int64_t>(prefix) / static_cast<std::int64_t>(total);
    const std::int64_t timestamp = block_start_ms_[block] + offset;
    return timestamp;
}

void Corpus::generated_text(std::uint64_t index, std::string& text) const {
    text.clear();
    const std::size_t target = generated_length(index);
    const bool mixed = options_.mixed_scripts && hash(index, CorpusField::script) % 100U < 15U;
    const bool has_url = hash(index, CorpusField::url) % 100U < 2U;
    HashStream stream{options_.seed, index, static_cast<std::uint64_t>(CorpusField::text_stream), 0U};
    std::uint64_t url_word = no_url_word;
    if (has_url) url_word = stream.next() % 12U;
    std::uint64_t sentence_words = 4U + stream.next() % 11U;
    std::uint64_t in_sentence = 0U;
    std::uint64_t word_number = 0U;
    bool capital = true;
    while (text.size() < target) {
        if (!text.empty()) text.push_back(' ');
        if (word_number == url_word) {
            append_url(stream, text);
        } else if (mixed && stream.next() % 100U < 25U) {
            append_foreign_word(stream, text);
        } else {
            append_latin_word(stream, capital, text);
        }
        capital = false;
        ++in_sentence;
        ++word_number;
        if (in_sentence >= sentence_words) {
            const std::uint64_t mark = stream.next() % 10U;
            char punctuation = '.';
            if (mark >= 7U) punctuation = '?';
            if (mark >= 9U) punctuation = '!';
            text.push_back(punctuation);
            capital = true;
            in_sentence = 0U;
            sentence_words = 4U + stream.next() % 11U;
        } else if (stream.next() % 10U == 0U) {
            text.push_back(',');
        }
    }
    cut_text(text, target);
    ++counters_.texts_generated;
    counters_.text_bytes_generated += text.size();
}

// -- rows -----------------------------------------------------------------------

MessageId Corpus::message_of_row(RowIndex row) const noexcept {
    // The last range whose surviving-messages-before count is <= row: every
    // erased message before it (and its own) lies before the row's message.
    std::size_t low = 0U;
    std::size_t high = erased_.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        const ErasedRange& range = erased_[middle];
        const std::uint64_t surviving_before = range.first - range.erased_before;
        if (surviving_before <= row.value) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    std::uint64_t message = row.value;
    if (low > 0U) {
        const ErasedRange& range = erased_[low - 1U];
        message = row.value + range.erased_before + range.count;
    }
    const MessageId id{message};
    return id;
}

RowIndex Corpus::row_of_message(MessageId message) const noexcept {
    std::uint64_t erased_before = 0U;
    std::uint64_t target = message.value;
    for (const ErasedRange& range : erased_) {
        if (range.first > target) break;
        if (target < range.first + range.count) target = range.first + range.count;
        erased_before = range.erased_before + range.count;
    }
    if (target >= message_count_) {
        const RowIndex end{row_count()};
        return end;
    }
    const RowIndex row{target - erased_before};
    return row;
}

void Corpus::erase_rows(RowIndex first, std::uint64_t count) {
    if (count == 0U) return;
    if (first.value >= row_count() || count > row_count() - first.value) {
        throw std::out_of_range("chat corpus: erased rows past the end");
    }
    const std::uint64_t first_message = message_of_row(first).value;
    const RowIndex last_row{first.value + count - 1U};
    const std::uint64_t end_message = message_of_row(last_row).value + 1U;
    // Ranges inside [first_message, end_message) are absorbed; ranges that
    // touch it are merged into it.
    std::vector<ErasedRange> merged{};
    merged.reserve(erased_.size() + 1U);
    ErasedRange fresh{first_message, end_message - first_message, 0U};
    bool placed = false;
    for (const ErasedRange& range : erased_) {
        const std::uint64_t range_end = range.first + range.count;
        if (range_end < fresh.first) {
            merged.push_back(range);
        } else if (range.first > fresh.first + fresh.count) {
            if (!placed) {
                merged.push_back(fresh);
                placed = true;
            }
            merged.push_back(range);
        } else {
            const std::uint64_t low = std::min(range.first, fresh.first);
            const std::uint64_t high = std::max(range_end, fresh.first + fresh.count);
            fresh.first = low;
            fresh.count = high - low;
        }
    }
    if (!placed) merged.push_back(fresh);
    std::uint64_t before = 0U;
    for (ErasedRange& range : merged) {
        range.erased_before = before;
        before += range.count;
    }
    erased_.swap(merged);
    erased_total_ = before;
}

// -- overlay ----------------------------------------------------------------------

const Corpus::EditRecord* Corpus::find_record(MessageId message) const {
    ++counters_.overlay_lookups;
    if (overlay_.empty()) return nullptr;
    std::size_t low = 0U;
    std::size_t high = overlay_.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (overlay_[middle].message < message) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low < overlay_.size() && overlay_[low].message == message) return &overlay_[low];
    return nullptr;
}

Corpus::EditRecord& Corpus::record_for(MessageId message) {
    std::size_t low = 0U;
    std::size_t high = overlay_.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (overlay_[middle].message < message) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low < overlay_.size() && overlay_[low].message == message) return overlay_[low];
    if (overlay_.size() >= maximum_overlay_records) throw std::length_error("chat corpus: overlay full");
    EditRecord fresh{};
    fresh.message = message;
    const std::vector<EditRecord>::iterator inserted =
        overlay_.insert(overlay_.begin() + static_cast<std::ptrdiff_t>(low), std::move(fresh));
    return *inserted;
}

UserId Corpus::author(MessageId message) const {
    const EditRecord* record = find_record(message);
    if (record != nullptr && (*record).local) return local_user;
    return generated_author(message.value);
}

std::int64_t Corpus::timestamp_ms(MessageId message) const {
    const EditRecord* record = find_record(message);
    if (record != nullptr && (*record).local) return (*record).local_timestamp_ms;
    return generated_timestamp_ms(message.value);
}

void Corpus::message_text(MessageId message, std::string& text) const {
    const EditRecord* record = find_record(message);
    if (record == nullptr) {
        generated_text(message.value, text);
        return;
    }
    if ((*record).deleted) {
        text.assign(deleted_text);
        return;
    }
    text.assign((*record).text);
}

bool Corpus::edited(MessageId message) const {
    const EditRecord* record = find_record(message);
    const bool changed = record != nullptr && (*record).revision > 0U && !(*record).local;
    return changed;
}

bool Corpus::deleted(MessageId message) const {
    const EditRecord* record = find_record(message);
    const bool gone = record != nullptr && (*record).deleted;
    return gone;
}

std::uint64_t Corpus::edit_revision(MessageId message) const {
    const EditRecord* record = find_record(message);
    if (record == nullptr) return 0U;
    return (*record).revision;
}

const UserRecord& Corpus::user(UserId user) const {
    if (user.value >= users_.size()) throw std::out_of_range("chat corpus: no such user");
    return users_[user.value];
}

void Corpus::append_generated(std::uint64_t count) {
    if (count > maximum_messages - message_count_) throw std::length_error("chat corpus: too many messages");
    extend_block_table(message_count_ + count);
    message_count_ += count;
}

MessageId Corpus::append_local(std::string_view text, std::int64_t now_ms) {
    if (message_count_ >= maximum_messages) throw std::length_error("chat corpus: too many messages");
    const MessageId message{message_count_};
    EditRecord& record = record_for(message);
    record.text.assign(text);
    cut_text(record.text, maximum_overlay_text_bytes);
    record.local = true;
    record.local_timestamp_ms = now_ms;
    record.edited_ms = now_ms;
    extend_block_table(message_count_ + 1U);
    ++message_count_;
    return message;
}

void Corpus::edit(MessageId message, std::string_view text, std::int64_t now_ms) {
    if (message.value >= message_count_) throw std::out_of_range("chat corpus: no such message");
    EditRecord& record = record_for(message);
    record.text.assign(text);
    cut_text(record.text, maximum_overlay_text_bytes);
    record.deleted = false;
    record.edited_ms = now_ms;
    ++record.revision;
}

void Corpus::tombstone(MessageId message, std::int64_t now_ms) {
    if (message.value >= message_count_) throw std::out_of_range("chat corpus: no such message");
    EditRecord& record = record_for(message);
    record.text.clear();
    record.deleted = true;
    record.edited_ms = now_ms;
    ++record.revision;
}

std::size_t Corpus::resident_bytes() const noexcept {
    std::size_t bytes = sizeof(Corpus);
    bytes += users_.capacity() * sizeof(UserRecord);
    for (const UserRecord& record : users_) {
        if (record.display_name.capacity() > 15U) bytes += record.display_name.capacity();
    }
    bytes += author_thresholds_.capacity() * sizeof(std::uint32_t);
    bytes += block_start_ms_.capacity() * sizeof(std::int64_t);
    bytes += block_weight_total_.capacity() * sizeof(std::uint32_t);
    bytes += overlay_.capacity() * sizeof(EditRecord);
    for (const EditRecord& record : overlay_) bytes += record.text.capacity();
    bytes += erased_.capacity() * sizeof(ErasedRange);
    return bytes;
}

// -- calendar ----------------------------------------------------------------------

std::int64_t utc_day_of(std::int64_t utc_ms) noexcept {
    constexpr std::int64_t day_ms = 86'400'000;
    std::int64_t day = utc_ms / day_ms;
    if (utc_ms % day_ms < 0) --day;
    return day;
}

CivilTime civil_time_of(std::int64_t utc_ms) noexcept {
    constexpr std::int64_t day_ms = 86'400'000;
    const std::int64_t days = utc_day_of(utc_ms);
    const std::int64_t in_day = utc_ms - days * day_ms;
    // civil_from_days (Hinnant), shifted to an era starting 0000-03-01.
    const std::int64_t z = days + 719'468;
    std::int64_t era = z / 146'097;
    if (z < 0 && z % 146'097 != 0) --era;
    const std::int64_t day_of_era = z - era * 146'097;
    const std::int64_t year_of_era = (day_of_era - day_of_era / 1'460 + day_of_era / 36'524 - day_of_era / 146'096) / 365;
    const std::int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const std::int64_t month_index = (5 * day_of_year + 2) / 153;
    const std::int64_t day = day_of_year - (153 * month_index + 2) / 5 + 1;
    std::int64_t month = month_index + 3;
    if (month > 12) month -= 12;
    std::int64_t year = year_of_era + era * 400;
    if (month <= 2) ++year;
    std::int64_t weekday = (days + 4) % 7;   // 1970-01-01 was a Thursday
    if (weekday < 0) weekday += 7;
    CivilTime civil{};
    civil.year = year;
    civil.month = static_cast<std::uint32_t>(month);
    civil.day = static_cast<std::uint32_t>(day);
    civil.hour = static_cast<std::uint32_t>(in_day / 3'600'000);
    civil.minute = static_cast<std::uint32_t>((in_day / 60'000) % 60);
    civil.weekday = static_cast<std::uint32_t>(weekday);
    return civil;
}

} // namespace chat_stress
