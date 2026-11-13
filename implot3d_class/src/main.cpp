// implot3d-class: the ImPlot3D-class plotting demonstrator on GUI.Forms'
// public API (D5). With --measure it runs one workload headless and writes
// its record (docs/WORKLOADS.md); otherwise it opens the window.

#include "options.hpp"
#include "plot3d_window.hpp"
#include "workloads.hpp"

#include <gui_forms/application.hpp>

#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

namespace gf = gui_forms;

namespace {

int run_window(gf::Application& application, const implot3d_class::AppOptions& options) {
    std::unique_ptr<implot3d_class::Plot3DWindow> window =
        std::make_unique<implot3d_class::Plot3DWindow>(application, options);
    gf::ApplicationWindowOptions window_options{};
    window_options.title = "ImPlot3D-class plots";
    window_options.initial_size = gf::Size{options.width, options.height};
    window_options.minimum_size = gf::Size{640.0, 420.0};
    const gf::ApplicationResult result = application.run(std::move(window), std::move(window_options));
    const int code = result.accepted() ? 0 : 1;
    return code;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const implot3d_class::OptionsResult parsed = implot3d_class::parse_options(argc, argv);
        if (!parsed.error.empty()) {
            std::fprintf(stderr, "implot3d-class: %s\n%s", parsed.error.c_str(), implot3d_class::usage_text());
            return 2;
        }
        const implot3d_class::AppOptions& options = parsed.options;
        if (options.help) {
            std::fputs(implot3d_class::usage_text(), stdout);
            return 0;
        }
        std::optional<implot3d_class::Workload> workload{};
        if (options.measure.has_value()) {
            workload = implot3d_class::workload_named(*options.measure);
            if (!workload.has_value()) {
                std::fprintf(stderr, "implot3d-class: unknown workload %s\n%s", (*options.measure).c_str(),
                             implot3d_class::usage_text());
                return 2;
            }
        }
        gf::ApplicationConfig config{};
        config.theme.base = gf::ThemeBase::gui_forms;
        gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
        if (!created) {
            std::fprintf(stderr, "implot3d-class: the application could not be made\n");
            return 1;
        }
        int code = 0;
        {
            gf::Application& application = *created.application;
            if (workload.has_value()) {
                code = implot3d_class::run_workload(application, options, *workload);
            } else {
                code = run_window(application, options);
            }
        }
        // Once, after the last run (as GUI.Forms' reference application does).
        const gf::ApplicationResult ended = gf::Application::shutdown();
        if (!ended.accepted() && code == 0) code = 1;
        return code;
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "implot3d-class: %s\n", failure.what());
        return 1;
    }
}
