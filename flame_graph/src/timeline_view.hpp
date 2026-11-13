#pragma once

// TimelineView: the trace over time, one lane per thread. A lane is a
// header row (the thread's label) and one row per call depth, capped at
// maximum_lane_rows; each span is a bar from its start to its end. A time
// ruler on top carries ticks from the plots package's tick generator.
//
// The horizontal axis is in milliseconds from the trace's first event for
// a nanosecond trace, or in samples for a folded one. Like FlameView, the
// range lives in a plots::PlotViewModel driven with x-axis gestures (drag
// pans, wheel zooms about the pointer); Shift+wheel and Page Up/Down scroll
// the lanes vertically.
//
// Painting a row: the first span ending after the range's start is found
// by binary search (spans of one row never overlap, so their ends are in
// start order); spans narrower than minimum_span_width merge into one bar
// with their neighbours, and after a merge the walk jumps by binary search
// to the first span starting past the merged pixel, so a row costs about
// one visit per device pixel however many spans the range holds (the
// spans jumped over all start inside the merged pixel; only the last can
// reach beyond it, and it is visited). Every bar of a frame goes to the
// painter in one fill_rects_each call.
//
// Storage: lane offsets are computed at model_changed; the bar arrays and
// the tick set are reserved there and reused. One UI thread.

#include "profile_inputs.hpp"

#include <gui_forms/control.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/events/key_events/key_events.hpp>
#include <gui_forms/events/mouse_events/mouse_events.hpp>
#include <gui_forms/plots/plot_view_model/plot_view_model.hpp>
#include <gui_forms/plots/tick_generator/tick_generator.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flame_graph {

struct TimelineStatistics final {
    std::uint64_t frames_painted{};
    std::uint64_t spans_visited{};
    std::uint64_t bars_drawn{};
    std::uint64_t spans_merged{};
    std::uint64_t jumps{};
    std::uint64_t spans_jumped{};
    std::uint64_t bars_over_cap{};
    std::uint64_t labels_drawn{};
    std::uint64_t label_measures{};
    std::uint64_t tick_generations{};
    std::uint64_t picks{};
    std::uint64_t view_changes{};
    std::uint64_t row_scrolls{};
    std::uint64_t last_frame_visits{};
    std::uint64_t last_frame_bars{};
    std::uint64_t last_frame_rows{};
};

// A span under the pointer.
struct SpanHit final {
    ThreadIndex lane{};
    std::uint32_t span{};
    CallNodeIndex node{};
    NameId name{};
};

class TimelineView final : public gui_forms::Control {
public:
    static constexpr double ruler_height = 22.0;
    static constexpr double lane_header_height = 18.0;
    static constexpr double lane_gap = 4.0;
    static constexpr double row_height = 16.0;
    static constexpr double minimum_span_width = 1.0;
    static constexpr double label_minimum_width = 40.0;
    static constexpr double label_size = 10.0;
    static constexpr std::size_t maximum_lane_rows = 32U;
    static constexpr std::size_t maximum_bars = 65536U;

    explicit TimelineView(gui_forms::StableId stable_id);
    ~TimelineView() override;
    TimelineView(const TimelineView&) = delete;
    TimelineView& operator=(const TimelineView&) = delete;

    void set_inputs(const ProfileInputs& inputs) noexcept;
    void model_changed();
    void inputs_changed() noexcept;

    // The visible range in axis units (see the file comment).
    [[nodiscard]] gui_forms::plots::PlotRange x_range() const noexcept;
    void set_x_range(gui_forms::plots::PlotRange range);
    void reset_view();
    void scroll_rows(double rows);
    [[nodiscard]] double row_scroll() const noexcept { return row_scroll_; }
    // Axis units per trace unit (1e-6 for nanoseconds to milliseconds).
    [[nodiscard]] double unit_scale() const noexcept { return unit_scale_; }
    // The local x of a trace time, and the lane's top (local, scrolled).
    [[nodiscard]] double device_x(std::int64_t time) const noexcept;
    [[nodiscard]] double lane_top(ThreadIndex lane) const noexcept;
    [[nodiscard]] std::optional<SpanHit> span_at(gui_forms::Point local) const;
    [[nodiscard]] gui_forms::Rect plot_rect() const noexcept { return plot_rect_; }
    [[nodiscard]] double content_height() const noexcept { return content_height_; }

    [[nodiscard]] gui_forms::Event<const NodePick&>& node_picked() noexcept { return node_picked_; }
    [[nodiscard]] gui_forms::Event<>& view_changed() noexcept { return view_changed_; }
    [[nodiscard]] const TimelineStatistics& statistics() const& noexcept { return statistics_; }
    const TimelineStatistics& statistics() const&& = delete;

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
    struct EndsAtOrBefore;
    struct StartsBefore;
    struct LaneStartsAtOrBefore;
    // One bar drawn this frame: a span's, or a merged run's (span empty).
    struct BarSource final {
        std::uint32_t lane{};
        std::optional<std::uint32_t> span{};
    };

    [[nodiscard]] double time_at(double device) const noexcept;
    [[nodiscard]] std::size_t lane_rows(const ThreadLane& lane) const noexcept;
    [[nodiscard]] gui_forms::Color span_color(const TraceSpan& span) const noexcept;
    [[nodiscard]] gui_forms::plots::PlotDataBounds data_bounds() const noexcept;
    bool clamp_range();
    void clamp_scroll() noexcept;
    void take_change(const gui_forms::plots::PlotViewChange& change);
    void paint_ruler(gui_forms::Painter& painter);
    void paint_row(const ThreadLane& lane, std::uint32_t lane_index, std::size_t depth, double y);
    void push_bar(gui_forms::Rect rect, gui_forms::Color color, BarSource source);
    void paint_lane_headers(gui_forms::Painter& painter);
    void paint_outlines_and_labels(gui_forms::Painter& painter);

    ProfileInputs inputs_{};
    gui_forms::plots::PlotViewModel range_model_{};
    gui_forms::plots::TickSet ticks_{};
    gui_forms::Rect plot_rect_{};
    bool laid_out_{};
    double unit_scale_{1.0e-6};
    std::int64_t origin_{};
    double row_scroll_{};
    double content_height_{};
    bool pressed_{};
    gui_forms::Point press_point_{};
    double ellipsis_width_{-1.0};
    TimelineStatistics statistics_{};

    // Per lane: its top in content coordinates (before scrolling).
    std::vector<double> lane_tops_{};
    // Reused per paint (reserved by model_changed).
    std::vector<gui_forms::Rect> bar_rects_{};
    std::vector<gui_forms::Color> bar_colors_{};
    std::vector<BarSource> bar_sources_{};
    std::vector<float> label_widths_{};
    std::string label_{};
    std::uint64_t frame_visits_{};

    gui_forms::Event<const NodePick&> node_picked_{};
    gui_forms::Event<> view_changed_{};
};

} // namespace flame_graph
