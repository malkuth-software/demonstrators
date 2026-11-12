// The chat log painted through GUI.Forms' offscreen CPU raster (the same
// raster the hosts use, with the bundled fonts): rows are measured with the
// real text engine, painted, avatars come from the atlas, and the same seed
// paints the same pixels twice. Needs GUIForms::Application.

#include "../src/chat_view.hpp"
#include "test_support.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>
#include <gui_forms/window/window.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <string>

namespace gf = gui_forms;
using chat_stress::ChatRoot;
using chat_stress::Corpus;
using chat_stress::CorpusOptions;
using chat_stress_tests::require;

namespace {

std::optional<gf::WindowCapture> paint_once(gf::Application& application, gf::OffscreenRaster& raster,
                                            chat_stress::AtlasCounters& atlas,
                                            chat_stress::ChatRowsCounters& rows) {
    Corpus corpus(CorpusOptions{chat_stress::default_seed, 1'000'000U, 5'000U, false});
    std::shared_ptr<ChatRoot> root = gf::make_control<ChatRoot>(gf::StableId("chat-stress"), corpus);
    std::optional<gf::WindowCapture> capture{};
    {
        gf::Window window(application, root, gf::Size{960.0, 640.0});
        gf::OffscreenAttachment attached(raster, window);
        window.perform_layout();
        static_cast<void>((*root).go_to_message(chat_stress::MessageId{500'000U}));
        window.perform_layout();
        gf::OffscreenCaptureOptions options{};
        options.scale = 2.0;
        capture = attached.capture_window(options);
        atlas = (*root).atlas().counters();
        rows = (*root).rows().counters();
        (*root).atlas().release(window);
    }
    return capture;
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

        chat_stress::AtlasCounters first_atlas{};
        chat_stress::ChatRowsCounters first_rows{};
        const std::optional<gf::WindowCapture> first = paint_once(*created.application, *raster, first_atlas, first_rows);
        require(first.has_value() && !(*first).bgra.empty(), "the window is captured");
        require(first_rows.rows_painted > 0U && first_rows.lines_drawn > 0U, "rows and their lines are painted");
        require(first_atlas.uploads > 0U, "avatars are drawn into the atlas");
        require(first_atlas.paint_fallbacks == 0U, "every painted avatar came from the atlas");

        chat_stress::AtlasCounters second_atlas{};
        chat_stress::ChatRowsCounters second_rows{};
        const std::optional<gf::WindowCapture> second =
            paint_once(*created.application, *raster, second_atlas, second_rows);
        require(second.has_value() && (*second).bgra == (*first).bgra, "the same seed paints the same pixels");
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "offscreen_paint_tests: %s\n", failure.what());
        return 1;
    }
    std::printf("offscreen_paint_tests: ok\n");
    return 0;
}
