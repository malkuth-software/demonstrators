#include "options.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace implot_class {

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
    return "usage: implot-class [--measure WORKLOAD] [--large-points N] [--output-dir DIR]\n"
           "                    [--font-dir DIR] [--width W] [--height H] [--export-png FILE]\n"
           "workloads: tour large-pan heatmap drag live link idle\n";
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
        } else if (name == "--large-points") {
            const std::optional<std::uint64_t> count = parse_unsigned(value);
            if (!count.has_value() || *count < minimum_large_points || *count > maximum_large_points) {
                result.error = "--large-points takes 1000 to 50000000";
                return result;
            }
            options.large_points = static_cast<std::size_t>(*count);
        } else if (name == "--output-dir") {
            options.output_dir = std::filesystem::path(std::string(value));
        } else if (name == "--font-dir") {
            options.font_dir = std::filesystem::path(std::string(value));
        } else if (name == "--export-png") {
            options.export_png = std::filesystem::path(std::string(value));
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

} // namespace implot_class
