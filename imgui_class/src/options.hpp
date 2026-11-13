#pragma once

// Command line of imgui-class:
//
//   imgui-class [--measure WORKLOAD] [--rows N] [--seed S] [--output-dir DIR]
//               [--font-dir DIR] [--width W] [--height H]
//
// --measure runs one workload headless (the offscreen raster) and writes
// its record (docs/WORKLOADS.md). --rows sets the table's rows (default
// 2000), --seed the data's seed. Otherwise the window opens for use.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace imgui_class {

struct AppOptions final {
    std::optional<std::string> measure{};
    std::size_t rows{2000U};
    std::uint64_t seed{1U};
    std::filesystem::path output_dir{"."};
    std::filesystem::path font_dir{};
    double width{1280.0};
    double height{820.0};
    bool help{};
};

struct OptionsResult final {
    AppOptions options{};
    // Empty when the command line was accepted.
    std::string error{};
};

inline constexpr std::size_t minimum_rows = 1U;
inline constexpr std::size_t maximum_rows = 1'000'000U;

[[nodiscard]] OptionsResult parse_options(int argc, const char* const* argv);
[[nodiscard]] const char* usage_text() noexcept;

} // namespace imgui_class
