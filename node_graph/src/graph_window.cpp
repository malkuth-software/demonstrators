#include "graph_window.hpp"

namespace node_graph {

namespace gf = gui_forms;

GraphWindow::GraphWindow(gf::Application& application, const AppOptions& options)
    : GraphWindow(application, gf::make_control<GraphRoot>(gf::StableId("node-graph")), options) {}

GraphWindow::GraphWindow(gf::Application& application, std::shared_ptr<GraphRoot> root, const AppOptions& options)
    : gf::Window(application, root, gf::Size{options.width, options.height}), root_(std::move(root)) {}

GraphWindow::~GraphWindow() {}

void GraphWindow::ready(gf::ApplicationWindowHandle handle) {
    static_cast<void>(handle);
    perform_layout();
    (*root_).canvas().frame_all();
    (*root_).refresh_status();
}

void GraphWindowReady::operator()(gf::Window& model, gf::ApplicationWindowHandle handle) const {
    static_cast<void>(model);
    if (window != nullptr) (*window).ready(handle);
}

} // namespace node_graph
