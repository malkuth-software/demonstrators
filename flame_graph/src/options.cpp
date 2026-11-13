#include "options.hpp"

#include "trace_generator.hpp"

#include <cerrno>
#include <cstdlib>
#include <string_view>

namespace flame_graph {

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

// A count option in [minimum, maximum]; sets `error` and returns false
// otherwise.
bool parse_count(std::string_view name, std::string_view value, std::uint64_t minimum, std::uint64_t maximum,
                 std::size_t& out, std::string& error) {
    const std::optional<std::uint64_t> count = parse_unsigned(value);
    if (!count.has_value() || *count < minimum || *count > maximum) {
        error = std::string(name) + " takes " + std::to_string(minimum) + " to " + std::to_string(maximum);
        return false;
    }
    out = static_cast<std::size_t>(*count);
    return true;
}

} // namespace

const char* usage_text() noexcept {
    return "usage: flame-graph [--trace FILE] [--format auto|chrome|folded] [--measure WORKLOAD]\n"
           "                   [--threads N] [--spans N] [--names N] [--seed S]\n"
           "                   [--output-dir DIR] [--font-dir DIR] [--width W] [--height H]\n"
           "workloads: load flame-zoom flame-pan timeline-zoom timeline-pan search table-scroll\n"
           "           mode-switch idle\n";
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
        if (name == "--trace") {
            options.trace = std::filesystem::path(std::string(value));
        } else if (name == "--format") {
            if (value == "auto") {
                options.format = TraceFormat::automatic;
            } else if (value == "chrome") {
                options.format = TraceFormat::chrome_json;
            } else if (value == "folded") {
                options.format = TraceFormat::folded;
            } else {
                result.error = "--format takes auto, chrome or folded";
                return result;
            }
        } else if (name == "--measure") {
            options.measure = std::string(value);
        } else if (name == "--threads") {
            if (!parse_count(name, value, 1U, maximum_generated_threads, options.threads, result.error)) return result;
        } else if (name == "--spans") {
            if (!parse_count(name, value, 1U, maximum_generated_spans, options.spans, result.error)) return result;
        } else if (name == "--names") {
            if (!parse_count(name, value, 1U, 100000U, options.names, result.error)) return result;
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

} // namespace flame_graph
