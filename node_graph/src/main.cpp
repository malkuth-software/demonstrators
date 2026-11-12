// node-graph: a node graph editor on GUI.Forms' public API
// (NODE_GRAPH_SPEC). With FILE it opens that document; with --generate it
// writes a seeded stress graph and exits; with --measure it runs one
// workload headless and writes its record (docs/WORKLOADS.md); otherwise it
// opens an empty graph.

#include "generator.hpp"
#include "graph_document.hpp"
#include "graph_window.hpp"
#include "options.hpp"
#include "workloads.hpp"

#include <gui_forms/application.hpp>

#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string>

namespace gf = gui_forms;

namespace {

int generate_file(const node_graph::AppOptions& options) {
    node_graph::GraphDocument document{};
    node_graph::GeneratorOptions generator{};
    generator.nodes = *options.generate;
    generator.links_per_node = options.links_per_node;
    generator.seed = options.seed;
    const node_graph::GeneratorReport report = node_graph::generate_stress_graph(document, generator);
    const node_graph::FileResult saved = document.save_file(options.output);
    if (saved.status != node_graph::FileStatus::ok) {
        std::fprintf(stderr, "node-graph: could not write %s\n", options.output.string().c_str());
        return 1;
    }
    std::printf("node-graph: %zu nodes, %zu links written to %s\n", report.nodes, report.links,
                options.output.string().c_str());
    return 0;
}

int run_window(gf::Application& application, const node_graph::AppOptions& options) {
    std::unique_ptr<node_graph::GraphWindow> window = std::make_unique<node_graph::GraphWindow>(application, options);
    if (!options.file.empty()) {
        node_graph::LoadReport report{};
        const node_graph::FileResult loaded = (*window).root().document().load_file(options.file, report);
        if (loaded.status != node_graph::FileStatus::ok) {
            std::fprintf(stderr, "node-graph: could not open %s (line %zu, column %zu)\n",
                         options.file.string().c_str(), loaded.load.line, loaded.load.column);
            return 1;
        }
        (*window).root().canvas().document_changed();
    }
    gf::ApplicationWindowOptions window_options{};
    window_options.title = "Node Graph";
    window_options.initial_size = gf::Size{options.width, options.height};
    window_options.minimum_size = gf::Size{480.0, 320.0};
    // The window lives until run returns, so the pointer stays valid for
    // every call of the ready target.
    window_options.ready = node_graph::GraphWindowReady{window.get()};
    const gf::ApplicationResult result = application.run(std::move(window), std::move(window_options));
    const int code = result.accepted() ? 0 : 1;
    return code;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const node_graph::OptionsResult parsed = node_graph::parse_options(argc, argv);
        if (!parsed.error.empty()) {
            std::fprintf(stderr, "node-graph: %s\n%s", parsed.error.c_str(), node_graph::usage_text());
            return 2;
        }
        const node_graph::AppOptions& options = parsed.options;
        if (options.help) {
            std::fputs(node_graph::usage_text(), stdout);
            return 0;
        }
        if (options.generate.has_value()) return generate_file(options);
        std::optional<node_graph::Workload> workload{};
        if (options.measure.has_value()) {
            workload = node_graph::workload_named(*options.measure);
            if (!workload.has_value()) {
                std::fprintf(stderr, "node-graph: unknown workload %s\n%s", (*options.measure).c_str(),
                             node_graph::usage_text());
                return 2;
            }
        }
        gf::ApplicationConfig config{};
        config.theme.base = gf::ThemeBase::gui_forms;
        gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
        if (!created) {
            std::fprintf(stderr, "node-graph: the application could not be made\n");
            return 1;
        }
        int code = 0;
        {
            gf::Application& application = *created.application;
            if (workload.has_value()) {
                code = node_graph::run_workload(application, options, *workload);
            } else {
                code = run_window(application, options);
            }
        }
        // Once, after the last run (as GUI.Forms' reference application does).
        const gf::ApplicationResult ended = gf::Application::shutdown();
        if (!ended.accepted() && code == 0) code = 1;
        return code;
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "node-graph: %s\n", failure.what());
        return 1;
    }
}
