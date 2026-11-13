#pragma once

// Command line of flame-graph:
//
//   flame-graph [--trace FILE] [--format auto|chrome|folded] [--measure WORKLOAD]
//               [--threads N] [--spans N] [--names N] [--seed S]
//               [--output-dir DIR] [--font-dir DIR] [--width W] [--height H]
//
// --trace opens a recorded trace (Chrome trace JSON or folded stacks,
// detected unless --format says). Without it the viewer generates a seeded
// synthetic trace (--threads lanes of --spans spans over --names frame
// names). --measure runs one workload headless (the offscreen raster) and
// writes its record (docs/WORKLOADS.md).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "trace_loader.hpp"

namespace flame_graph {

struct AppOptions final {
    std::optional<std::filesystem::path> trace{};
    TraceFormat format{TraceFormat::automatic};
    std::optional<std::string> measure{};
    std::size_t threads{4U};
    std::size_t spans{20000U};
    std::size_t names{400U};
    std::uint64_t seed{1U};
    std::filesystem::path output_dir{"."};
    std::filesystem::path font_dir{};
    double width{1280.0};
    double height{860.0};
    bool help{};
};

struct OptionsResult final {
    AppOptions options{};
    // Empty when the command line was accepted.
    std::string error{};
};

[[nodiscard]] OptionsResult parse_options(int argc, const char* const* argv);
[[nodiscard]] const char* usage_text() noexcept;

} // namespace flame_graph
