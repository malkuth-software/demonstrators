#include "json_writer.hpp"

#include <cmath>
#include <cstdio>

namespace imgui_class {

JsonWriter::JsonWriter() {
    text_.reserve(64U * 1024U);
    has_element_.reserve(maximum_depth);
}

void JsonWriter::separate() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (has_element_.empty()) return;
    if (has_element_.back()) text_.push_back(',');
    has_element_.back() = true;
}

void JsonWriter::begin_object() {
    separate();
    text_.push_back('{');
    has_element_.push_back(false);
}

void JsonWriter::end_object() {
    text_.push_back('}');
    if (!has_element_.empty()) has_element_.pop_back();
}

void JsonWriter::begin_array() {
    separate();
    text_.push_back('[');
    has_element_.push_back(false);
}

void JsonWriter::end_array() {
    text_.push_back(']');
    if (!has_element_.empty()) has_element_.pop_back();
}

void JsonWriter::key(std::string_view name) {
    separate();
    write_string(name);
    text_.push_back(':');
    after_key_ = true;
}

void JsonWriter::value(std::string_view text) {
    separate();
    write_string(text);
}

void JsonWriter::write_string(std::string_view text) {
    text_.push_back('"');
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            text_.push_back('\\');
            text_.push_back(character);
        } else if (byte < 0x20U) {
            char escaped[8]{};
            const int written = std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned int>(byte));
            if (written > 0) text_.append(escaped, static_cast<std::size_t>(written));
        } else {
            text_.push_back(character);
        }
    }
    text_.push_back('"');
}

void JsonWriter::value(std::uint64_t number) {
    separate();
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(number));
    if (written > 0) text_.append(buffer, static_cast<std::size_t>(written));
}

void JsonWriter::value(std::int64_t number) {
    separate();
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(number));
    if (written > 0) text_.append(buffer, static_cast<std::size_t>(written));
}

void JsonWriter::value(double number) {
    separate();
    if (!std::isfinite(number)) {
        text_.append("null");
        return;
    }
    char buffer[40]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%.17g", number);
    if (written > 0) text_.append(buffer, static_cast<std::size_t>(written));
}

void JsonWriter::value(bool flag) {
    separate();
    if (flag) {
        text_.append("true");
    } else {
        text_.append("false");
    }
}

void JsonWriter::null() {
    separate();
    text_.append("null");
}

void JsonWriter::raw(std::string_view json) {
    separate();
    text_.append(json);
}

void JsonWriter::optional(const std::optional<std::uint64_t>& number) {
    if (number.has_value()) {
        value(*number);
    } else {
        null();
    }
}

} // namespace imgui_class
