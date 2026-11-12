#pragma once

// Command line of node-graph:
//
//   node-graph [FILE] [--generate N] [--links-per-node L] [--seed S]
//              [--output FILE] [--measure WORKLOAD] [--nodes N]
//              [--output-dir DIR] [--font-dir DIR] [--width W] [--height H]
//
// FILE opens a graph document. --generate writes a seeded stress graph of N
// nodes to --output and exits. --measure runs one workload headless (the
// offscreen raster) and writes its record (docs/WORKLOADS.md); --nodes sets
// the generated graph's size for it. Otherwise the window opens for use.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace node_graph {

struct AppOptions final {
    std::filesystem::path file{};
    std::optional<std::size_t> generate{};
    std::size_t links_per_node{2U};
    std::uint64_t seed{1U};
    std::filesystem::path output{};
    std::optional<std::string> measure{};
    std::size_t nodes{1000U};
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

// The most nodes --generate and --nodes accept.
inline constexpr std::size_t maximum_generated_nodes = 1'000'000U;

[[nodiscard]] OptionsResult parse_options(int argc, const char* const* argv);
[[nodiscard]] const char* usage_text() noexcept;

} // namespace node_graph
