#pragma once

// ColorField: the colour picker GUI.Forms lacks (GAPS.md I2). A
// saturation-value square for the current hue, a vertical hue bar and a
// vertical alpha bar over a checkerboard, all drawn with Painter's linear
// gradients; drag in any of them, or use the keys (arrows move in the
// square, Shift+Up/Down the hue, Alt+Up/Down the alpha).
//
// Events: color_changed after each change a gesture or a key makes, never
// after set_color. One UI thread.

#include "color_math.hpp"

#include <gui_forms/control.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/events/key_events/key_events.hpp>
#include <gui_forms/events/mouse_events/mouse_events.hpp>

#include <cstdint>

namespace imgui_class {

struct ColorFieldStatistics final {
    std::uint64_t frames_painted{};
    std::uint64_t gestures{};
    std::uint64_t changes{};
};

class ColorField final : public gui_forms::Control {
public:
    static constexpr double bar_width = 18.0;
    static constexpr double gap = 8.0;

    explicit ColorField(gui_forms::StableId stable_id);

    // Sets the colour (normalized), raising nothing.
    void set_color(Hsva colour);
    [[nodiscard]] Hsva color() const noexcept { return colour_; }
    [[nodiscard]] gui_forms::Event<const Hsva&>& color_changed() noexcept { return color_changed_; }
    [[nodiscard]] const ColorFieldStatistics& statistics() const& noexcept { return statistics_; }
    const ColorFieldStatistics& statistics() const&& = delete;

    // The parts' rectangles of the last layout (local).
    [[nodiscard]] gui_forms::Rect square() const noexcept { return square_; }
    [[nodiscard]] gui_forms::Rect hue_bar() const noexcept { return hue_bar_; }
    [[nodiscard]] gui_forms::Rect alpha_bar() const noexcept { return alpha_bar_; }

    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect local_damage) override;
    void on_mouse_down(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_move(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_up(gui_forms::MouseEventArgs& arguments) override;
    void on_key_down(gui_forms::KeyEventArgs& arguments) override;

private:
    enum class Part : std::uint8_t { none, square, hue, alpha };

    [[nodiscard]] Part part_at(gui_forms::Point local) const noexcept;
    // Applies the pointer at `local` to `part`; raises color_changed when
    // the colour moved.
    void apply(Part part, gui_forms::Point local);
    void change(Hsva colour);

    Hsva colour_{0.0, 1.0, 1.0, 1.0};
    gui_forms::Rect square_{};
    gui_forms::Rect hue_bar_{};
    gui_forms::Rect alpha_bar_{};
    Part dragging_{Part::none};
    ColorFieldStatistics statistics_{};
    gui_forms::Event<const Hsva&> color_changed_{};
};

} // namespace imgui_class
