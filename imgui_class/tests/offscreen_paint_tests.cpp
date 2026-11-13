// The demo painted through GUI.Forms' offscreen CPU raster (the hosts'
// raster, with the bundled fonts): every page shown, the table's cell
// plots and value bars drawn by owner draw, a click reaching the counter
// button, a drag in the colour field, the generated theme sheet compiled
// and set as the preview's override, and the same seed painting the same
// pixels twice. Needs GUIForms::Application.

#include "../src/demo_root.hpp"
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

namespace gf = gui_forms;
using imgui_class::DemoPage;
using imgui_class::DemoRoot;
using imgui_class_tests::require;

namespace {

struct PaintResult final {
    std::optional<gf::WindowCapture> widgets{};
    imgui_class::DemoRootCounters counters{};
    imgui_class::Hsva dragged{};
    bool style_applied{};
};

void press(gf::Window& window, gf::PointerAction action, gf::Point at) {
    gf::PointerEvent event{};
    event.action = action;
    event.position = at;
    if (action != gf::PointerAction::move) event.button = gf::PointerButton::primary;
    static_cast<void>(window.dispatch_pointer(event));
}

PaintResult paint_once(gf::Application& application, gf::OffscreenRaster& raster) {
    imgui_class::DemoRootOptions options{};
    options.rows = 400U;
    options.seed = 3U;
    std::shared_ptr<DemoRoot> root = gf::make_control<DemoRoot>(gf::StableId("imgui"), options);
    PaintResult result{};
    {
        gf::Window window(application, root, gf::Size{1280.0, 820.0});
        gf::OffscreenAttachment attached(raster, window);
        gf::OffscreenCaptureOptions capture_options{};
        window.perform_layout();
        // No effect animates in a capture compared for equality.
        (*root).set_effect(0U);
        window.perform_layout();
        result.widgets = attached.capture_window(capture_options);
        const gf::Rect button = (*root).counter_button().client_rectangle();
        const gf::Point at = (*root).counter_button().point_to_window(gf::Point{button.width / 2.0, button.height / 2.0});
        press(window, gf::PointerAction::move, at);
        press(window, gf::PointerAction::down, at);
        press(window, gf::PointerAction::up, at);
        for (std::size_t page = 1U; page < imgui_class::demo_page_count; ++page) {
            (*root).select_page(static_cast<DemoPage>(page));
            window.perform_layout();
            static_cast<void>(attached.capture_window(capture_options));
        }
        (*root).select_page(DemoPage::tables);
        window.perform_layout();
        (*root).advance();
        static_cast<void>(attached.capture_window(capture_options));
        (*root).select_page(DemoPage::colour);
        window.perform_layout();
        imgui_class::ColorField& field = (*root).color_field();
        const gf::Rect square = field.square();
        press(window, gf::PointerAction::down, field.point_to_window(gf::Point{square.x + 1.0, square.y + 1.0}));
        press(window, gf::PointerAction::move, field.point_to_window(gf::Point{square.x + square.width * 0.5, square.y + 1.0}));
        press(window, gf::PointerAction::up, field.point_to_window(gf::Point{square.x + square.width * 0.5, square.y + 1.0}));
        static_cast<void>(attached.capture_window(capture_options));
        result.dragged = field.color();
        (*root).select_page(DemoPage::style);
        window.perform_layout();
        imgui_class::StyleChoice choice{};
        choice.accent_hue = 0.1;
        choice.corner_radius = 6.0;
        (*root).set_style(choice);
        static_cast<void>(attached.capture_window(capture_options));
        result.style_applied = (*root).style_applied();
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
        require(first.widgets.has_value() && !(*first.widgets).bgra.empty(), "the Widgets page is captured");
        require(first.counters.button_clicks == 1U, "the click reached the counter button");
        require(first.counters.page_switches >= imgui_class::demo_page_count - 1U, "every page was shown");
        require(first.counters.cell_plots_painted > 0U && first.counters.cell_bars_painted > 0U,
                "the table drew its cell plots and value bars");
        require(first.counters.advances == 1U, "the table advanced");
        require(first.dragged.value > 0.95 && first.dragged.saturation > 0.45 && first.dragged.saturation < 0.55,
                "the drag in the colour square set saturation and value");
        require(first.counters.style_compiles >= 1U && first.counters.style_refusals == 0U, "the generated sheet compiled");
        require(first.style_applied, "the preview took the theme override");

        const PaintResult second = paint_once(*created.application, *raster);
        require(second.widgets.has_value() && (*second.widgets).bgra == (*first.widgets).bgra,
                "the same seed paints the same pixels");
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "offscreen_paint_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("offscreen_paint_tests: passed");
    return 0;
}
