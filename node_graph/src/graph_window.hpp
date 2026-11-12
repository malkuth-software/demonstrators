#pragma once

// The node graph window: a GUI.Forms Window whose root is GraphRoot.
// Application::run owns the window; the window holds the root.

#include "graph_root.hpp"
#include "options.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/window/window.hpp>

#include <memory>

namespace node_graph {

class GraphWindow final : public gui_forms::Window {
public:
    GraphWindow(gui_forms::Application& application, const AppOptions& options);
    ~GraphWindow() override;

    [[nodiscard]] GraphRoot& root() const noexcept { return *root_; }
    // Frames the graph once the host has sized the window (the ready
    // callback calls it).
    void ready(gui_forms::ApplicationWindowHandle handle);

private:
    GraphWindow(gui_forms::Application& application, std::shared_ptr<GraphRoot> root, const AppOptions& options);

    std::shared_ptr<GraphRoot> root_;
};

// ApplicationWindowOptions::ready's target: a named function object with
// its context (the window), never a lambda.
struct GraphWindowReady final {
    GraphWindow* window{};
    void operator()(gui_forms::Window& model, gui_forms::ApplicationWindowHandle handle) const;
};

} // namespace node_graph
