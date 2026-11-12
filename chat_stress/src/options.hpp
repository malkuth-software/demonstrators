#pragma once

// Command line of chat-stress:
//
//   chat-stress [--seed S] [--messages N] [--users U] [--scripts latin|mixed]
//               [--measure WORKLOAD] [--output-dir DIR] [--font-dir DIR]
//               [--width W] [--height H]
//
// WORKLOAD is one of the names in docs/WORKLOADS.md (startup, memory, idle,
// idle-focused, wheel, trackpad, page-down, thumb, jump, append, edit,
// setup). Without --measure the app opens its window for interactive use.

#include "corpus.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace chat_stress {

struct AppOptions final {
    CorpusOptions corpus{};
    std::optional<std::string> measure{};
    std::filesystem::path output_dir{"."};
    std::filesystem::path font_dir{};
    double width{1280.0};
    double height{800.0};
    bool help{};
};

struct OptionsResult final {
    AppOptions options{};
    // Empty when the command line was accepted.
    std::string error{};
};

[[nodiscard]] OptionsResult parse_options(int argc, const char* const* argv);
[[nodiscard]] const char* usage_text() noexcept;

} // namespace chat_stress
