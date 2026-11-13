#pragma once

// FormStack: a column of labelled rows, each a caption (a Label the stack
// makes) beside a control, at the row height the caller gives. The
// widgets pages are built from these; the layout is the app's own few
// lines of arrange (GAPS.md I9 asks for a stock one).
//
// Ownership: the stack owns its captions and controls (add_child); the
// rows here observe them in order.

#include <gui_forms/control.hpp>
#include <gui_forms/controls/label.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace imgui_class {

class FormStack final : public gui_forms::Control {
public:
    static constexpr double caption_width = 150.0;
    static constexpr double gap = 6.0;

    explicit FormStack(gui_forms::StableId stable_id);

    // A row `height` units tall: `caption` (none when empty) and `control`
    // spanning the rest of the width, or `width` units when positive.
    void add_row(const std::string& caption, const gui_forms::Control::Ptr& control, double height, double width = 0.0);
    [[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }

    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;

private:
    struct Row final {
        std::shared_ptr<gui_forms::Label> caption{};
        gui_forms::Control::Ptr control{};
        double height{};
        double width{};
    };

    std::vector<Row> rows_{};
};

} // namespace imgui_class
