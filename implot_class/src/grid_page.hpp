#pragma once

// GridPage: one tab's content: a tool strip of fixed-width controls along
// the top and the cells below in a grid of `columns` columns, rows as many
// as the cells need, each cell the same size. The layout is the app's own
// (a few lines of arrange) rather than GridPanel's, so the demonstrator
// leans only on the base Control layout contract.
//
// Ownership: the page owns its children (add_child); the lists here
// observe them in order.

#include <gui_forms/control.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace implot_class {

class GridPage : public gui_forms::Control {
public:
    static constexpr double tool_height = 28.0;
    static constexpr double gap = 6.0;

    GridPage(gui_forms::StableId stable_id, std::size_t columns);

    void add_cell(const gui_forms::Control::Ptr& cell);
    // A control in the tool strip, `width` logical units wide.
    void add_tool(const gui_forms::Control::Ptr& tool, double width);
    [[nodiscard]] std::size_t cell_count() const noexcept { return cells_.size(); }

    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;

private:
    struct Tool final {
        gui_forms::Control::Ptr control{};
        double width{};
    };

    std::size_t columns_{1U};
    std::vector<gui_forms::Control::Ptr> cells_{};
    std::vector<Tool> tools_{};
};

} // namespace implot_class
