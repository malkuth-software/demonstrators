// chat-stress: a chat log of up to a billion generated messages on
// GUI.Forms' public API (CHAT_STRESS_SPEC). Without --measure it opens the
// window for use; with --measure it runs one workload and writes its record
// (docs/WORKLOADS.md).

#include "chat_window.hpp"
#include "corpus.hpp"
#include "options.hpp"
#include "setup_collection.hpp"
#include "workload_driver.hpp"

#include <gui_forms/application.hpp>

#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string>

namespace gf = gui_forms;

namespace {

int run_window(gf::Application& application, const chat_stress::AppOptions& options,
               std::optional<chat_stress::Workload> workload) {
    // Declared before the window, which borrows it and ends inside run.
    chat_stress::Corpus corpus(options.corpus);
    std::unique_ptr<chat_stress::ChatWindow> window =
        std::make_unique<chat_stress::ChatWindow>(application, corpus, options, workload);
    gf::ApplicationWindowOptions window_options{};
    window_options.title = "Chat Stress";
    window_options.initial_size = gf::Size{options.width, options.height};
    window_options.minimum_size = gf::Size{480.0, 320.0};
    // The window lives until run returns, so the pointer stays valid for
    // every call of the ready target.
    window_options.ready = chat_stress::ChatWindowReady{window.get()};
    const gf::ApplicationResult result = application.run(std::move(window), std::move(window_options));
    const int code = result.accepted() ? 0 : 1;
    return code;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const chat_stress::OptionsResult parsed = chat_stress::parse_options(argc, argv);
        if (!parsed.error.empty()) {
            std::fprintf(stderr, "chat-stress: %s\n%s", parsed.error.c_str(), chat_stress::usage_text());
            return 2;
        }
        const chat_stress::AppOptions& options = parsed.options;
        if (options.help) {
            std::fputs(chat_stress::usage_text(), stdout);
            return 0;
        }
        std::optional<chat_stress::Workload> workload{};
        const bool setup = options.measure.has_value() && *options.measure == "setup";
        if (options.measure.has_value() && !setup) {
            workload = chat_stress::workload_named(*options.measure);
            if (!workload.has_value()) {
                std::fprintf(stderr, "chat-stress: unknown workload %s\n%s", (*options.measure).c_str(),
                             chat_stress::usage_text());
                return 2;
            }
        }
        gf::ApplicationConfig config{};
        config.theme.base = gf::ThemeBase::gui_forms;
        gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
        if (!created) {
            std::fprintf(stderr, "chat-stress: the application could not be made\n");
            return 1;
        }
        int code = 0;
        {
            gf::Application& application = *created.application;
            if (setup) {
                code = chat_stress::collect_setup(options, application);
            } else {
                code = run_window(application, options, workload);
            }
        }
        // Once, after the last run (as GUI.Forms' reference application does).
        const gf::ApplicationResult ended = gf::Application::shutdown();
        if (!ended.accepted() && code == 0) code = 1;
        return code;
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "chat-stress: %s\n", failure.what());
        return 1;
    }
}
