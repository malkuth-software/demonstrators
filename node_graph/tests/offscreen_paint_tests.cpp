// The canvas painted through GUI.Forms' offscreen CPU raster (the hosts'
// raster, with the bundled fonts): a seeded stress graph framed whole
// (below full detail), then at zoom 1 about one of its nodes (full detail)
// in both link styles, titles laid out once and then taken from the cache,
// the visible nodes published as semantics, and the same seed painting the
// same pixels twice. Needs GUIForms::Application.

#include "../src/generator.hpp"
#include "../src/graph_root.hpp"
#include "test_support.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>
#include <gui_forms/window/window.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <vector>

namespace gf = gui_forms;
using node_graph::GraphRoot;
using node_graph_tests::require;

namespace {

struct PaintResult final {
    std::optional<gf::WindowCapture> capture{};
    node_graph::CanvasStatistics statistics{};
    node_graph::LevelOfDetail framed_level{node_graph::LevelOfDetail::full};
    std::size_t semantic_children{};
};

PaintResult paint_once(gf::Application& application, gf::OffscreenRaster& raster) {
    std::shared_ptr<GraphRoot> root = gf::make_control<GraphRoot>(gf::StableId("node-graph"));
    PaintResult result{};
    {
        gf::Window window(application, root, gf::Size{1024.0, 720.0});
        gf::OffscreenAttachment attached(raster, window);
        node_graph::GeneratorOptions options{};
        // Small enough that the routed frame (the router routes the whole
        // graph) keeps the test bounded; large enough to frame below full detail.
        options.nodes = 120U;
        options.seed = 17U;
        static_cast<void>(node_graph::generate_stress_graph((*root).document(), options));
        (*root).canvas().document_changed();
        window.perform_layout();
        (*root).canvas().frame_all();
        gf::OffscreenCaptureOptions capture_options{};
        // Framed whole (below full detail), then in to full detail, both link
        // styles.
        static_cast<void>(attached.capture_window(capture_options));
        result.framed_level = (*root).canvas().statistics().level;
        // Zoom 1 about the first node's centre, which the framed view shows,
        // then centre it: zooming about a corner would show only the frame's
        // margin, with no node and so no title to lay out.
        node_graph::NodeCanvasView& canvas = (*root).canvas();
        const gf::NodeRecord* first = nullptr;
        for (const gf::NodeRecord& node : (*root).document().graph().node_slots()) {
            if (node.live) {
                first = &node;
                break;
            }
        }
        require(first != nullptr, "the generated graph has a node");
        const gf::WorldPoint centre{(*first).bounds.x + (*first).bounds.width / 2.0,
                                    (*first).bounds.y + (*first).bounds.height / 2.0};
        const gf::Point local = canvas.view().to_view(centre);
        canvas.zoom_about(local, 1.0 / canvas.view().zoom);
        const gf::Rect client = canvas.client_rectangle();
        canvas.pan_by(gf::Point{client.width / 2.0 - local.x, client.height / 2.0 - local.y});
        static_cast<void>(attached.capture_window(capture_options));
        (*root).canvas().set_link_style(node_graph::LinkStyle::routed);
        static_cast<void>(attached.capture_window(capture_options));
        (*root).canvas().set_link_style(node_graph::LinkStyle::soft);
        result.capture = attached.capture_window(capture_options);
        result.statistics = (*root).canvas().statistics();
        result.semantic_children = (*root).canvas().semantic_virtual_children().size();
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
        require(first.capture.has_value() && !(*first.capture).bgra.empty(), "the window is captured");
        require(first.statistics.frames_painted >= 4U, "each capture painted the canvas");
        require(first.statistics.nodes_painted > 0U && first.statistics.links_painted > 0U, "nodes and links are painted");
        require(first.framed_level != node_graph::LevelOfDetail::full, "the framed graph is drawn below full detail");
        require(first.statistics.level == node_graph::LevelOfDetail::full, "zoom 1 is full detail");
        require(first.statistics.level_transitions > 0U, "the zoom crossed levels of detail");
        require(first.statistics.last_frame_nodes > 0U, "nodes are in view at zoom 1");
        require(first.statistics.title_layouts_failed == 0U, "no title layout was refused");
        require(first.statistics.titles_laid_out > 0U, "titles are laid out as paragraphs");
        require(first.statistics.title_cache_hits > 0U, "unchanged titles come from the cache");
        require(first.statistics.router_runs >= 1U, "the routed style ran the router");
        require(first.semantic_children > 0U, "the visible nodes are published as semantics");

        const PaintResult second = paint_once(*created.application, *raster);
        require(second.capture.has_value() && (*second.capture).bgra == (*first.capture).bgra,
                "the same seed paints the same pixels");
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "offscreen_paint_tests: %s\n", failure.what());
        return 1;
    }
    std::printf("offscreen_paint_tests: ok\n");
    return 0;
}
