#pragma once

// DragPlot: a PlotFrame kind showing a DragModel: the polyline through its
// points (stroke_polyline), the points as markers (draw_markers), and the
// two lines across the plot (paint_infinite_lines). A primary press on a
// handle drags it (the frame's content gesture); elsewhere the press pans.
// handles_moved fires after each move, on the UI thread.
//
// The drawing calls are the core Painter's batched marks, reached directly
// as a consumer kind does; their gd_* counterparts are recorder_draw_lines,
// recorder_draw_markers and recorder_draw_line_segments (GAPS.md G-table).

#include "drag_model.hpp"
#include "plot_frame.hpp"

#include <gui_forms/event.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace implot_class {

class DragPlot final : public PlotFrame {
public:
    explicit DragPlot(gui_forms::StableId stable_id);

    [[nodiscard]] DragModel& model() noexcept { return model_; }
    [[nodiscard]] const DragModel& model() const noexcept { return model_; }
    // Call after changing the model from outside a drag (repaints).
    void model_changed();
    [[nodiscard]] gui_forms::Event<>& handles_moved() noexcept { return handles_moved_; }
    [[nodiscard]] DragTarget dragging() const noexcept { return dragging_; }

protected:
    void paint_content(gui_forms::Painter& painter, const gui_forms::plots::PlotTransform& transform) override;
    [[nodiscard]] PlotFrameView data_extent() const override;
    bool press_content(gui_forms::Point local, const gui_forms::plots::PlotTransform& transform) override;
    void drag_content(gui_forms::Point local, const gui_forms::plots::PlotTransform& transform) override;
    void release_content() override;
    void hover_content(gui_forms::Point local, const gui_forms::plots::PlotTransform& transform) override;
    [[nodiscard]] std::string readout() const override;

private:
    DragModel model_{};
    DragTarget dragging_{};
    DragTarget hovered_{};
    // The polyline's device points, reused each paint.
    std::vector<gui_forms::Point> device_points_{};
    gui_forms::Event<> handles_moved_{};
};

} // namespace implot_class
