#include "color_field.hpp"

#include <gui_forms/events/input_events/input_events.hpp>
#include <gui_forms/surface_root/surface_root.hpp>
#include <gui_forms/types/paint_marks/paint_marks.hpp>
#include <gui_forms/types/painter/painter.hpp>

#include <algorithm>
#include <array>
#include <utility>

namespace imgui_class {

namespace gf = gui_forms;

namespace {

constexpr gf::Color edge = gf::Color::rgba(60, 64, 72, 255);
constexpr gf::Color marker_dark = gf::Color::rgba(20, 20, 20, 255);
constexpr gf::Color marker_light = gf::Color::rgba(255, 255, 255, 255);
constexpr double key_step = 0.01;

double share_of(double position, double start, double extent) noexcept {
    if (extent <= 0.0) return 0.0;
    const double share = std::clamp((position - start) / extent, 0.0, 1.0);
    return share;
}

gf::Color with_alpha(gf::Color colour, std::uint8_t alpha) noexcept {
    gf::Color result = colour;
    result.alpha = alpha;
    return result;
}

} // namespace

ColorField::ColorField(gf::StableId stable_id) : gf::Control(std::move(stable_id)) {
    focusable = true;
}

void ColorField::set_color(Hsva colour) {
    const Hsva next = normalized(colour);
    if (next == colour_) return;
    colour_ = next;
    invalidate(gf::Dirty::paint);
}

void ColorField::change(Hsva colour) {
    const Hsva next = normalized(colour);
    if (next == colour_) return;
    colour_ = next;
    ++statistics_.changes;
    invalidate(gf::Dirty::paint);
    color_changed_.emit(colour_);
}

gf::Size ColorField::measure(gf::Size available) {
    const double side = std::min(available.height, 220.0);
    const gf::Size size{std::min(available.width, side + 2.0 * (bar_width + gap)), side};
    return size;
}

void ColorField::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    const double side = std::max(0.0, std::min(final_bounds.height, final_bounds.width - 2.0 * (bar_width + gap)));
    square_ = gf::Rect{0.0, 0.0, side, side};
    hue_bar_ = gf::Rect{side + gap, 0.0, bar_width, side};
    alpha_bar_ = gf::Rect{side + 2.0 * gap + bar_width, 0.0, bar_width, side};
}

void ColorField::on_paint(gf::Painter& painter, gf::Rect local_damage) {
    static_cast<void>(local_damage);
    ++statistics_.frames_painted;
    if (square_.width <= 0.0) return;
    // The square: the hue at full saturation and value, white fading out
    // to the right (saturation), black fading in downwards (value).
    const gf::Color hue_colour = hsv_color(Hsva{colour_.hue, 1.0, 1.0, 1.0});
    painter.fill_rect(square_, hue_colour);
    const std::array<gf::GradientStop, 2> whites{{{0.0, marker_light}, {1.0, with_alpha(marker_light, 0U)}}};
    painter.fill_linear_gradient(square_, gf::Point{square_.x, square_.y}, gf::Point{square_.x + square_.width, square_.y},
                                 whites);
    const std::array<gf::GradientStop, 2> blacks{{{0.0, with_alpha(marker_dark, 0U)}, {1.0, gf::Color::rgba(0, 0, 0, 255)}}};
    painter.fill_linear_gradient(square_, gf::Point{square_.x, square_.y}, gf::Point{square_.x, square_.y + square_.height},
                                 blacks);
    painter.stroke_rect(square_, edge, 1.0);
    const gf::Point mark{square_.x + colour_.saturation * square_.width, square_.y + (1.0 - colour_.value) * square_.height};
    const gf::Color ring = colour_.value > 0.5 && colour_.saturation < 0.5 ? marker_dark : marker_light;
    painter.stroke_rounded_rect(gf::Rect{mark.x - 5.0, mark.y - 5.0, 10.0, 10.0}, 5.0, ring, 1.5);

    // The hue bar: seven stops round the turn, top to bottom.
    std::array<gf::GradientStop, 7> hues{};
    for (std::size_t index = 0U; index < hues.size(); ++index) {
        const double share = static_cast<double>(index) / 6.0;
        hues[index] = gf::GradientStop{share, hsv_color(Hsva{share >= 1.0 ? 0.0 : share, 1.0, 1.0, 1.0})};
    }
    painter.fill_linear_gradient(hue_bar_, gf::Point{hue_bar_.x, hue_bar_.y},
                                 gf::Point{hue_bar_.x, hue_bar_.y + hue_bar_.height}, hues);
    painter.stroke_rect(hue_bar_, edge, 1.0);
    const double hue_y = hue_bar_.y + colour_.hue * hue_bar_.height;
    painter.stroke_rect(gf::Rect{hue_bar_.x - 2.0, hue_y - 2.0, hue_bar_.width + 4.0, 4.0}, marker_dark, 1.0);

    // The alpha bar: the colour from transparent (bottom) to opaque (top)
    // over a checkerboard.
    gf::CheckerPattern checker{};
    checker.cell = 6.0;
    checker.first = gf::Color::rgba(204, 204, 204, 255);
    checker.second = gf::Color::rgba(255, 255, 255, 255);
    checker.anchor = gf::Point{alpha_bar_.x, alpha_bar_.y};
    painter.fill_checker(alpha_bar_, checker);
    const gf::Color opaque = hsv_color(Hsva{colour_.hue, colour_.saturation, colour_.value, 1.0});
    const std::array<gf::GradientStop, 2> alphas{{{0.0, opaque}, {1.0, with_alpha(opaque, 0U)}}};
    painter.fill_linear_gradient(alpha_bar_, gf::Point{alpha_bar_.x, alpha_bar_.y},
                                 gf::Point{alpha_bar_.x, alpha_bar_.y + alpha_bar_.height}, alphas);
    painter.stroke_rect(alpha_bar_, edge, 1.0);
    const double alpha_y = alpha_bar_.y + (1.0 - colour_.alpha) * alpha_bar_.height;
    painter.stroke_rect(gf::Rect{alpha_bar_.x - 2.0, alpha_y - 2.0, alpha_bar_.width + 4.0, 4.0}, marker_dark, 1.0);
}

ColorField::Part ColorField::part_at(gf::Point local) const noexcept {
    if (square_.contains(local)) return Part::square;
    if (hue_bar_.contains(local)) return Part::hue;
    if (alpha_bar_.contains(local)) return Part::alpha;
    return Part::none;
}

void ColorField::apply(Part part, gf::Point local) {
    Hsva next = colour_;
    switch (part) {
    case Part::square:
        next.saturation = share_of(local.x, square_.x, square_.width);
        next.value = 1.0 - share_of(local.y, square_.y, square_.height);
        break;
    case Part::hue:
        // The bottom edge is the top's hue: keep it just below one turn.
        next.hue = std::min(share_of(local.y, hue_bar_.y, hue_bar_.height), 0.999999);
        break;
    case Part::alpha:
        next.alpha = 1.0 - share_of(local.y, alpha_bar_.y, alpha_bar_.height);
        break;
    case Part::none:
    default:
        return;
    }
    change(next);
}

void ColorField::on_mouse_down(gf::MouseEventArgs& arguments) {
    if (arguments.button != gf::PointerButton::primary) return;
    const Part part = part_at(arguments.location);
    if (part == Part::none) return;
    dragging_ = part;
    ++statistics_.gestures;
    set_pointer_capture(true);
    apply(part, arguments.location);
    arguments.handled = true;
}

void ColorField::on_mouse_move(gf::MouseEventArgs& arguments) {
    if (dragging_ == Part::none) return;
    apply(dragging_, arguments.location);
    arguments.handled = true;
}

void ColorField::on_mouse_up(gf::MouseEventArgs& arguments) {
    if (dragging_ == Part::none) return;
    apply(dragging_, arguments.location);
    dragging_ = Part::none;
    set_pointer_capture(false);
    arguments.handled = true;
}

void ColorField::on_key_down(gf::KeyEventArgs& arguments) {
    Hsva next = colour_;
    const bool shift = gf::has_modifier(arguments.modifiers, gf::Modifier::shift);
    const bool alt = gf::has_modifier(arguments.modifiers, gf::Modifier::alt);
    const std::uint32_t key = arguments.physical_key;
    if (key == gf::PhysicalKey::left) {
        next.saturation -= key_step;
    } else if (key == gf::PhysicalKey::right) {
        next.saturation += key_step;
    } else if (key == gf::PhysicalKey::up || key == gf::PhysicalKey::down) {
        const double delta = key == gf::PhysicalKey::up ? key_step : -key_step;
        if (shift) {
            next.hue += delta;
        } else if (alt) {
            next.alpha += delta;
        } else {
            next.value += delta;
        }
    } else {
        return;
    }
    change(next);
    arguments.handled = true;
}

} // namespace imgui_class
