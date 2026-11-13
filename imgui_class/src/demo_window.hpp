#pragma once

// The ImGui-class window: a GUI.Forms Window whose root is DemoRoot.
// Application::run owns the window; the window holds the root.

#include "options.hpp"
#include "demo_root.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/window/window.hpp>

#include <memory>

namespace imgui_class {

class DemoWindow final : public gui_forms::Window {
public:
    DemoWindow(gui_forms::Application& application, const AppOptions& options);
    ~DemoWindow() override;

    [[nodiscard]] DemoRoot& root() const noexcept { return *root_; }

private:
    DemoWindow(gui_forms::Application& application, std::shared_ptr<DemoRoot> root, const AppOptions& options);

    std::shared_ptr<DemoRoot> root_;
};

} // namespace imgui_class
