#include "gd_export.hpp"

#if defined(IMPLOT_CLASS_WITH_GD)
#include <gui_forms/drawing_c_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <vector>

#if defined(IMPLOT_CLASS_WITH_GD_RASTER)
// The raster module's entry point (drawing_skia_c_api.cpp). No installed
// header declares it (GAPS.md G5), so its type is written here by hand,
// against the house rule that entry points take their type from a
// declaration; it goes when GUI.Drawing publishes one.
extern "C" gd_result gdr_initialize(void);
#endif
#endif

namespace implot_class {

#if !defined(IMPLOT_CLASS_WITH_GD)

GdExportReport export_plot_png(const GdExportInput& input, const std::filesystem::path& path) {
    static_cast<void>(input);
    static_cast<void>(path);
    GdExportReport report{};
    report.status = GdExportStatus::not_built;
    return report;
}

#else

namespace {

// The gd objects one export made, released together at the end (each
// release drops the export's reference; the ABI disposes on the last).
class HandleSet final {
public:
    static constexpr std::size_t capacity = 16U;

    explicit HandleSet(const gd_api_v0& api) noexcept : api_(&api) {}
    ~HandleSet() {
        for (std::size_t index = 0U; index < count_; ++index) static_cast<void>((*api_).release(handles_[index]));
    }
    HandleSet(const HandleSet&) = delete;
    HandleSet& operator=(const HandleSet&) = delete;

    // Keeps `handle`; false (the handle released at once) when full.
    bool keep(gd_handle handle) noexcept {
        if (count_ == capacity) {
            static_cast<void>((*api_).release(handle));
            return false;
        }
        handles_[count_] = handle;
        count_ = count_ + 1U;
        return true;
    }

private:
    const gd_api_v0* api_{};
    std::array<gd_handle, capacity> handles_{};
    std::size_t count_{};
};

// Records the first failure; later steps are skipped by the caller.
struct Steps final {
    GdExportReport* report{};

    bool check(gd_result result, const char* call) {
        if (result == GD_OK) return true;
        if ((*report).failed_call.empty()) {
            (*report).status = GdExportStatus::call_failed;
            (*report).failed_call = call;
            (*report).result = static_cast<std::uint32_t>(result);
        }
        return false;
    }
};

gd_color argb(std::uint32_t value) noexcept {
    gd_color color{};
    color.argb = value;
    color.is_empty = 0U;
    return color;
}

// A linear map from data to the plot rectangle (y up).
struct DeviceMap final {
    double x0{};
    double x_scale{1.0};
    double y0{};
    double y_scale{1.0};
    gd_rect plot{};

    [[nodiscard]] gd_point at(double x, double y) const noexcept {
        gd_point point{};
        point.x = plot.x + (x - x0) * x_scale;
        point.y = plot.y + plot.height - (y - y0) * y_scale;
        return point;
    }
};

DeviceMap make_map(const XYSeries& series, gd_rect plot) noexcept {
    const ValueExtent x = finite_extent(series.xs);
    const ValueExtent y = finite_extent(series.ys);
    DeviceMap map{};
    map.plot = plot;
    map.x0 = x.minimum;
    map.x_scale = plot.width / (x.maximum - x.minimum);
    map.y0 = y.minimum;
    map.y_scale = plot.height / (y.maximum - y.minimum);
    return map;
}

} // namespace

GdExportReport export_plot_png(const GdExportInput& input, const std::filesystem::path& path) {
    GdExportReport report{};
    gd_api_v0 api{};
    api.struct_size = sizeof(api);
    if (gd_get_api_v0(GD_ABI_VERSION_0_3, &api) != GD_OK) {
        report.status = GdExportStatus::api_unavailable;
        return report;
    }
#if defined(IMPLOT_CLASS_WITH_GD_RASTER)
    static_cast<void>(gdr_initialize());
#endif
    HandleSet handles(api);
    Steps steps{&report};
    const double width = static_cast<double>(input.width);
    const double height = static_cast<double>(input.height);
    gd_handle recorder{};
    if (!steps.check(api.recorder_create(&recorder), "recorder_create")) return report;
    static_cast<void>(handles.keep(recorder));
    gd_handle paper{};
    gd_handle area{};
    gd_handle grid{};
    gd_handle ink{};
    gd_handle bar_brush{};
    gd_handle band_brush{};
    if (!steps.check(api.solid_brush_create(argb(0xFFEEEEEAU), &paper), "solid_brush_create")) return report;
    static_cast<void>(handles.keep(paper));
    if (!steps.check(api.solid_brush_create(argb(0xFFFCFCFAU), &area), "solid_brush_create")) return report;
    static_cast<void>(handles.keep(area));
    if (!steps.check(api.pen_create(argb(0xFFD6D6D0U), 1.0, &grid), "pen_create")) return report;
    static_cast<void>(handles.keep(grid));
    if (!steps.check(api.pen_create(argb(0xFFC85028U), 1.5, &ink), "pen_create")) return report;
    static_cast<void>(handles.keep(ink));
    if (!steps.check(api.pen_set_line_join(ink, GD_LINE_JOIN_ROUND, 4.0), "pen_set_line_join")) return report;
    if (!steps.check(api.solid_brush_create(argb(0xFF4678BEU), &bar_brush), "solid_brush_create")) return report;
    static_cast<void>(handles.keep(bar_brush));
    if (!steps.check(api.solid_brush_create(argb(0x60C85028U), &band_brush), "solid_brush_create")) return report;
    static_cast<void>(handles.keep(band_brush));

    const gd_rect page{0.0, 0.0, width, height};
    const gd_rect plot{48.0, 16.0, width - 64.0, height - 48.0};
    if (!steps.check(api.recorder_fill_rectangle(recorder, paper, page), "recorder_fill_rectangle")) return report;
    if (!steps.check(api.recorder_fill_rectangle(recorder, area, plot), "recorder_fill_rectangle")) return report;

    // Grid: ten divisions each way in one segments batch.
    std::vector<gd_point> segments{};
    segments.reserve(44U);
    for (std::size_t index = 0U; index <= 10U; ++index) {
        const double share = static_cast<double>(index) / 10.0;
        const double x = plot.x + share * plot.width;
        const double y = plot.y + share * plot.height;
        segments.push_back(gd_point{x, plot.y});
        segments.push_back(gd_point{x, plot.y + plot.height});
        segments.push_back(gd_point{plot.x, y});
        segments.push_back(gd_point{plot.x + plot.width, y});
    }
    if (!steps.check(api.recorder_draw_line_segments(recorder, grid, segments.data(), segments.size()),
                     "recorder_draw_line_segments")) {
        return report;
    }

    if (input.line != nullptr && (*input.line).size() >= 2U) {
        const XYSeries& line = *input.line;
        const DeviceMap map = make_map(line, plot);
        // The band under the line, closed by hand as one polygon (G2):
        // the line's points, then the baseline back.
        std::vector<gd_point> band{};
        band.reserve(line.size() + 2U);
        std::vector<gd_point> piece{};
        piece.reserve(line.size());
        for (std::size_t index = 0U; index < line.size(); ++index) {
            if (!std::isfinite(line.ys[index])) continue;
            band.push_back(map.at(line.xs[index], line.ys[index]));
        }
        if (band.size() >= 2U) {
            const double baseline = plot.y + plot.height;
            const gd_point last{band.back().x, baseline};
            const gd_point first{band.front().x, baseline};
            band.push_back(last);
            band.push_back(first);
            if (!steps.check(api.recorder_fill_polygon(recorder, band_brush, band.data(), band.size(), GD_FILL_WINDING),
                             "recorder_fill_polygon")) {
                return report;
            }
        }
        // The line, split at NaN gaps (G3): draw_lines takes one unbroken
        // polyline of two or more points.
        for (std::size_t index = 0U; index <= line.size(); ++index) {
            const bool gap = index == line.size() || !std::isfinite(line.ys[index]);
            if (!gap) {
                piece.push_back(map.at(line.xs[index], line.ys[index]));
                continue;
            }
            if (piece.size() >= 2U) {
                if (!steps.check(api.recorder_draw_lines(recorder, ink, piece.data(), piece.size()),
                                 "recorder_draw_lines")) {
                    return report;
                }
                ++report.line_pieces;
            }
            piece.clear();
        }
        if (input.scatter != nullptr && (*input.scatter).size() > 0U) {
            const XYSeries& scatter = *input.scatter;
            std::vector<gd_point> centres{};
            centres.reserve(scatter.size());
            for (std::size_t index = 0U; index < scatter.size(); ++index) {
                centres.push_back(map.at(scatter.xs[index], scatter.ys[index]));
            }
            gd_marker_style marker{};
            marker.shape = GD_MARKER_CIRCLE;
            marker.size = 4.0;
            marker.fill = argb(0xC0287AB4U);
            marker.edge = argb(0x00000000U);
            marker.edge.is_empty = 1U;
            marker.edge_width = 0.0;
            if (!steps.check(api.recorder_draw_markers(recorder, centres.data(), centres.size(), &marker),
                             "recorder_draw_markers")) {
                return report;
            }
        }
    }

    if (!input.bars.empty()) {
        // Bars along the bottom fifth, one rectangles batch.
        std::vector<gd_rect> rects{};
        rects.reserve(input.bars.size());
        const ValueExtent extent = finite_extent(input.bars);
        const double top = std::max(extent.maximum, 0.0);
        const double slot = plot.width / static_cast<double>(input.bars.size());
        const double band_height = plot.height * 0.2;
        for (std::size_t index = 0U; index < input.bars.size(); ++index) {
            const double value = std::isfinite(input.bars[index]) ? std::max(input.bars[index], 0.0) : 0.0;
            const double bar_height = top > 0.0 ? band_height * value / top : 0.0;
            rects.push_back(gd_rect{plot.x + slot * (static_cast<double>(index) + 0.15),
                                    plot.y + plot.height - bar_height, slot * 0.7, bar_height});
        }
        if (!steps.check(api.recorder_fill_rectangles(recorder, bar_brush, rects.data(), rects.size()),
                         "recorder_fill_rectangles")) {
            return report;
        }
    }

    if (!input.heatmap.empty() && input.heatmap_rows > 0U && input.heatmap_columns > 0U &&
        input.heatmap.size() == static_cast<std::size_t>(input.heatmap_rows) * input.heatmap_columns) {
        // Per-cell colours are not an ABI batch (G4): the cells become a
        // bitmap's pixels, drawn scaled.
        gd_handle cells{};
        if (!steps.check(api.bitmap_create(input.heatmap_columns, input.heatmap_rows, GD_PIXEL_BGRA32_PREMULTIPLIED, &cells),
                         "bitmap_create")) {
            return report;
        }
        static_cast<void>(handles.keep(cells));
        gd_bitmap_lock_view view{};
        if (!steps.check(api.bitmap_lock(cells, GD_BITMAP_LOCK_WRITE, &view), "bitmap_lock")) return report;
        std::uint8_t* const bytes = static_cast<std::uint8_t*>(view.writable_data);
        for (std::uint32_t row = 0U; row < input.heatmap_rows; ++row) {
            std::uint8_t* const line = bytes + static_cast<std::size_t>(row) * view.row_bytes;
            for (std::uint32_t column = 0U; column < input.heatmap_columns; ++column) {
                const double value = std::clamp(input.heatmap[static_cast<std::size_t>(row) * input.heatmap_columns + column], 0.0, 1.0);
                // A blue-to-yellow ramp; opaque, so premultiplication is
                // the identity.
                line[column * 4U + 0U] = static_cast<std::uint8_t>(200.0 * (1.0 - value));
                line[column * 4U + 1U] = static_cast<std::uint8_t>(40.0 + 180.0 * value);
                line[column * 4U + 2U] = static_cast<std::uint8_t>(250.0 * value);
                line[column * 4U + 3U] = 255U;
            }
        }
        if (!steps.check(api.bitmap_unlock(cells, view.token), "bitmap_unlock")) return report;
        const gd_rect destination{plot.x + plot.width * 0.6, plot.y + 8.0, plot.width * 0.38, plot.height * 0.38};
        const gd_rect source{0.0, 0.0, static_cast<double>(input.heatmap_columns), static_cast<double>(input.heatmap_rows)};
        const gd_handle no_attributes{};
        if (!steps.check(api.recorder_draw_bitmap(recorder, cells, destination, source, no_attributes),
                         "recorder_draw_bitmap")) {
            return report;
        }
    }

    if (!steps.check(api.recorder_command_count(recorder, &report.commands_recorded), "recorder_command_count")) {
        return report;
    }
    gd_handle bitmap{};
    if (!steps.check(api.bitmap_create(input.width, input.height, GD_PIXEL_BGRA32_PREMULTIPLIED, &bitmap), "bitmap_create")) {
        return report;
    }
    static_cast<void>(handles.keep(bitmap));
    if (!steps.check(api.recorder_execute(recorder, bitmap, &report.commands_executed), "recorder_execute")) return report;
    std::uint64_t required = 0U;
    const gd_result sized = api.bitmap_encode_png(bitmap, nullptr, 0U, &required);
    if (sized != GD_ERROR_BUFFER_TOO_SMALL && !steps.check(sized, "bitmap_encode_png")) return report;
    std::vector<std::uint8_t> png(static_cast<std::size_t>(required));
    if (!steps.check(api.bitmap_encode_png(bitmap, png.data(), png.size(), &required), "bitmap_encode_png")) return report;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        report.status = GdExportStatus::file_failed;
        return report;
    }
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file) {
        report.status = GdExportStatus::file_failed;
        return report;
    }
    report.png_bytes = png.size();
    report.status = GdExportStatus::written;
    return report;
}

#endif

} // namespace implot_class
