#include "chat_window.hpp"

namespace chat_stress {

namespace gf = gui_forms;

ChatWindow::ChatWindow(gf::Application& application, Corpus& corpus, const AppOptions& options,
                       std::optional<Workload> workload)
    : ChatWindow(application, gf::make_control<ChatRoot>(gf::StableId("chat-stress"), corpus), options, workload) {}

ChatWindow::ChatWindow(gf::Application& application, std::shared_ptr<ChatRoot> root, const AppOptions& options,
                       std::optional<Workload> workload)
    : gf::Window(application, root, gf::Size{options.width, options.height}), root_(std::move(root)) {
    if (workload.has_value()) driver_ = std::make_unique<WorkloadDriver>(*this, *root_, *workload, options);
}

ChatWindow::~ChatWindow() { driver_.reset(); }

void ChatWindow::ready(gf::ApplicationWindowHandle handle) {
    if (driver_ != nullptr) (*driver_).start(handle);
}

void ChatWindowReady::operator()(gf::Window& model, gf::ApplicationWindowHandle handle) const {
    static_cast<void>(model);
    if (window != nullptr) (*window).ready(handle);
}

} // namespace chat_stress
