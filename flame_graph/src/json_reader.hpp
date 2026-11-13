#pragma once

// A bounded pull reader for JSON text (RFC 8259), for the Chrome trace
// loader: next() returns one token at a time, so a trace of millions of
// events is read without building a document. Containers are tracked on a
// fixed stack of at most maximum_depth frames; deeper text is an error, as
// is anything after the top value. Strings are decoded (escapes, \u with
// surrogate pairs; a lone surrogate becomes U+FFFD); numbers are kept as
// their text and converted on request.
//
// Ownership: the reader borrows `text` for its whole life (the caller keeps
// it). string() returns a view into the text when the string had no
// escapes, else into the reader's own buffer; either is valid until the
// next call of next() or skip_value(). One thread.
//
// Failure: the first error stops the reader; next() returns error from
// then on and error() says where (byte offset, 1-based line and column)
// and what.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace flame_graph {

enum class JsonToken : std::uint8_t {
    begin_object,
    end_object,
    begin_array,
    end_array,
    key,
    string,
    number,
    true_value,
    false_value,
    null_value,
    end,
    error
};

struct JsonError final {
    std::size_t offset{};
    std::size_t line{};
    std::size_t column{};
    // A static description; empty when there was no error.
    std::string_view message{};
};

class JsonReader final {
public:
    static constexpr std::size_t maximum_depth = 64U;

    explicit JsonReader(std::string_view text);
    JsonReader(const JsonReader&) = delete;
    JsonReader& operator=(const JsonReader&) = delete;

    [[nodiscard]] JsonToken next();
    // After a key: consumes the key's whole value. At any other point:
    // consumes the next value. False when that ended in an error or at the
    // end of the text.
    bool skip_value();
    // The last key or string token's decoded bytes.
    [[nodiscard]] std::string_view string() const& noexcept { return string_; }
    std::string_view string() const&& = delete;
    // The last number token's text, and its value; integer() is none for a
    // number with a fraction or exponent or out of the 64-bit range.
    [[nodiscard]] std::string_view number_text() const& noexcept { return number_; }
    std::string_view number_text() const&& = delete;
    [[nodiscard]] std::optional<double> number() const noexcept;
    [[nodiscard]] std::optional<std::int64_t> integer() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept { return depth_; }
    [[nodiscard]] std::size_t offset() const noexcept { return at_; }
    [[nodiscard]] const JsonError& error() const noexcept { return error_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    enum class FrameKind : std::uint8_t { object, array };
    enum class FrameState : std::uint8_t { first, after_value, after_key };
    struct Frame final {
        FrameKind kind{FrameKind::object};
        FrameState state{FrameState::first};
    };

    void skip_space() noexcept;
    JsonToken fail(std::string_view message) noexcept;
    JsonToken read_value();
    JsonToken read_string(JsonToken token);
    JsonToken read_number();
    JsonToken read_literal(std::string_view word, JsonToken token);
    JsonToken close(FrameKind kind);
    // Appends the UTF-8 form of `scalar` to buffer_.
    void append_scalar(std::uint32_t scalar);

    std::string_view text_{};
    std::size_t at_{};
    std::array<Frame, maximum_depth> frames_{};
    std::size_t depth_{};
    bool done_{};
    bool failed_{};
    JsonError error_{};
    std::string_view string_{};
    std::string_view number_{};
    std::string buffer_{};
};

} // namespace flame_graph
