#pragma once

// The ImPlot-class window: a GUI.Forms Window whose root is PlotRoot.
// Application::run owns the window; the window holds the root.

#include "options.hpp"
#include "plot_root.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/window/window.hpp>

#include <memory>

namespace implot_class {

class PlotWindow final : public gui_forms::Window {
public:
    PlotWindow(gui_forms::Application& application, const AppOptions& options);
    ~PlotWindow() override;

    [[nodiscard]] PlotRoot& root() const noexcept { return *root_; }

private:
    PlotWindow(gui_forms::Application& application, std::shared_ptr<PlotRoot> root, const AppOptions& options);

    std::shared_ptr<PlotRoot> root_;
};

} // namespace implot_class
