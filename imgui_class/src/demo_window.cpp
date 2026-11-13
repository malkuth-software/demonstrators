#include "demo_window.hpp"

#include <utility>

namespace imgui_class {

namespace gf = gui_forms;

namespace {

std::shared_ptr<DemoRoot> make_root(const AppOptions& options) {
    DemoRootOptions root_options{};
    root_options.rows = options.rows;
    root_options.seed = options.seed;
    std::shared_ptr<DemoRoot> root = gf::make_control<DemoRoot>(gf::StableId("imgui"), root_options);
    return root;
}

} // namespace

DemoWindow::DemoWindow(gf::Application& application, const AppOptions& options)
    : DemoWindow(application, make_root(options), options) {}

DemoWindow::DemoWindow(gf::Application& application, std::shared_ptr<DemoRoot> root, const AppOptions& options)
    : gf::Window(application, root, gf::Size{options.width, options.height}), root_(std::move(root)) {}

DemoWindow::~DemoWindow() {}

} // namespace imgui_class
