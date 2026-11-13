#pragma once

// Command line of implot3d-class:
//
//   implot3d-class [--measure WORKLOAD] [--grid N] [--output-dir DIR]
//                  [--font-dir DIR] [--width W] [--height H]
//
// --measure runs one workload headless (the offscreen raster) and writes
// its record (docs/WORKLOADS.md). --grid sets the surfaces' samples per
// side (default 96). Otherwise the window opens for use.

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace implot3d_class {

struct AppOptions final {
    std::optional<std::string> measure{};
    std::size_t grid_side{96U};
    std::filesystem::path output_dir{"."};
    std::filesystem::path font_dir{};
    double width{1100.0};
    double height{780.0};
    bool help{};
};

struct OptionsResult final {
    AppOptions options{};
    // Empty when the command line was accepted.
    std::string error{};
};

inline constexpr std::size_t minimum_grid_option = 4U;
inline constexpr std::size_t maximum_grid_option = 1024U;

[[nodiscard]] OptionsResult parse_options(int argc, const char* const* argv);
[[nodiscard]] const char* usage_text() noexcept;

} // namespace implot3d_class
