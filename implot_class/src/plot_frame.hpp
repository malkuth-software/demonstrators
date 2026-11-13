#pragma once

// PlotFrame: the frame the demonstrator's own plot kinds share (heatmap,
// histogram, draggable handles), built from the plots package's public
// primitives the way a third party would build a kind PlotView lacks
// (GAPS.md P1): the view and gesture model (plots::PlotViewModel), the
// axes and their ticks (plots::TickGenerator), the data-to-device
// transform (plots::PlotTransform) and the batched mark painters
// (plots::SeriesMarkPainter) on the core Painter.
//
// Layout: the title strip on top, the y axis's labels on the left, the x
// axis's below, the plot rectangle in the rest (minus `right_margin()`,
// which a kind may claim, as the heatmap does for its colour scale).
//
// Interaction (the model's table): left drag pans, the wheel zooms about
// the pointer, right drag zooms to a box, a double click or Home fits, +
// and - zoom. A kind may claim a press first (press_content) to drag its
// own items; the frame then forwards moves and the release to it and the
// model sees nothing.
//
// Device scale: the frame cannot learn the window's display scale through
// the public API (GAPS.md P4), so its transform uses 1 device pixel per
// logical unit; decimation columns and the snap rule are logical.
//
// Events: view_changed after a gesture, a key or set_ranges moved the
// view (not after fit_on_next_layout's automatic fit). One UI thread.

#include <gui_forms/control.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/events/key_events/key_events.hpp>
#include <gui_forms/events/mouse_events/mouse_events.hpp>
#include <gui_forms/plots/axis/axis.hpp>
#include <gui_forms/plots/mark_painters/mark_painters.hpp>
#include <gui_forms/plots/plot_transform/plot_transform.hpp>
#include <gui_forms/plots/plot_view_model/plot_view_model.hpp>
#include <gui_forms/plots/tick_generator/tick_generator.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace implot_class {

// What the frame and its kind did, summed since construction.
struct PlotFrameStatistics final {
    std::uint64_t frames_painted{};
    std::uint64_t mark_calls{};
    std::uint64_t points_in{};
    std::uint64_t points_drawn{};
    std::uint64_t truncated_calls{};
    std::uint64_t view_changes{};
    std::uint64_t content_drags{};
    std::uint64_t tick_generations{};
    std::uint64_t ticks_truncated{};
    // Points drawn by the last frame's mark calls (frame cost is bounded by
    // decimation: compare with the plot's width).
    std::uint64_t last_frame_points_drawn{};
};

// The primary axes' ranges after a change.
struct PlotFrameView final {
    gui_forms::plots::PlotRange x{};
    gui_forms::plots::PlotRange y{};
};

class PlotFrame : public gui_forms::Control {
public:
    static constexpr double title_height = 20.0;
    static constexpr double left_margin = 56.0;
    static constexpr double bottom_margin = 30.0;
    static constexpr double base_right_margin = 12.0;
    static constexpr double tick_length = 4.0;

    explicit PlotFrame(gui_forms::StableId stable_id);
    ~PlotFrame() override;
    PlotFrame(const PlotFrame&) = delete;
    PlotFrame& operator=(const PlotFrame&) = delete;

    void set_title(std::string title);
    [[nodiscard]] const std::string& title() const& noexcept { return title_; }
    const std::string& title() const&& = delete;
    // The primary axes' ranges; refused (false) when inadmissible.
    bool set_ranges(gui_forms::plots::PlotRange x, gui_forms::plots::PlotRange y);
    [[nodiscard]] gui_forms::plots::PlotRange x_range() const noexcept;
    [[nodiscard]] gui_forms::plots::PlotRange y_range() const noexcept;
    // Fits to the kind's data now (raises view_changed when it moved).
    void fit();
    // Fits once the next layout has a plot rectangle.
    void fit_on_next_layout() noexcept { fit_pending_ = true; }
    [[nodiscard]] gui_forms::Rect plot_rect() const noexcept { return plot_rect_; }
    [[nodiscard]] const PlotFrameStatistics& statistics() const& noexcept { return statistics_; }
    const PlotFrameStatistics& statistics() const&& = delete;
    [[nodiscard]] gui_forms::Event<const PlotFrameView&>& view_changed() noexcept { return view_changed_; }
    // The transform of the last layout (valid once laid out with a
    // non-empty plot rectangle).
    [[nodiscard]] bool transform_valid() const noexcept { return transform_valid_; }
    [[nodiscard]] const gui_forms::plots::PlotTransform& transform() const& noexcept { return transform_; }
    const gui_forms::plots::PlotTransform& transform() const&& = delete;

    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect local_damage) override;

protected:
    // -- what a kind supplies ----------------------------------------------------------
    // Draws the kind's marks inside the plot rectangle (the painter is
    // clipped to it), reporting each call's statistics through note_marks.
    virtual void paint_content(gui_forms::Painter& painter, const gui_forms::plots::PlotTransform& transform) = 0;
    // The data's extent, for fitting (an invalid range leaves that axis).
    [[nodiscard]] virtual PlotFrameView data_extent() const = 0;
    // The most points one mark call draws without decimation (the painter's
    // scratch is reserved for it at layout).
    [[nodiscard]] virtual std::size_t maximum_points() const noexcept { return 4096U; }
    // Space on the right of the plot the kind draws in (paint_margin).
    [[nodiscard]] virtual double right_margin() const noexcept { return base_right_margin; }
    // Draws in the right margin, after the plot; `margin` is local.
    virtual void paint_margin(gui_forms::Painter& painter, gui_forms::Rect margin);
    // A primary press inside the plot: true claims the gesture.
    virtual bool press_content(gui_forms::Point local, const gui_forms::plots::PlotTransform& transform);
    virtual void drag_content(gui_forms::Point local, const gui_forms::plots::PlotTransform& transform);
    virtual void release_content();
    // The pointer over the plot without a gesture (hover readouts).
    virtual void hover_content(gui_forms::Point local, const gui_forms::plots::PlotTransform& transform);
    // The text drawn right of the title (a readout); empty for none.
    [[nodiscard]] virtual std::string readout() const;

    void note_marks(const gui_forms::plots::MarkStatistics& marks) noexcept;
    [[nodiscard]] gui_forms::plots::SeriesMarkPainter& marks() noexcept { return marks_; }

    void on_mouse_down(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_move(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_up(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_wheel(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_double_click(gui_forms::MouseEventArgs& arguments) override;
    void on_key_down(gui_forms::KeyEventArgs& arguments) override;

private:
    [[nodiscard]] gui_forms::plots::PlotZone zone_at(gui_forms::Point local) const noexcept;
    [[nodiscard]] gui_forms::plots::PlotDataBounds data_bounds() const;
    // Remakes the transform from the model's axes; false when invalid.
    bool update_transform();
    // Invalidates and raises view_changed after a model change.
    void take_change(const gui_forms::plots::PlotViewChange& change);
    void paint_axis_labels(gui_forms::Painter& painter, const gui_forms::plots::TickSet& ticks, bool x_axis);

    std::string title_{};
    gui_forms::plots::PlotViewModel model_{};
    gui_forms::plots::SeriesMarkPainter marks_{};
    gui_forms::plots::TickSet x_ticks_{};
    gui_forms::plots::TickSet y_ticks_{};
    gui_forms::plots::PlotTransform transform_{};
    bool transform_valid_{};
    gui_forms::Rect plot_rect_{};
    std::size_t reserved_columns_{};
    std::size_t reserved_points_{};
    bool fit_pending_{true};
    bool content_gesture_{};
    bool model_gesture_{};
    PlotFrameStatistics statistics_{};
    std::uint64_t frame_points_drawn_{};
    gui_forms::Event<const PlotFrameView&> view_changed_{};
};

} // namespace implot_class
