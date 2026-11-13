// flame-graph: the flame graph and profiler viewer on GUI.Forms' public
// API (D2). With --measure it runs one workload headless and writes its
// record (docs/WORKLOADS.md); otherwise it opens the window.

#include "profiler_window.hpp"
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

int run_window(gf::Application& application, const flame_graph::AppOptions& options) {
    std::unique_ptr<flame_graph::ProfilerWindow> window = std::make_unique<flame_graph::ProfilerWindow>(application, options);
    gf::ApplicationWindowOptions window_options{};
    window_options.title = "Flame graph";
    window_options.initial_size = gf::Size{options.width, options.height};
    window_options.minimum_size = gf::Size{640.0, 420.0};
    const gf::ApplicationResult result = application.run(std::move(window), std::move(window_options));
    const int code = result.accepted() ? 0 : 1;
    return code;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const flame_graph::OptionsResult parsed = flame_graph::parse_options(argc, argv);
        if (!parsed.error.empty()) {
            std::fprintf(stderr, "flame-graph: %s\n%s", parsed.error.c_str(), flame_graph::usage_text());
            return 2;
        }
        const flame_graph::AppOptions& options = parsed.options;
        if (options.help) {
            std::fputs(flame_graph::usage_text(), stdout);
            return 0;
        }
        std::optional<flame_graph::Workload> workload{};
        if (options.measure.has_value()) {
            workload = flame_graph::workload_named(*options.measure);
            if (!workload.has_value()) {
                std::fprintf(stderr, "flame-graph: unknown workload %s\n%s", (*options.measure).c_str(),
                             flame_graph::usage_text());
                return 2;
            }
        }
        gf::ApplicationConfig config{};
        config.theme.base = gf::ThemeBase::gui_forms;
        gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
        if (!created) {
            std::fprintf(stderr, "flame-graph: the application could not be made\n");
            return 1;
        }
        int code = 0;
        {
            gf::Application& application = *created.application;
            if (workload.has_value()) {
                code = flame_graph::run_workload(application, options, *workload);
            } else {
                code = run_window(application, options);
            }
        }
        // Once, after the last run (as GUI.Forms' reference application does).
        const gf::ApplicationResult ended = gf::Application::shutdown();
        if (!ended.accepted() && code == 0) code = 1;
        return code;
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "flame-graph: %s\n", failure.what());
        return 1;
    }
}
