#include "plot_window.hpp"

#include <utility>

namespace implot_class {

namespace gf = gui_forms;

namespace {

std::shared_ptr<PlotRoot> make_root(const AppOptions& options) {
    PlotRootOptions root_options{};
    root_options.large_points = options.large_points;
    root_options.start_feed = true;
    std::shared_ptr<PlotRoot> root = gf::make_control<PlotRoot>(gf::StableId("implot"), root_options);
    return root;
}

} // namespace

PlotWindow::PlotWindow(gf::Application& application, const AppOptions& options)
    : PlotWindow(application, make_root(options), options) {}

PlotWindow::PlotWindow(gf::Application& application, std::shared_ptr<PlotRoot> root, const AppOptions& options)
    : gf::Window(application, root, gf::Size{options.width, options.height}), root_(std::move(root)) {}

PlotWindow::~PlotWindow() {}

} // namespace implot_class
