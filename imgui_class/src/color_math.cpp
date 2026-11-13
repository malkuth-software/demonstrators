#include "color_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace imgui_class {

namespace gf = gui_forms;

namespace {

double unit(double value, double fallback) noexcept {
    if (!std::isfinite(value)) return fallback;
    const double clamped = std::clamp(value, 0.0, 1.0);
    return clamped;
}

std::uint8_t byte_of(double value) noexcept {
    const double scaled = std::round(unit(value, 0.0) * 255.0);
    return static_cast<std::uint8_t>(scaled);
}

int hex_digit(char character) noexcept {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

void append_byte(std::string& text, std::uint8_t value) {
    text.push_back(digits[value >> 4U]);
    text.push_back(digits[value & 0x0FU]);
}

} // namespace

Hsva normalized(Hsva colour) noexcept {
    Hsva result{};
    double hue = std::isfinite(colour.hue) ? colour.hue : 0.0;
    hue -= std::floor(hue);
    // floor can leave exactly 1.0 for a tiny negative hue.
    if (hue >= 1.0) hue = 0.0;
    result.hue = hue;
    result.saturation = unit(colour.saturation, 0.0);
    result.value = unit(colour.value, 0.0);
    result.alpha = unit(colour.alpha, 1.0);
    return result;
}

Rgba hsv_to_rgb(Hsva colour) noexcept {
    const Hsva hsv = normalized(colour);
    const double chroma = hsv.value * hsv.saturation;
    const double sector = hsv.hue * 6.0;
    const double x = chroma * (1.0 - std::fabs(std::fmod(sector, 2.0) - 1.0));
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    const int index = static_cast<int>(sector);
    switch (index) {
    case 0:
        red = chroma;
        green = x;
        break;
    case 1:
        red = x;
        green = chroma;
        break;
    case 2:
        green = chroma;
        blue = x;
        break;
    case 3:
        green = x;
        blue = chroma;
        break;
    case 4:
        red = x;
        blue = chroma;
        break;
    default:
        red = chroma;
        blue = x;
        break;
    }
    const double lift = hsv.value - chroma;
    Rgba result{};
    result.red = red + lift;
    result.green = green + lift;
    result.blue = blue + lift;
    result.alpha = hsv.alpha;
    return result;
}

Hsva rgb_to_hsv(Rgba colour) noexcept {
    const double red = unit(colour.red, 0.0);
    const double green = unit(colour.green, 0.0);
    const double blue = unit(colour.blue, 0.0);
    const double largest = std::max(red, std::max(green, blue));
    const double smallest = std::min(red, std::min(green, blue));
    const double chroma = largest - smallest;
    Hsva result{};
    result.value = largest;
    result.alpha = unit(colour.alpha, 1.0);
    if (chroma <= 0.0) return result;
    result.saturation = largest > 0.0 ? chroma / largest : 0.0;
    double sector = 0.0;
    if (largest == red) {
        sector = (green - blue) / chroma;
        if (sector < 0.0) sector += 6.0;
    } else if (largest == green) {
        sector = (blue - red) / chroma + 2.0;
    } else {
        sector = (red - green) / chroma + 4.0;
    }
    result.hue = sector / 6.0;
    if (result.hue >= 1.0) result.hue -= 1.0;
    return result;
}

gf::Color to_color(Rgba colour) noexcept {
    const gf::Color result = gf::Color::rgba(byte_of(colour.red), byte_of(colour.green), byte_of(colour.blue),
                                             byte_of(unit(colour.alpha, 1.0)));
    return result;
}

Rgba from_color(gf::Color colour) noexcept {
    Rgba result{};
    result.red = static_cast<double>(colour.red) / 255.0;
    result.green = static_cast<double>(colour.green) / 255.0;
    result.blue = static_cast<double>(colour.blue) / 255.0;
    result.alpha = static_cast<double>(colour.alpha) / 255.0;
    return result;
}

gf::Color hsv_color(Hsva colour) noexcept {
    const gf::Color result = to_color(hsv_to_rgb(colour));
    return result;
}

std::string format_hex(gf::Color colour) {
    std::string text{};
    text.reserve(9U);
    text.push_back('#');
    append_byte(text, colour.red);
    append_byte(text, colour.green);
    append_byte(text, colour.blue);
    if (colour.alpha != 255U) append_byte(text, colour.alpha);
    return text;
}

std::optional<gf::Color> parse_hex(std::string_view text) noexcept {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1U);
    std::array<int, 8> values{};
    if (text.size() != 3U && text.size() != 6U && text.size() != 8U) return std::nullopt;
    for (std::size_t index = 0U; index < text.size(); ++index) {
        const int digit = hex_digit(text[index]);
        if (digit < 0) return std::nullopt;
        values[index] = digit;
    }
    gf::Color colour{};
    if (text.size() == 3U) {
        colour.red = static_cast<std::uint8_t>(values[0] * 17);
        colour.green = static_cast<std::uint8_t>(values[1] * 17);
        colour.blue = static_cast<std::uint8_t>(values[2] * 17);
        return colour;
    }
    colour.red = static_cast<std::uint8_t>(values[0] * 16 + values[1]);
    colour.green = static_cast<std::uint8_t>(values[2] * 16 + values[3]);
    colour.blue = static_cast<std::uint8_t>(values[4] * 16 + values[5]);
    if (text.size() == 8U) colour.alpha = static_cast<std::uint8_t>(values[6] * 16 + values[7]);
    return colour;
}

} // namespace imgui_class
