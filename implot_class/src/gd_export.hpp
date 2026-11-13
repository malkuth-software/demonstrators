#pragma once

// The gd_* path: a plot drawn through the experimental GUI.Drawing C ABI
// (drawing_c_api.h, ABI 0.3) into an offscreen bitmap and written as PNG,
// the way a C or C# consumer would draw a plot it exports (a report, a
// clipboard image). It exists to put plot-shaped work through the ABI
// before the drawing seal; every call it could not make is a GAPS.md G
// entry.
//
// What it draws, per series kind, with the gd_* call it uses:
//   background, plot area   recorder_fill_rectangle
//   grid lines              recorder_draw_line_segments (one batch)
//   a line series           recorder_draw_lines, split at NaN gaps by the
//                           app (the ABI's polyline takes no gaps, G3)
//   a scatter series        recorder_draw_markers (one batch)
//   bars                    recorder_fill_rectangles (one batch)
//   a shaded band           recorder_fill_polygon, the band closed by hand
//                           (no fill_band in the ABI, G2)
//   a heatmap               bitmap_lock + recorder_draw_bitmap: cells are
//                           written as pixels (no fill_rects_each or
//                           per-rectangle colours in the ABI, G4)
//
// Built only when the CMake option IMPLOT_CLASS_GD_LIBRARY names the
// library (IMPLOT_CLASS_WITH_GD); otherwise export_plot_png reports
// GdExportStatus::not_built. The raster service must be installed for
// recorder_execute to draw: the raster module's entry point gdr_initialize
// has no public header (G5), so it is declared here by hand when
// IMPLOT_CLASS_WITH_GD_RASTER is set.

#include "series_data.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace implot_class {

enum class GdExportStatus : std::uint8_t {
    written,
    // The program was built without the gd_* library.
    not_built,
    // gd_get_api_v0 refused the version or the table.
    api_unavailable,
    // A gd_* call failed; `failed_call` names it.
    call_failed,
    // The PNG could not be written to the path.
    file_failed,
};

struct GdExportReport final {
    GdExportStatus status{GdExportStatus::not_built};
    // The first gd_* call that failed, and its gd_result.
    std::string failed_call{};
    std::uint32_t result{};
    std::uint64_t commands_recorded{};
    std::uint64_t commands_executed{};
    std::uint64_t png_bytes{};
    // Line pieces drawn after splitting at gaps (G3).
    std::uint64_t line_pieces{};
};

struct GdExportInput final {
    std::uint32_t width{800U};
    std::uint32_t height{500U};
    // Borrowed for the call.
    const XYSeries* line{};
    const XYSeries* scatter{};
    std::span<const double> bars{};
    // rows * columns values in [0, 1], drawn in the lower-right quarter.
    std::span<const double> heatmap{};
    std::uint32_t heatmap_rows{};
    std::uint32_t heatmap_columns{};
};

// Draws `input` and writes the PNG to `path`. Allocates (the PNG buffer and
// the gd objects, all released before returning, also on failure).
[[nodiscard]] GdExportReport export_plot_png(const GdExportInput& input, const std::filesystem::path& path);

} // namespace implot_class
