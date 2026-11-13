#include "grid_page.hpp"

#include <algorithm>
#include <utility>

namespace implot_class {

namespace gf = gui_forms;

GridPage::GridPage(gf::StableId stable_id, std::size_t columns)
    : gf::Control(std::move(stable_id)), columns_(columns == 0U ? 1U : columns) {
    cells_.reserve(8U);
    tools_.reserve(8U);
}

void GridPage::add_cell(const gf::Control::Ptr& cell) {
    cells_.push_back(cell);
    add_child(cell);
}

void GridPage::add_tool(const gf::Control::Ptr& tool, double width) {
    Tool entry{};
    entry.control = tool;
    entry.width = width;
    tools_.push_back(entry);
    add_child(tool);
}

gf::Size GridPage::measure(gf::Size available) {
    return available;
}

void GridPage::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    double top = gap;
    if (!tools_.empty()) {
        double x = gap;
        for (const Tool& tool : tools_) {
            set_child_layout(tool.control, gf::Rect{x, gap, tool.width, tool_height - gap});
            x += tool.width + gap;
        }
        top = tool_height + gap;
    }
    if (cells_.empty()) return;
    const std::size_t rows = (cells_.size() + columns_ - 1U) / columns_;
    const double width = std::max(0.0, final_bounds.width - gap * static_cast<double>(columns_ + 1U));
    const double height = std::max(0.0, final_bounds.height - top - gap * static_cast<double>(rows));
    const double cell_width = width / static_cast<double>(columns_);
    const double cell_height = height / static_cast<double>(rows);
    for (std::size_t index = 0U; index < cells_.size(); ++index) {
        const std::size_t row = index / columns_;
        const std::size_t column = index % columns_;
        const double x = gap + static_cast<double>(column) * (cell_width + gap);
        const double y = top + static_cast<double>(row) * (cell_height + gap);
        set_child_layout(cells_[index], gf::Rect{x, y, cell_width, cell_height});
    }
}

} // namespace implot_class
