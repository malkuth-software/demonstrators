#include "options.hpp"

#include <cerrno>
#include <cstdlib>
#include <string_view>

namespace imgui_class {

namespace {

// Parses an unsigned decimal integer; none on anything else or overflow.
std::optional<std::uint64_t> parse_unsigned(std::string_view text) {
    if (text.empty() || text[0] == '-') return std::nullopt;
    const std::string copy(text);
    char* end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(copy.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || *end != '\0') return std::nullopt;
    return static_cast<std::uint64_t>(value);
}

} // namespace

const char* usage_text() noexcept {
    return "usage: imgui-class [--measure WORKLOAD] [--rows N] [--seed S] [--output-dir DIR]\n"
           "                   [--font-dir DIR] [--width W] [--height H]\n"
           "workloads: tour widgets table scrub colour style idle\n";
}

OptionsResult parse_options(int argc, const char* const* argv) {
    OptionsResult result{};
    AppOptions& options = result.options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view name(argv[index]);
        if (name == "--help" || name == "-h") {
            options.help = true;
            continue;
        }
        if (index + 1 >= argc) {
            result.error = "missing value after " + std::string(name);
            return result;
        }
        const std::string_view value(argv[index + 1]);
        ++index;
        if (name == "--measure") {
            options.measure = std::string(value);
        } else if (name == "--rows") {
            const std::optional<std::uint64_t> count = parse_unsigned(value);
            if (!count.has_value() || *count < minimum_rows || *count > maximum_rows) {
                result.error = "--rows takes 1 to 1000000";
                return result;
            }
            options.rows = static_cast<std::size_t>(*count);
        } else if (name == "--seed") {
            const std::optional<std::uint64_t> seed = parse_unsigned(value);
            if (!seed.has_value()) {
                result.error = "--seed takes an unsigned integer";
                return result;
            }
            options.seed = *seed;
        } else if (name == "--output-dir") {
            options.output_dir = std::filesystem::path(std::string(value));
        } else if (name == "--font-dir") {
            options.font_dir = std::filesystem::path(std::string(value));
        } else if (name == "--width" || name == "--height") {
            const std::optional<std::uint64_t> extent = parse_unsigned(value);
            if (!extent.has_value() || *extent < 320U || *extent > 8192U) {
                result.error = std::string(name) + " takes 320 to 8192";
                return result;
            }
            if (name == "--width") {
                options.width = static_cast<double>(*extent);
            } else {
                options.height = static_cast<double>(*extent);
            }
        } else {
            result.error = "unknown option " + std::string(name);
            return result;
        }
    }
    return result;
}

} // namespace imgui_class
