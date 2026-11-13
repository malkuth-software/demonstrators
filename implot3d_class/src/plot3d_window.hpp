#pragma once

// The ImPlot3D-class window: a GUI.Forms Window whose root is Plot3DRoot.
// Application::run owns the window; the window holds the root.

#include "options.hpp"
#include "plot3d_root.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/window/window.hpp>

#include <memory>

namespace implot3d_class {

class Plot3DWindow final : public gui_forms::Window {
public:
    Plot3DWindow(gui_forms::Application& application, const AppOptions& options);
    ~Plot3DWindow() override;

    [[nodiscard]] Plot3DRoot& root() const noexcept { return *root_; }

private:
    Plot3DWindow(gui_forms::Application& application, std::shared_ptr<Plot3DRoot> root, const AppOptions& options);

    std::shared_ptr<Plot3DRoot> root_;
};

} // namespace implot3d_class
