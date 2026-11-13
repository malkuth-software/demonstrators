// The plots painted through GUI.Forms' offscreen CPU raster (the hosts'
// raster, with the bundled fonts): every page shown and painted, the
// custom heatmap and histogram drawing their marks, a 1,000,000-point
// line panned, the live plot fed and its sources withdrawn when hidden,
// the linked subplots following one another, and the same build painting
// the same pixels twice. Needs GUIForms::Application.

#include "../src/plot_root.hpp"
#include "test_support.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>
#include <gui_forms/window/window.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <vector>

namespace gf = gui_forms;
namespace gp = gui_forms::plots;
using implot_class::PlotPage;
using implot_class::PlotRoot;
using implot_class_tests::require;

namespace {

struct PaintResult final {
    std::optional<gf::WindowCapture> basics{};
    implot_class::PlotFrameStatistics heatmap{};
    implot_class::PlotFrameStatistics histogram{};
    implot_class::PlotFrameStatistics drag{};
    implot_class::PlotRootCounters counters{};
    gp::SeriesChannelStatistics channel{};
    std::uint64_t live_frames{};
    std::uint64_t propagations{};
    bool linked{};
    bool panned{};
};

bool same_range(gp::PlotRange left, gp::PlotRange right) {
    const double tolerance = 1e-9 * (std::fabs(left.extent()) + 1.0);
    const bool same = std::fabs(left.minimum - right.minimum) <= tolerance &&
                      std::fabs(left.maximum - right.maximum) <= tolerance;
    return same;
}

PaintResult paint_once(gf::Application& application, gf::OffscreenRaster& raster) {
    implot_class::PlotRootOptions options{};
    options.large_points = 1'000'000U;
    options.start_feed = false;
    std::shared_ptr<PlotRoot> root = gf::make_control<PlotRoot>(gf::StableId("implot"), options);
    PaintResult result{};
    {
        gf::Window window(application, root, gf::Size{1280.0, 820.0});
        gf::OffscreenAttachment attached(raster, window);
        gf::OffscreenCaptureOptions capture_options{};
        window.perform_layout();
        result.basics = attached.capture_window(capture_options);
        for (std::size_t page = 1U; page < implot_class::plot_page_count; ++page) {
            (*root).select_page(static_cast<PlotPage>(page));
            window.perform_layout();
            static_cast<void>(attached.capture_window(capture_options));
        }
        // Large: pan a tenth-wide window to the end.
        (*root).select_page(PlotPage::large);
        window.perform_layout();
        const double count = static_cast<double>((*root).large_points());
        result.panned = (*root).large_line().set_x_range(0U, gp::PlotRange{count * 0.9, count});
        static_cast<void>(attached.capture_window(capture_options));
        // Live: write blocks and run ambient frames at fixed times.
        (*root).select_page(PlotPage::live);
        window.perform_layout();
        gf::PlotView& live = (*root).live_plot();
        const gf::FrameTime start = gf::FrameClock::now();
        for (std::size_t step = 0U; step < 30U; ++step) {
            static_cast<void>((*root).feed().write_blocks((*root).live_channel(), 1U));
            static_cast<void>(live.on_ambient_frame(start + std::chrono::milliseconds(16 * static_cast<std::int64_t>(step + 1U))));
            static_cast<void>(attached.capture_window(capture_options));
        }
        result.channel = (*(*root).live_channel()).statistics();
        result.live_frames = live.frames_run();
        // Subplots: one plot's x range reaches the others.
        (*root).select_page(PlotPage::subplots);
        window.perform_layout();
        implot_class::LinkedPlotGrid& grid = (*root).subplots();
        static_cast<void>(grid.plot(1U).set_x_range(0U, gp::PlotRange{1.0, 3.0}));
        static_cast<void>(attached.capture_window(capture_options));
        result.linked = true;
        for (std::size_t index = 0U; index < implot_class::LinkedPlotGrid::plot_count; ++index) {
            if (!same_range(grid.plot(index).x_range(0U), gp::PlotRange{1.0, 3.0})) result.linked = false;
        }
        result.propagations = grid.propagations();
        (*root).select_page(PlotPage::basics);
        window.perform_layout();
        static_cast<void>(attached.capture_window(capture_options));
        result.heatmap = (*root).heatmap().statistics();
        result.histogram = (*root).histogram().statistics();
        result.drag = (*root).drag().statistics();
        result.counters = (*root).counters();
    }
    return result;
}

} // namespace

int main() {
    try {
        gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
        require(static_cast<bool>(created), "the application is made");
        gf::OffscreenRasterOptions raster_options{};
        const char* font_dir = std::getenv("GUI_FORMS_FONT_DIR");
        if (font_dir != nullptr) raster_options.font_directory = font_dir;
        gf::OffscreenRasterCreateResult made = gf::OffscreenRaster::create(raster_options);
        std::unique_ptr<gf::OffscreenRaster> raster = made.take_raster();
        require(raster != nullptr, "the offscreen raster is made (set GUI_FORMS_FONT_DIR to the bundled fonts)");

        const PaintResult first = paint_once(*created.application, *raster);
        require(first.basics.has_value() && !(*first.basics).bgra.empty(), "the Basics page is captured");
        require(first.heatmap.frames_painted > 0U && first.heatmap.points_drawn > 0U, "the heatmap painted its cells");
        require(first.histogram.frames_painted > 0U && first.histogram.points_drawn > 0U, "the histogram painted its bins");
        require(first.drag.frames_painted > 0U, "the drag plot painted");
        require(first.panned, "the large line took the pan range");
        require(first.channel.consumed > 0U && first.channel.overrun == 0U, "the live plot consumed the feed");
        require(first.live_frames > 0U, "the live plot ran ambient frames");
        require(first.linked && first.propagations > 0U, "the subplots follow one another");
        require(first.counters.sources_withdrawn >= 2U && first.counters.sources_restored >= 1U,
                "the Live page's sources follow its visibility");

        const PaintResult second = paint_once(*created.application, *raster);
        require(second.basics.has_value() && (*second.basics).bgra == (*first.basics).bgra,
                "the same build paints the same pixels");
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "offscreen_paint_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("offscreen_paint_tests: passed");
    return 0;
}
