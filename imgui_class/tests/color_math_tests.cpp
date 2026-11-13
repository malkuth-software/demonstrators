// The colour arithmetic: HSV and RGB round trips over a grid, the primary
// and grey corners, normalization of out-of-range and non-finite input,
// and hexadecimal formatting and parsing. Headless.

#include "../src/color_math.hpp"
#include "test_support.hpp"

#include <cmath>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>

namespace gf = gui_forms;
using imgui_class::Hsva;
using imgui_class::Rgba;
using imgui_class_tests::require;

namespace {

bool near(double left, double right, double tolerance) {
    const bool close = std::fabs(left - right) <= tolerance;
    return close;
}

void test_corners() {
    const Rgba red = imgui_class::hsv_to_rgb(Hsva{0.0, 1.0, 1.0, 1.0});
    require(red.red == 1.0 && red.green == 0.0 && red.blue == 0.0, "hue 0 is red");
    const Rgba green = imgui_class::hsv_to_rgb(Hsva{1.0 / 3.0, 1.0, 1.0, 1.0});
    require(near(green.green, 1.0, 1e-12) && near(green.red, 0.0, 1e-12), "hue 1/3 is green");
    const Rgba blue = imgui_class::hsv_to_rgb(Hsva{2.0 / 3.0, 1.0, 1.0, 1.0});
    require(near(blue.blue, 1.0, 1e-12) && near(blue.green, 0.0, 1e-12), "hue 2/3 is blue");
    const Hsva grey = imgui_class::rgb_to_hsv(Rgba{0.5, 0.5, 0.5, 1.0});
    require(grey.saturation == 0.0 && grey.hue == 0.0 && grey.value == 0.5, "grey has no saturation and hue 0");
    const Hsva black = imgui_class::rgb_to_hsv(Rgba{0.0, 0.0, 0.0, 1.0});
    require(black.value == 0.0 && black.saturation == 0.0, "black is value 0");
}

void test_round_trips() {
    for (std::size_t h = 0U; h < 36U; ++h) {
        for (std::size_t s = 1U; s <= 10U; ++s) {
            for (std::size_t v = 1U; v <= 10U; ++v) {
                const Hsva start{static_cast<double>(h) / 36.0, static_cast<double>(s) / 10.0,
                                 static_cast<double>(v) / 10.0, 0.5};
                const Hsva back = imgui_class::rgb_to_hsv(imgui_class::hsv_to_rgb(start));
                require(near(back.saturation, start.saturation, 1e-9) && near(back.value, start.value, 1e-9),
                        "saturation and value survive the round trip");
                require(near(back.hue, start.hue, 1e-9), "a saturated hue survives the round trip");
                require(back.alpha == 0.5, "alpha passes through");
            }
        }
    }
    for (std::size_t value = 0U; value < 256U; ++value) {
        const gf::Color colour = gf::Color::rgba(static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(255U - value),
                                                 static_cast<std::uint8_t>((value * 7U) % 256U), 255U);
        const gf::Color back = imgui_class::to_color(imgui_class::from_color(colour));
        require(back == colour, "8-bit colours survive the round trip");
        const gf::Color through_hsv = imgui_class::hsv_color(imgui_class::rgb_to_hsv(imgui_class::from_color(colour)));
        require(through_hsv == colour, "8-bit colours survive a trip through HSV");
    }
}

void test_normalized() {
    const Hsva wrapped = imgui_class::normalized(Hsva{1.25, 2.0, -1.0, 7.0});
    require(near(wrapped.hue, 0.25, 1e-12), "hue wraps into [0, 1)");
    require(wrapped.saturation == 1.0 && wrapped.value == 0.0 && wrapped.alpha == 1.0, "the rest is clamped");
    const Hsva negative = imgui_class::normalized(Hsva{-0.25, 0.5, 0.5, 0.5});
    require(near(negative.hue, 0.75, 1e-12), "a negative hue wraps");
    const Hsva broken = imgui_class::normalized(Hsva{NAN, NAN, INFINITY, NAN});
    require(broken.hue == 0.0 && broken.saturation == 0.0 && broken.value == 0.0 && broken.alpha == 1.0,
            "non-finite components take their fallbacks");
}

void test_hex() {
    require(imgui_class::format_hex(gf::Color::rgba(255, 128, 0, 255)) == "#ff8000", "opaque colours have six digits");
    require(imgui_class::format_hex(gf::Color::rgba(1, 2, 3, 4)) == "#01020304", "translucent colours have eight");
    const std::optional<gf::Color> six = imgui_class::parse_hex("#FF8000");
    require(six.has_value() && *six == gf::Color::rgba(255, 128, 0, 255), "six digits parse, either case");
    const std::optional<gf::Color> three = imgui_class::parse_hex("f80");
    require(three.has_value() && *three == gf::Color::rgba(255, 136, 0, 255), "three digits expand, no '#'");
    const std::optional<gf::Color> eight = imgui_class::parse_hex("#01020304");
    require(eight.has_value() && (*eight).alpha == 4U, "eight digits carry alpha");
    require(!imgui_class::parse_hex("#12345").has_value(), "five digits are refused");
    require(!imgui_class::parse_hex("#gg0000").has_value(), "non-hex digits are refused");
    require(!imgui_class::parse_hex("").has_value(), "empty text is refused");
}

} // namespace

int main() {
    try {
        test_corners();
        test_round_trips();
        test_normalized();
        test_hex();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "color_math_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("color_math_tests: passed");
    return 0;
}
