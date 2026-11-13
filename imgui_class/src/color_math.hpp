#pragma once

// Colour arithmetic for the colour editors: HSV and RGB in [0, 1], 8-bit
// colours, and hexadecimal text (#rrggbb or #rrggbbaa). Pure functions,
// no allocation except the formatted text.

#include <gui_forms/types/paint_types/paint_types.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace imgui_class {

// Hue in [0, 1) (a turn), saturation, value and alpha in [0, 1].
struct Hsva final {
    double hue{};
    double saturation{};
    double value{};
    double alpha{1.0};

    friend bool operator==(const Hsva& left, const Hsva& right) noexcept {
        return left.hue == right.hue && left.saturation == right.saturation && left.value == right.value &&
               left.alpha == right.alpha;
    }
};

// Channels in [0, 1].
struct Rgba final {
    double red{};
    double green{};
    double blue{};
    double alpha{1.0};
};

// Every component clamped to its range; hue wrapped into [0, 1). A
// non-finite component becomes 0 (alpha 1).
[[nodiscard]] Hsva normalized(Hsva colour) noexcept;
[[nodiscard]] Rgba hsv_to_rgb(Hsva colour) noexcept;
// Grey (no saturation) keeps hue 0.
[[nodiscard]] Hsva rgb_to_hsv(Rgba colour) noexcept;
// Rounded to the nearest 8-bit step.
[[nodiscard]] gui_forms::Color to_color(Rgba colour) noexcept;
[[nodiscard]] Rgba from_color(gui_forms::Color colour) noexcept;
[[nodiscard]] gui_forms::Color hsv_color(Hsva colour) noexcept;

// "#rrggbb" for an opaque colour, "#rrggbbaa" otherwise; lower case.
[[nodiscard]] std::string format_hex(gui_forms::Color colour);
// Accepts "#rgb", "#rrggbb" and "#rrggbbaa", either case, the '#'
// optional; none for anything else.
[[nodiscard]] std::optional<gui_forms::Color> parse_hex(std::string_view text) noexcept;

} // namespace imgui_class
