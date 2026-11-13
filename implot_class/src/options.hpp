#pragma once

// Command line of implot-class:
//
//   implot-class [--measure WORKLOAD] [--large-points N] [--output-dir DIR]
//                [--font-dir DIR] [--width W] [--height H]
//                [--export-png FILE]
//
// --measure runs one workload headless (the offscreen raster) and writes
// its record (docs/WORKLOADS.md). --large-points sets the large series'
// length (default 1,000,000). --export-png draws a plot through the gd_*
// ABI into FILE and exits (only in a build with IMPLOT_CLASS_GD_LIBRARY).
// Otherwise the window opens for use.

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace implot_class {

struct AppOptions final {
    std::optional<std::string> measure{};
    std::size_t large_points{1'000'000U};
    std::filesystem::path output_dir{"."};
    std::filesystem::path font_dir{};
    std::filesystem::path export_png{};
    double width{1280.0};
    double height{820.0};
    bool help{};
};

struct OptionsResult final {
    AppOptions options{};
    // Empty when the command line was accepted.
    std::string error{};
};

inline constexpr std::size_t minimum_large_points = 1000U;
inline constexpr std::size_t maximum_large_points = 50'000'000U;

[[nodiscard]] OptionsResult parse_options(int argc, const char* const* argv);
[[nodiscard]] const char* usage_text() noexcept;

} // namespace implot_class
