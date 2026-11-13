#include "options.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace implot3d_class {

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
    return "usage: implot3d-class [--measure WORKLOAD] [--grid N] [--output-dir DIR]\n"
           "                      [--font-dir DIR] [--width W] [--height H]\n"
           "workloads: tour orbit zoom pick strides interpolation resolution idle\n";
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
        } else if (name == "--grid") {
            const std::optional<std::uint64_t> side = parse_unsigned(value);
            if (!side.has_value() || *side < minimum_grid_option || *side > maximum_grid_option) {
                result.error = "--grid takes 4 to 1024";
                return result;
            }
            options.grid_side = static_cast<std::size_t>(*side);
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

} // namespace implot3d_class
