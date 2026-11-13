#pragma once

// A minimal JSON writer into one reused string: objects, arrays, keys and
// scalars, with commas placed by a depth stack. Strings are escaped (quote,
// backslash and control characters); invalid UTF-8 is written as is. The
// same writer as chat_stress's, kept per demonstrator so that each builds
// on its own.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace implot3d_class {

class JsonWriter final {
public:
    static constexpr std::size_t maximum_depth = 32U;

    JsonWriter();
    void begin_object();
    void end_object();
    void begin_array();
    void end_array();
    void key(std::string_view name);
    void value(std::string_view text);
    void value(const char* text) { value(std::string_view(text)); }
    void value(std::uint64_t number);
    void value(std::int64_t number);
    void value(double number);
    void value(bool flag);
    void null();
    // Writes an already formed JSON value (MetricsSnapshot::to_json).
    void raw(std::string_view json);
    // Writes `number` when present, null otherwise.
    void optional(const std::optional<std::uint64_t>& number);
    [[nodiscard]] const std::string& text() const& noexcept { return text_; }
    const std::string& text() const&& = delete;

private:
    void separate();
    void write_string(std::string_view text);
    std::string text_{};
    // Per open container: whether it has an element yet.
    std::vector<bool> has_element_{};
    bool after_key_{};
};

} // namespace implot3d_class
