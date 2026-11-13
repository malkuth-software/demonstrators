// The viewer painted through GUI.Forms' offscreen CPU raster (the hosts'
// raster, with the bundled fonts): a generated trace drawn as an icicle
// and as a flame graph (the captures differ), cells, labels, timeline bars
// and table rows drawn, the search's highlight changing the pixels, a zoom
// bounding the cells drawn, and the same seed painting the same pixels
// twice. Needs GUIForms::Application.

#include "../src/profiler_root.hpp"
#include "test_support.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>
#include <gui_forms/window/window.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>

namespace gf = gui_forms;
using flame_graph::FlameOrientation;
using flame_graph::ProfilerRoot;
using flame_graph_tests::require;

namespace {

struct PaintResult final {
    std::optional<gf::WindowCapture> icicle{};
    std::optional<gf::WindowCapture> flame{};
    std::optional<gf::WindowCapture> searched{};
    flame_graph::FlameViewStatistics flame_statistics{};
    flame_graph::TimelineStatistics timeline_statistics{};
    flame_graph::DetailsTableStatistics table_statistics{};
    std::uint64_t zoomed_cells{};
    std::uint64_t zoomed_bound{};
};

PaintResult paint_once(gf::Application& application, gf::OffscreenRaster& raster) {
    std::shared_ptr<ProfilerRoot> root = gf::make_control<ProfilerRoot>(gf::StableId("flame-graph"));
    flame_graph::GeneratorOptions options{};
    options.seed = 5U;
    options.threads = 3U;
    options.spans_per_thread = 3000U;
    options.names = 120U;
    (*root).generate(options);
    PaintResult result{};
    {
        gf::Window window(application, root, gf::Size{1280.0, 860.0});
        gf::OffscreenAttachment attached(raster, window);
        gf::OffscreenCaptureOptions capture_options{};
        window.perform_layout();
        result.icicle = attached.capture_window(capture_options);
        (*root).set_orientation(FlameOrientation::flame);
        window.perform_layout();
        result.flame = attached.capture_window(capture_options);
        (*root).set_search("render");
        window.perform_layout();
        result.searched = attached.capture_window(capture_options);
        (*root).set_search("");
        // Zoomed to the heaviest child of the root: the cells drawn stay
        // within rows times width.
        const flame_graph::TraceModel& model = (*root).model();
        std::uint32_t heaviest = 0U;
        for (const std::uint32_t child : model.children(flame_graph::root_node)) {
            if (heaviest == 0U || model.node(flame_graph::CallNodeIndex{child}).total >
                                      model.node(flame_graph::CallNodeIndex{heaviest}).total) {
                heaviest = child;
            }
        }
        static_cast<void>((*root).flame().zoom_to(flame_graph::CallNodeIndex{heaviest}));
        window.perform_layout();
        static_cast<void>(attached.capture_window(capture_options));
        result.zoomed_cells = (*root).flame().statistics().last_frame_cells;
        result.zoomed_bound = static_cast<std::uint64_t>((*root).flame().visible_rows()) *
                              static_cast<std::uint64_t>((*root).flame().plot_rect().width + 2.0);
        result.flame_statistics = (*root).flame().statistics();
        result.timeline_statistics = (*root).timeline().statistics();
        result.table_statistics = (*root).table().statistics();
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
        require(first.icicle.has_value() && !(*first.icicle).bgra.empty(), "the icicle is captured");
        require(first.flame.has_value() && (*first.flame).bgra != (*first.icicle).bgra,
                "the flame graph differs from the icicle");
        require(first.searched.has_value() && (*first.searched).bgra != (*first.flame).bgra,
                "the search highlight changes the pixels");
        require(first.flame_statistics.cells_drawn > 0U && first.flame_statistics.labels_drawn > 0U,
                "the flame graph drew cells and labels");
        require(first.flame_statistics.cells_pruned_narrow > 0U, "narrow subtrees were pruned at full range");
        require(first.timeline_statistics.bars_drawn > 0U && first.timeline_statistics.tick_generations > 0U,
                "the timeline drew bars and its ruler");
        require(first.table_statistics.rows_painted > 0U, "the table drew rows");
        require(first.zoomed_cells > 0U && first.zoomed_cells <= first.zoomed_bound,
                "a zoomed frame stays within rows times width");

        const PaintResult second = paint_once(*created.application, *raster);
        require(second.icicle.has_value() && (*second.icicle).bgra == (*first.icicle).bgra,
                "the same seed paints the same pixels");
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "offscreen_paint_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("offscreen_paint_tests: passed");
    return 0;
}
