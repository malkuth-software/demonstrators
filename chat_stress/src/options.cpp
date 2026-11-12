#include "options.hpp"

#include <cerrno>
#include <cstdlib>
#include <string_view>

namespace chat_stress {

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

} // namespace

const char* usage_text() noexcept {
    return "usage: chat-stress [--seed S] [--messages N] [--users U] [--scripts latin|mixed]\n"
           "                   [--measure WORKLOAD] [--output-dir DIR] [--font-dir DIR]\n"
           "                   [--width W] [--height H]\n"
           "workloads: startup memory idle idle-focused wheel trackpad page-down thumb jump\n"
           "           append edit setup\n";
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
        if (name == "--seed") {
            const std::optional<std::uint64_t> seed = parse_unsigned(value);
            if (!seed.has_value()) {
                result.error = "--seed takes an unsigned 64-bit integer";
                return result;
            }
            options.corpus.seed = *seed;
        } else if (name == "--messages") {
            const std::optional<std::uint64_t> messages = parse_unsigned(value);
            if (!messages.has_value() || *messages > maximum_messages) {
                result.error = "--messages takes 0 to 1000000000";
                return result;
            }
            options.corpus.messages = *messages;
        } else if (name == "--users") {
            const std::optional<std::uint64_t> users = parse_unsigned(value);
            if (!users.has_value() || *users == 0U || *users > maximum_users) {
                result.error = "--users takes 1 to 65000";
                return result;
            }
            options.corpus.users = static_cast<std::uint32_t>(*users);
        } else if (name == "--scripts") {
            if (value == "latin") {
                options.corpus.mixed_scripts = false;
            } else if (value == "mixed") {
                options.corpus.mixed_scripts = true;
            } else {
                result.error = "--scripts takes latin or mixed";
                return result;
            }
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
    return result;
}

} // namespace chat_stress
