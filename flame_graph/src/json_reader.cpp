#include "json_reader.hpp"

#include <charconv>
#include <cmath>
#include <system_error>

namespace flame_graph {

namespace {

bool is_space(char byte) noexcept {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
}

bool is_digit(char byte) noexcept {
    return byte >= '0' && byte <= '9';
}

// The value of one hexadecimal digit, or none.
std::optional<std::uint32_t> hex_digit(char byte) noexcept {
    if (byte >= '0' && byte <= '9') return static_cast<std::uint32_t>(byte - '0');
    if (byte >= 'a' && byte <= 'f') return static_cast<std::uint32_t>(byte - 'a' + 10);
    if (byte >= 'A' && byte <= 'F') return static_cast<std::uint32_t>(byte - 'A' + 10);
    return std::nullopt;
}

constexpr std::uint32_t replacement_character = 0xfffdU;

} // namespace

JsonReader::JsonReader(std::string_view text) : text_(text) {
    buffer_.reserve(256U);
}

void JsonReader::skip_space() noexcept {
    while (at_ < text_.size() && is_space(text_[at_])) ++at_;
}

JsonToken JsonReader::fail(std::string_view message) noexcept {
    if (failed_) return JsonToken::error;
    failed_ = true;
    error_.offset = at_;
    error_.message = message;
    // Line and column of the offset, counted once here.
    std::size_t line = 1U;
    std::size_t line_start = 0U;
    const std::size_t limit = at_ < text_.size() ? at_ : text_.size();
    for (std::size_t index = 0U; index < limit; ++index) {
        if (text_[index] != '\n') continue;
        ++line;
        line_start = index + 1U;
    }
    error_.line = line;
    error_.column = limit - line_start + 1U;
    return JsonToken::error;
}

JsonToken JsonReader::next() {
    if (failed_) return JsonToken::error;
    skip_space();
    if (depth_ == 0U) {
        if (done_) {
            if (at_ < text_.size()) return fail("text after the top value");
            return JsonToken::end;
        }
        if (at_ >= text_.size()) return fail("no value");
        done_ = true;
        return read_value();
    }
    if (at_ >= text_.size()) return fail("unterminated container");
    Frame& frame = frames_[depth_ - 1U];
    const char byte = text_[at_];
    if (frame.kind == FrameKind::object) {
        if (frame.state == FrameState::after_key) {
            if (byte != ':') return fail("expected ':'");
            ++at_;
            skip_space();
            if (at_ >= text_.size()) return fail("no value after ':'");
            frame.state = FrameState::after_value;
            return read_value();
        }
        if (byte == '}') return close(FrameKind::object);
        if (frame.state == FrameState::after_value) {
            if (byte != ',') return fail("expected ',' or '}'");
            ++at_;
            skip_space();
            if (at_ >= text_.size()) return fail("no key after ','");
        }
        if (text_[at_] != '"') return fail("expected a key");
        frame.state = FrameState::after_key;
        return read_string(JsonToken::key);
    }
    if (byte == ']') return close(FrameKind::array);
    if (frame.state == FrameState::after_value) {
        if (byte != ',') return fail("expected ',' or ']'");
        ++at_;
        skip_space();
        if (at_ >= text_.size()) return fail("no value after ','");
    }
    frame.state = FrameState::after_value;
    return read_value();
}

JsonToken JsonReader::close(FrameKind kind) {
    // A trailing comma (state first after a ',') cannot reach here: the
    // comma path requires a key or value next.
    ++at_;
    --depth_;
    const JsonToken token = kind == FrameKind::object ? JsonToken::end_object : JsonToken::end_array;
    return token;
}

JsonToken JsonReader::read_value() {
    const char byte = text_[at_];
    if (byte == '{' || byte == '[') {
        if (depth_ >= maximum_depth) return fail("nested deeper than the reader's limit");
        Frame frame{};
        frame.kind = byte == '{' ? FrameKind::object : FrameKind::array;
        frame.state = FrameState::first;
        frames_[depth_] = frame;
        ++depth_;
        ++at_;
        const JsonToken token = byte == '{' ? JsonToken::begin_object : JsonToken::begin_array;
        return token;
    }
    if (byte == '"') return read_string(JsonToken::string);
    if (byte == '-' || is_digit(byte)) return read_number();
    if (byte == 't') return read_literal("true", JsonToken::true_value);
    if (byte == 'f') return read_literal("false", JsonToken::false_value);
    if (byte == 'n') return read_literal("null", JsonToken::null_value);
    return fail("unexpected character");
}

JsonToken JsonReader::read_literal(std::string_view word, JsonToken token) {
    if (text_.substr(at_, word.size()) != word) return fail("unknown literal");
    at_ += word.size();
    return token;
}

JsonToken JsonReader::read_number() {
    const std::size_t start = at_;
    if (text_[at_] == '-') ++at_;
    if (at_ >= text_.size() || !is_digit(text_[at_])) return fail("expected a digit");
    if (text_[at_] == '0') {
        ++at_;
    } else {
        while (at_ < text_.size() && is_digit(text_[at_])) ++at_;
    }
    if (at_ < text_.size() && text_[at_] == '.') {
        ++at_;
        if (at_ >= text_.size() || !is_digit(text_[at_])) return fail("expected a digit after '.'");
        while (at_ < text_.size() && is_digit(text_[at_])) ++at_;
    }
    if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
        ++at_;
        if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) ++at_;
        if (at_ >= text_.size() || !is_digit(text_[at_])) return fail("expected an exponent digit");
        while (at_ < text_.size() && is_digit(text_[at_])) ++at_;
    }
    number_ = text_.substr(start, at_ - start);
    return JsonToken::number;
}

void JsonReader::append_scalar(std::uint32_t scalar) {
    if (scalar < 0x80U) {
        buffer_.push_back(static_cast<char>(scalar));
    } else if (scalar < 0x800U) {
        buffer_.push_back(static_cast<char>(0xc0U | (scalar >> 6U)));
        buffer_.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
    } else if (scalar < 0x10000U) {
        buffer_.push_back(static_cast<char>(0xe0U | (scalar >> 12U)));
        buffer_.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU)));
        buffer_.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
    } else {
        buffer_.push_back(static_cast<char>(0xf0U | (scalar >> 18U)));
        buffer_.push_back(static_cast<char>(0x80U | ((scalar >> 12U) & 0x3fU)));
        buffer_.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU)));
        buffer_.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
    }
}

JsonToken JsonReader::read_string(JsonToken token) {
    ++at_;
    const std::size_t start = at_;
    // The plain case first: no escape before the closing quote, so the
    // result is a view into the text.
    while (at_ < text_.size() && text_[at_] != '"' && text_[at_] != '\\') {
        if (static_cast<unsigned char>(text_[at_]) < 0x20U) return fail("control character in a string");
        ++at_;
    }
    if (at_ >= text_.size()) return fail("unterminated string");
    if (text_[at_] == '"') {
        string_ = text_.substr(start, at_ - start);
        ++at_;
        return token;
    }
    buffer_.assign(text_.substr(start, at_ - start));
    while (at_ < text_.size() && text_[at_] != '"') {
        const char byte = text_[at_];
        if (static_cast<unsigned char>(byte) < 0x20U) return fail("control character in a string");
        if (byte != '\\') {
            buffer_.push_back(byte);
            ++at_;
            continue;
        }
        ++at_;
        if (at_ >= text_.size()) return fail("unterminated escape");
        const char escape = text_[at_];
        ++at_;
        if (escape == '"' || escape == '\\' || escape == '/') {
            buffer_.push_back(escape);
        } else if (escape == 'b') {
            buffer_.push_back('\b');
        } else if (escape == 'f') {
            buffer_.push_back('\f');
        } else if (escape == 'n') {
            buffer_.push_back('\n');
        } else if (escape == 'r') {
            buffer_.push_back('\r');
        } else if (escape == 't') {
            buffer_.push_back('\t');
        } else if (escape == 'u') {
            std::uint32_t unit = 0U;
            for (std::size_t digit = 0U; digit < 4U; ++digit) {
                if (at_ >= text_.size()) return fail("short \\u escape");
                const std::optional<std::uint32_t> value = hex_digit(text_[at_]);
                if (!value.has_value()) return fail("bad \\u escape");
                unit = (unit << 4U) | *value;
                ++at_;
            }
            std::uint32_t scalar = unit;
            if (unit >= 0xd800U && unit <= 0xdbffU) {
                // A high surrogate: a low one must follow, else U+FFFD.
                scalar = replacement_character;
                const bool pair = at_ + 6U <= text_.size() && text_[at_] == '\\' && text_[at_ + 1U] == 'u';
                if (pair) {
                    std::uint32_t low = 0U;
                    bool valid = true;
                    for (std::size_t digit = 0U; digit < 4U; ++digit) {
                        const std::optional<std::uint32_t> value = hex_digit(text_[at_ + 2U + digit]);
                        if (!value.has_value()) valid = false;
                        low = (low << 4U) | value.value_or(0U);
                    }
                    if (valid && low >= 0xdc00U && low <= 0xdfffU) {
                        scalar = 0x10000U + ((unit - 0xd800U) << 10U) + (low - 0xdc00U);
                        at_ += 6U;
                    }
                }
            } else if (unit >= 0xdc00U && unit <= 0xdfffU) {
                scalar = replacement_character;
            }
            append_scalar(scalar);
        } else {
            return fail("unknown escape");
        }
    }
    if (at_ >= text_.size()) return fail("unterminated string");
    ++at_;
    string_ = buffer_;
    return token;
}

bool JsonReader::skip_value() {
    std::size_t open = 0U;
    while (true) {
        const JsonToken token = next();
        if (token == JsonToken::error || token == JsonToken::end) return false;
        if (token == JsonToken::begin_object || token == JsonToken::begin_array) {
            ++open;
            continue;
        }
        if (token == JsonToken::end_object || token == JsonToken::end_array) {
            // Closing a container the skipped value did not open: the
            // caller asked to skip where no value was.
            if (open == 0U) return false;
            --open;
            if (open == 0U) return true;
            continue;
        }
        if (token == JsonToken::key) continue;
        if (open == 0U) return true;
    }
}

std::optional<double> JsonReader::number() const noexcept {
    if (number_.empty()) return std::nullopt;
    double value = 0.0;
    const std::from_chars_result parsed = std::from_chars(number_.data(), number_.data() + number_.size(), value);
    if (parsed.ec != std::errc{} || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<std::int64_t> JsonReader::integer() const noexcept {
    if (number_.empty()) return std::nullopt;
    std::int64_t value = 0;
    const std::from_chars_result parsed = std::from_chars(number_.data(), number_.data() + number_.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != number_.data() + number_.size()) return std::nullopt;
    return value;
}

} // namespace flame_graph
