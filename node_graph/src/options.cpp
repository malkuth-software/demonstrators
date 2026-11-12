#include "options.hpp"

#include <cerrno>
#include <cstdlib>
#include <string_view>

namespace node_graph {

namespace {

// Parses an unsigned integer (decimal, or hexadecimal with 0x); none on
// anything else or overflow.
std::optional<std::uint64_t> parse_unsigned(std::string_view text) {
    if (text.empty()) return std::nullopt;
    const std::string copy(text);
    char* end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(copy.c_str(), &end, 0);
    if (errno != 0 || end == nullptr || *end != '\0' || copy[0] == '-') return std::nullopt;
    return static_cast<std::uint64_t>(value);
}

std::optional<double> parse_extent(std::string_view text) {
    const std::optional<std::uint64_t> value = parse_unsigned(text);
    if (!value.has_value() || *value < 200U || *value > 8192U) return std::nullopt;
    return static_cast<double>(*value);
}

std::optional<std::size_t> parse_node_count(std::string_view text) {
    const std::optional<std::uint64_t> value = parse_unsigned(text);
    if (!value.has_value() || *value == 0U || *value > maximum_generated_nodes) return std::nullopt;
    return static_cast<std::size_t>(*value);
}

} // namespace

const char* usage_text() noexcept {
    return "usage: node-graph [FILE] [--generate N] [--links-per-node L] [--seed S] [--output FILE]\n"
           "                  [--measure WORKLOAD] [--nodes N] [--output-dir DIR] [--font-dir DIR]\n"
           "                  [--width W] [--height H]\n"
           "workloads: load pan zoom drag magnet marquee-knife edit-host roundtrip idle\n";
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
        if (name.empty() || name[0] != '-') {
            if (!options.file.empty()) {
                result.error = "more than one file named";
                return result;
            }
            options.file = std::filesystem::path(std::string(name));
            continue;
        }
        if (index + 1 >= argc) {
            result.error = "missing value after " + std::string(name);
            return result;
        }
        const std::string_view value(argv[index + 1]);
        ++index;
        if (name == "--generate" || name == "--nodes") {
            const std::optional<std::size_t> count = parse_node_count(value);
            if (!count.has_value()) {
                result.error = std::string(name) + " takes a node count from 1 to 1000000";
                return result;
            }
            if (name == "--generate") {
                options.generate = *count;
            } else {
                options.nodes = *count;
            }
        } else if (name == "--links-per-node") {
            const std::optional<std::uint64_t> links = parse_unsigned(value);
            if (!links.has_value() || *links > 8U) {
                result.error = "--links-per-node takes 0 to 8";
                return result;
            }
            options.links_per_node = static_cast<std::size_t>(*links);
        } else if (name == "--seed") {
            const std::optional<std::uint64_t> seed = parse_unsigned(value);
            if (!seed.has_value()) {
                result.error = "--seed takes an unsigned 64-bit integer";
                return result;
            }
            options.seed = *seed;
        } else if (name == "--output") {
            options.output = std::filesystem::path(std::string(value));
        } else if (name == "--measure") {
            options.measure = std::string(value);
        } else if (name == "--output-dir") {
            options.output_dir = std::filesystem::path(std::string(value));
        } else if (name == "--font-dir") {
            options.font_dir = std::filesystem::path(std::string(value));
        } else if (name == "--width" || name == "--height") {
            const std::optional<double> extent = parse_extent(value);
            if (!extent.has_value()) {
                result.error = std::string(name) + " takes 200 to 8192";
                return result;
            }
            if (name == "--width") {
                options.width = *extent;
            } else {
                options.height = *extent;
            }
        } else {
            result.error = "unknown option " + std::string(name);
            return result;
        }
    }
    if (options.generate.has_value() && options.output.empty()) {
        result.error = "--generate needs --output";
    }
    return result;
}

} // namespace node_graph
