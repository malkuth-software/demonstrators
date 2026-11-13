#include "profiler_window.hpp"

#include <cstdio>
#include <utility>

namespace flame_graph {

namespace gf = gui_forms;

GeneratorOptions generator_options(const AppOptions& options) noexcept {
    GeneratorOptions generator{};
    generator.seed = options.seed;
    generator.threads = options.threads;
    generator.spans_per_thread = options.spans;
    generator.names = options.names;
    return generator;
}

std::shared_ptr<ProfilerRoot> make_profiler_root(const AppOptions& options) {
    std::shared_ptr<ProfilerRoot> root = gf::make_control<ProfilerRoot>(gf::StableId("flame-graph"));
    if (options.trace.has_value()) {
        const LoadReport report = (*root).load_file(*options.trace, options.format);
        if (!report.loaded) {
            std::fprintf(stderr, "flame-graph: %s: %s\n", (*options.trace).string().c_str(), report.message.c_str());
        }
    } else {
        (*root).generate(generator_options(options));
    }
    return root;
}

ProfilerWindow::ProfilerWindow(gf::Application& application, const AppOptions& options)
    : ProfilerWindow(application, make_profiler_root(options), options) {}

ProfilerWindow::ProfilerWindow(gf::Application& application, std::shared_ptr<ProfilerRoot> root,
                               const AppOptions& options)
    : gf::Window(application, root, gf::Size{options.width, options.height}), root_(std::move(root)) {}

ProfilerWindow::~ProfilerWindow() {}

} // namespace flame_graph
