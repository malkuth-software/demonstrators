#pragma once

// The viewer's window: a GUI.Forms Window whose root is ProfilerRoot, with
// the trace the command line names (or a generated one) loaded before the
// window opens. Application::run owns the window; the window holds the
// root.

#include "options.hpp"
#include "profiler_root.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/window/window.hpp>

#include <memory>

namespace flame_graph {

// A root with the options' trace loaded (or generated).
[[nodiscard]] std::shared_ptr<ProfilerRoot> make_profiler_root(const AppOptions& options);
// The generator options the command line describes.
[[nodiscard]] GeneratorOptions generator_options(const AppOptions& options) noexcept;

class ProfilerWindow final : public gui_forms::Window {
public:
    ProfilerWindow(gui_forms::Application& application, const AppOptions& options);
    ~ProfilerWindow() override;

    [[nodiscard]] ProfilerRoot& root() const noexcept { return *root_; }

private:
    ProfilerWindow(gui_forms::Application& application, std::shared_ptr<ProfilerRoot> root,
                   const AppOptions& options);

    std::shared_ptr<ProfilerRoot> root_;
};

} // namespace flame_graph
