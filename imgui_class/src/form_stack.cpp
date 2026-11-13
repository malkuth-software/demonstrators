#include "form_stack.hpp"

#include <algorithm>
#include <utility>

namespace imgui_class {

namespace gf = gui_forms;

FormStack::FormStack(gf::StableId stable_id) : gf::Control(std::move(stable_id)) {
    rows_.reserve(16U);
}

void FormStack::add_row(const std::string& caption, const gf::Control::Ptr& control, double height, double width) {
    Row row{};
    if (!caption.empty()) {
        const std::string id = std::string(stable_id().value()) + ".caption-" + std::to_string(rows_.size());
        row.caption = gf::make_control<gf::Label>(gf::StableId(id), caption);
        add_child(row.caption);
    }
    row.control = control;
    row.height = height;
    row.width = width;
    add_child(control);
    rows_.push_back(std::move(row));
}

gf::Size FormStack::measure(gf::Size available) {
    double height = gap;
    for (const Row& row : rows_) height += row.height + gap;
    const gf::Size size{available.width, std::min(available.height, height)};
    return size;
}

void FormStack::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    double y = gap;
    const double control_x = gap + caption_width + gap;
    const double room = std::max(0.0, final_bounds.width - control_x - gap);
    for (const Row& row : rows_) {
        if (row.caption != nullptr) set_child_layout(row.caption, gf::Rect{gap, y, caption_width, row.height});
        const double width = row.width > 0.0 ? std::min(row.width, room) : room;
        set_child_layout(row.control, gf::Rect{control_x, y, width, row.height});
        y += row.height + gap;
    }
}

} // namespace imgui_class
