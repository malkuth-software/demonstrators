#pragma once

// The chat stress window: a GUI.Forms Window whose root is ChatRoot, plus
// the workload driver when the app runs a windowed workload. The window
// owns the driver; Application::run owns the window. The corpus is
// borrowed and outlives the window (main declares it first).

#include "chat_view.hpp"
#include "options.hpp"
#include "workload_driver.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/window/window.hpp>

#include <memory>
#include <optional>

namespace chat_stress {

class ChatWindow final : public gui_forms::Window {
public:
    ChatWindow(gui_forms::Application& application, Corpus& corpus, const AppOptions& options,
               std::optional<Workload> workload);
    ~ChatWindow() override;

    [[nodiscard]] ChatRoot& root() const noexcept { return *root_; }
    // Starts the workload, if any (the ready callback calls it).
    void ready(gui_forms::ApplicationWindowHandle handle);

private:
    ChatWindow(gui_forms::Application& application, std::shared_ptr<ChatRoot> root, const AppOptions& options,
               std::optional<Workload> workload);

    std::shared_ptr<ChatRoot> root_;
    std::unique_ptr<WorkloadDriver> driver_{};
};

// ApplicationWindowOptions::ready's target: a named function object with
// its context (the window), never a lambda.
struct ChatWindowReady final {
    ChatWindow* window{};
    void operator()(gui_forms::Window& model, gui_forms::ApplicationWindowHandle handle) const;
};

} // namespace chat_stress
