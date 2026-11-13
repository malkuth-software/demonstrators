// The demonstrator painted through GUI.Forms' offscreen CPU raster (the
// hosts' raster, with the bundled fonts): every scene drawn with triangles
// and labels, a drag turning the box, a hover naming the surface point in
// the status line, the kept picture not rendered again while nothing
// changes, the projection switch, and the same build painting the same
// pixels twice. Needs GUIForms::Application.

#include "../src/plot3d_root.hpp"
#include "test_support.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>
#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/window/window.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <string>

namespace gf = gui_forms;
namespace gp = gui_forms::plots;
using implot3d_class::Plot3DRoot;
using implot3d_class::SceneKind;
using implot3d_class_tests::require;

namespace {

struct PaintResult final {
    std::optional<gf::WindowCapture> first{};
    implot3d_class::Plot3DRootCounters counters{};
    bool every_scene_drew{};
    bool turned{};
    bool hovered_surface{};
    bool status_shows_readout{};
    bool kept_at_rest{};
    bool orthographic{};
};

void move(gf::Window& window, gf::PointerAction action, gf::Point position, gf::PointerButton button) {
    gf::PointerEvent event{};
    event.action = action;
    event.position = position;
    event.button = button;
    event.click_count = action == gf::PointerAction::move ? 0U : 1U;
    static_cast<void>(window.dispatch_pointer(event));
}

PaintResult paint_once(gf::Application& application, gf::OffscreenRaster& raster) {
    implot3d_class::Plot3DRootOptions options{};
    options.grid_side = 64U;
    options.cloud_points = 500U;
    std::shared_ptr<Plot3DRoot> root = gf::make_control<Plot3DRoot>(gf::StableId("implot3d"), options);
    PaintResult result{};
    {
        gf::Window window(application, root, gf::Size{960.0, 720.0});
        gf::OffscreenAttachment attached(raster, window);
        gf::OffscreenCaptureOptions capture_options{};
        window.perform_layout();
        result.first = attached.capture_window(capture_options);
        gf::Plot3DView& plot = (*root).plot();

        // Every scene draws: a picture, triangles where it has a surface,
        // and tick labels.
        result.every_scene_drew = plot.scene().triangles_drawn() > 0U && !plot.scene().labels().empty();
        for (std::size_t index = 1U; index < implot3d_class::scene_count; ++index) {
            const std::uint64_t before = plot.picture_renders();
            (*root).select_scene(static_cast<SceneKind>(index));
            window.perform_layout();
            static_cast<void>(attached.capture_window(capture_options));
            if (plot.picture_renders() == before) result.every_scene_drew = false;
            if ((*root).surface_items() > 0U && plot.scene().triangles_drawn() == 0U) result.every_scene_drew = false;
        }
        (*root).select_scene(SceneKind::peaks);
        window.perform_layout();
        static_cast<void>(attached.capture_window(capture_options));

        // A drag across turns the box.
        const gf::Rect bounds = plot.plot_bounds();
        const gf::Point centre = plot.point_to_window(gf::Point{bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0});
        const double azimuth = plot.camera().azimuth;
        move(window, gf::PointerAction::down, centre, gf::PointerButton::primary);
        move(window, gf::PointerAction::move, gf::Point{centre.x + 60.0, centre.y}, gf::PointerButton::primary);
        move(window, gf::PointerAction::move, gf::Point{centre.x + 120.0, centre.y}, gf::PointerButton::primary);
        move(window, gf::PointerAction::up, gf::Point{centre.x + 120.0, centre.y}, gf::PointerButton::primary);
        static_cast<void>(attached.capture_window(capture_options));
        result.turned = plot.camera().azimuth != azimuth;

        // Hovering the centre names the surface point in the status line.
        move(window, gf::PointerAction::move, centre, gf::PointerButton::none);
        static_cast<void>(attached.capture_window(capture_options));
        result.hovered_surface = plot.hover().kind == gp::Plot3DHitKind::surface;
        result.status_shows_readout = !plot.readout().empty() && (*root).status_text() == plot.readout();

        // Nothing changed: the kept picture is drawn again, not rendered.
        const std::uint64_t renders = plot.picture_renders();
        static_cast<void>(attached.capture_window(capture_options));
        static_cast<void>(attached.capture_window(capture_options));
        result.kept_at_rest = plot.picture_renders() == renders;

        (*root).set_orthographic(true);
        static_cast<void>(attached.capture_window(capture_options));
        result.orthographic = plot.camera().projection == gp::Plot3DProjection::orthographic;
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
        require(first.first.has_value() && !(*first.first).bgra.empty(), "the first frame is captured");
        require(first.every_scene_drew, "every scene drew its picture, triangles and labels");
        require(first.counters.items_refused == 0U, "no item was refused");
        require(first.turned, "a drag across turned the box");
        require(first.counters.camera_changes > 0U, "the drag raised camera_changed");
        require(first.hovered_surface, "the centre hovers the surface");
        require(first.status_shows_readout, "the status line shows the readout");
        require(first.kept_at_rest, "the picture is kept while nothing changes");
        require(first.orthographic, "the projection switches to orthographic");

        const PaintResult second = paint_once(*created.application, *raster);
        require(second.first.has_value() && (*second.first).bgra == (*first.first).bgra,
                "the same build paints the same pixels");
        static_cast<void>(gf::Application::shutdown());
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "offscreen_paint_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("offscreen_paint_tests: passed");
    return 0;
}
