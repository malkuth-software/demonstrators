#pragma once

// FlameView: the merged call tree as a flame graph (root at the bottom,
// callees above) or an icicle chart (root at the top, callees below).
// Each node is a cell one row high whose width is its total weight; the
// horizontal view is a range of weight, [0, total] when unzoomed.
//
// Built on the plots package's view model (plots::PlotViewModel) for the
// horizontal range: every gesture is handed to it as an x-axis gesture, so
// a drag pans and the wheel zooms about the pointer along x only; the
// model's y axis is unused. Vertical scrolling (Shift+wheel, Page Up/Down)
// is the view's own, for stacks deeper than the view is tall.
//
// Painting: a depth-first walk from the root that skips a subtree when its
// cell lies outside the range, is narrower than minimum_cell_width (its
// children are narrower still) or lies past the far edge in the growth
// direction. The visible cells go to the painter in one fill_rects_each
// call (one colour per cell), then the selection outlines, then labels on
// cells wide enough, cut to fit with an ellipsis. Label widths are measured
// once per name and kept (label_widths_); a cut label's length is estimated
// from the name's average glyph width, because the Painter has no elided
// or boxed text (GAPS.md F2).
//
// Storage: the walk stack, the cell rectangles, colours and nodes are
// reserved when a trace is loaded (model_changed) and reused each paint;
// painting allocates nothing. One UI thread.
//
// Input: click picks a cell (node_picked), double click zooms to it (the
// root or empty space resets), drag pans, wheel zooms, Shift+wheel scrolls
// rows, Home or Escape resets, + and - zoom, Left and Right pan.

#include "profile_inputs.hpp"

#include <gui_forms/control.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/events/key_events/key_events.hpp>
#include <gui_forms/events/mouse_events/mouse_events.hpp>
#include <gui_forms/plots/plot_view_model/plot_view_model.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flame_graph {

enum class FlameOrientation : std::uint8_t { flame, icicle };

struct FlameViewStatistics final {
    std::uint64_t frames_painted{};
    std::uint64_t cells_visited{};
    std::uint64_t cells_drawn{};
    std::uint64_t cells_pruned_narrow{};
    std::uint64_t cells_culled{};
    std::uint64_t cells_over_cap{};
    std::uint64_t labels_drawn{};
    std::uint64_t labels_cut{};
    std::uint64_t label_measures{};
    std::uint64_t picks{};
    std::uint64_t zooms{};
    std::uint64_t resets{};
    std::uint64_t view_changes{};
    std::uint64_t row_scrolls{};
    std::uint64_t last_frame_visits{};
    std::uint64_t last_frame_cells{};
    std::uint64_t last_frame_labels{};
};

class FlameView final : public gui_forms::Control {
public:
    static constexpr double header_height = 20.0;
    static constexpr double row_height = 18.0;
    static constexpr double minimum_cell_width = 1.0;
    static constexpr double label_minimum_width = 28.0;
    static constexpr double label_padding = 4.0;
    static constexpr double label_size = 11.0;
    // The most cells one frame draws (a full view holds far fewer: about
    // rows times width over minimum_cell_width).
    static constexpr std::size_t maximum_cells = 65536U;

    explicit FlameView(gui_forms::StableId stable_id);
    ~FlameView() override;
    FlameView(const FlameView&) = delete;
    FlameView& operator=(const FlameView&) = delete;

    void set_inputs(const ProfileInputs& inputs) noexcept;
    // After a load: storage reserved for the trace, the zoom and the row
    // scroll reset.
    void model_changed();
    // After the search or the selection changed: repaints.
    void inputs_changed() noexcept;

    void set_orientation(FlameOrientation orientation);
    [[nodiscard]] FlameOrientation orientation() const noexcept { return orientation_; }
    // Zooms the range to the node's extent; false for a node the model
    // does not hold.
    bool zoom_to(CallNodeIndex node);
    void reset_zoom();
    [[nodiscard]] std::optional<CallNodeIndex> zoom_root() const noexcept { return zoom_root_; }
    [[nodiscard]] gui_forms::plots::PlotRange x_range() const noexcept;
    // Scrolls by `rows` rows towards the deep end (negative: back).
    void scroll_rows(double rows);
    [[nodiscard]] double row_scroll() const noexcept { return row_scroll_; }

    // The cell under a local point, or none.
    [[nodiscard]] std::optional<CallNodeIndex> node_at(gui_forms::Point local) const;
    // The rectangle a node's cell occupies now, local (it may lie outside
    // the plot, or be narrower than drawn cells); none before layout.
    [[nodiscard]] std::optional<gui_forms::Rect> cell_rect(CallNodeIndex node) const;
    [[nodiscard]] gui_forms::Rect plot_rect() const noexcept { return plot_rect_; }
    // Rows the plot shows at once.
    [[nodiscard]] std::size_t visible_rows() const noexcept;

    [[nodiscard]] gui_forms::Event<const NodePick&>& node_picked() noexcept { return node_picked_; }
    // After a user gesture or key moved the range (not after zoom_to or
    // reset_zoom called by the program).
    [[nodiscard]] gui_forms::Event<>& view_changed() noexcept { return view_changed_; }
    [[nodiscard]] const FlameViewStatistics& statistics() const& noexcept { return statistics_; }
    const FlameViewStatistics& statistics() const&& = delete;

    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect local_damage) override;

protected:
    void on_mouse_down(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_move(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_up(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_wheel(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_double_click(gui_forms::MouseEventArgs& arguments) override;
    void on_key_down(gui_forms::KeyEventArgs& arguments) override;

private:
    struct ChildEndsBefore;

    [[nodiscard]] double device_x(double weight) const noexcept;
    [[nodiscard]] double weight_at(double device) const noexcept;
    [[nodiscard]] double row_top(std::size_t depth) const noexcept;
    [[nodiscard]] std::optional<std::size_t> depth_at(double device_y) const noexcept;
    [[nodiscard]] gui_forms::Color cell_color(const CallNode& node) const noexcept;
    [[nodiscard]] gui_forms::plots::PlotDataBounds data_bounds() const noexcept;
    void set_range(gui_forms::plots::PlotRange range);
    // Keeps the range inside [0, total] and no wider; true when it moved.
    bool clamp_range();
    void clamp_scroll() noexcept;
    // A model change from a gesture or key: clamps, repaints, raises.
    void take_change(const gui_forms::plots::PlotViewChange& change);
    void paint_header(gui_forms::Painter& painter);
    void paint_labels(gui_forms::Painter& painter);
    void paint_outlines(gui_forms::Painter& painter);

    ProfileInputs inputs_{};
    FlameOrientation orientation_{FlameOrientation::icicle};
    gui_forms::plots::PlotViewModel range_model_{};
    gui_forms::Rect plot_rect_{};
    bool laid_out_{};
    std::optional<CallNodeIndex> zoom_root_{};
    double row_scroll_{};
    std::size_t deepest_{};
    bool pressed_{};
    gui_forms::Point press_point_{};
    double ellipsis_width_{-1.0};
    FlameViewStatistics statistics_{};

    // Reused per paint (reserved by model_changed).
    std::vector<std::uint32_t> walk_{};
    std::vector<gui_forms::Rect> cell_rects_{};
    std::vector<gui_forms::Color> cell_colors_{};
    std::vector<std::uint32_t> cell_nodes_{};
    // Per name: the label's measured width at label_size, < 0 unmeasured.
    std::vector<float> label_widths_{};
    std::string label_{};

    gui_forms::Event<const NodePick&> node_picked_{};
    gui_forms::Event<> view_changed_{};
};

} // namespace flame_graph
