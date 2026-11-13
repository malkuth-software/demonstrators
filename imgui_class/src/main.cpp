// imgui-class: the ImGui-class widget demonstrator on GUI.Forms' public
// API (D3). With --measure it runs one workload headless and writes its
// record (docs/WORKLOADS.md); otherwise it opens the window.

#include "demo_window.hpp"
#include "options.hpp"
#include "workloads.hpp"

#include <gui_forms/application.hpp>

#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

namespace gf = gui_forms;

namespace {

int run_window(gf::Application& application, const imgui_class::AppOptions& options) {
    std::unique_ptr<imgui_class::DemoWindow> window = std::make_unique<imgui_class::DemoWindow>(application, options);
    gf::ApplicationWindowOptions window_options{};
    window_options.title = "ImGui-class widgets";
    window_options.initial_size = gf::Size{options.width, options.height};
    window_options.minimum_size = gf::Size{640.0, 420.0};
    const gf::ApplicationResult result = application.run(std::move(window), std::move(window_options));
    const int code = result.accepted() ? 0 : 1;
    return code;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const imgui_class::OptionsResult parsed = imgui_class::parse_options(argc, argv);
        if (!parsed.error.empty()) {
            std::fprintf(stderr, "imgui-class: %s\n%s", parsed.error.c_str(), imgui_class::usage_text());
            return 2;
        }
        const imgui_class::AppOptions& options = parsed.options;
        if (options.help) {
            std::fputs(imgui_class::usage_text(), stdout);
            return 0;
        }
        std::optional<imgui_class::Workload> workload{};
        if (options.measure.has_value()) {
            workload = imgui_class::workload_named(*options.measure);
            if (!workload.has_value()) {
                std::fprintf(stderr, "imgui-class: unknown workload %s\n%s", (*options.measure).c_str(),
                             imgui_class::usage_text());
                return 2;
            }
        }
        gf::ApplicationConfig config{};
        config.theme.base = gf::ThemeBase::gui_forms;
        gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
        if (!created) {
            std::fprintf(stderr, "imgui-class: the application could not be made\n");
            return 1;
        }
        int code = 0;
        {
            gf::Application& application = *created.application;
            if (workload.has_value()) {
                code = imgui_class::run_workload(application, options, *workload);
            } else {
                code = run_window(application, options);
            }
        }
        // Once, after the last run (as GUI.Forms' reference application does).
        const gf::ApplicationResult ended = gf::Application::shutdown();
        if (!ended.accepted() && code == 0) code = 1;
        return code;
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "imgui-class: %s\n", failure.what());
        return 1;
    }
}
