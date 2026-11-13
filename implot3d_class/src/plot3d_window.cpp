#include "plot3d_window.hpp"

#include <utility>

namespace implot3d_class {

namespace gf = gui_forms;

namespace {

std::shared_ptr<Plot3DRoot> make_root(const AppOptions& options) {
    Plot3DRootOptions root_options{};
    root_options.grid_side = options.grid_side;
    std::shared_ptr<Plot3DRoot> root = gf::make_control<Plot3DRoot>(gf::StableId("implot3d"), root_options);
    return root;
}

} // namespace

Plot3DWindow::Plot3DWindow(gf::Application& application, const AppOptions& options)
    : Plot3DWindow(application, make_root(options), options) {}

Plot3DWindow::Plot3DWindow(gf::Application& application, std::shared_ptr<Plot3DRoot> root, const AppOptions& options)
    : gf::Window(application, root, gf::Size{options.width, options.height}), root_(std::move(root)) {}

Plot3DWindow::~Plot3DWindow() {}

} // namespace implot3d_class
