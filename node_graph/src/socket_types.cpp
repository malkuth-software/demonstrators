#include "socket_types.hpp"

#include <array>

namespace node_graph {

namespace gf = gui_forms;

namespace {

// Rows: the output's type; columns: the input's type; both in SocketType
// order (float, colour, curve, signal, boolean).
//   float -> colour: a grey of that level (implicit).
//   colour -> float: its luminance (lossy).
//   float <-> signal: a signal is a float that changes with time (implicit).
//   float -> boolean: above one half (lossy); boolean -> float: 0 or 1.
//   curve: only into a curve input.
constexpr std::array<std::array<Conversion, socket_type_count>, socket_type_count> conversion_table{{
    {{Conversion::identity, Conversion::implicit, Conversion::refused, Conversion::implicit, Conversion::lossy}},
    {{Conversion::lossy, Conversion::identity, Conversion::refused, Conversion::lossy, Conversion::refused}},
    {{Conversion::refused, Conversion::refused, Conversion::identity, Conversion::refused, Conversion::refused}},
    {{Conversion::implicit, Conversion::implicit, Conversion::refused, Conversion::identity, Conversion::lossy}},
    {{Conversion::implicit, Conversion::refused, Conversion::refused, Conversion::implicit, Conversion::identity}},
}};

} // namespace

std::optional<SocketType> socket_type_from_number(std::uint16_t number) noexcept {
    if (number >= socket_type_count) return std::nullopt;
    const SocketType type = static_cast<SocketType>(number);
    return type;
}

std::uint16_t socket_type_number(SocketType type) noexcept {
    const std::uint16_t number = static_cast<std::uint16_t>(type);
    return number;
}

std::string_view socket_type_name(SocketType type) noexcept {
    switch (type) {
    case SocketType::float_value: return "float";
    case SocketType::colour: return "colour";
    case SocketType::curve: return "curve";
    case SocketType::signal: return "signal";
    case SocketType::boolean: return "bool";
    }
    return "unknown";
}

Conversion conversion_between(SocketType from, SocketType to) noexcept {
    const std::size_t row = static_cast<std::size_t>(from);
    const std::size_t column = static_cast<std::size_t>(to);
    if (row >= socket_type_count || column >= socket_type_count) return Conversion::refused;
    const Conversion conversion = conversion_table[row][column];
    return conversion;
}

Conversion conversion_between_numbers(std::uint16_t from, std::uint16_t to) noexcept {
    const std::optional<SocketType> from_type = socket_type_from_number(from);
    const std::optional<SocketType> to_type = socket_type_from_number(to);
    if (!from_type.has_value() || !to_type.has_value()) return Conversion::refused;
    const Conversion conversion = conversion_between(*from_type, *to_type);
    return conversion;
}

bool connectable(std::uint16_t from, std::uint16_t to) noexcept {
    const bool result = conversion_between_numbers(from, to) != Conversion::refused;
    return result;
}

gf::MarkerShape socket_glyph(SocketType type) noexcept {
    switch (type) {
    case SocketType::float_value: return gf::MarkerShape::circle;
    case SocketType::colour: return gf::MarkerShape::diamond;
    case SocketType::curve: return gf::MarkerShape::square;
    case SocketType::signal: return gf::MarkerShape::triangle_right;
    case SocketType::boolean: return gf::MarkerShape::triangle_up;
    }
    return gf::MarkerShape::circle;
}

gf::Color socket_colour(SocketType type) noexcept {
    switch (type) {
    case SocketType::float_value: return gf::Color::rgba(160, 160, 168);
    case SocketType::colour: return gf::Color::rgba(214, 178, 52);
    case SocketType::curve: return gf::Color::rgba(96, 170, 226);
    case SocketType::signal: return gf::Color::rgba(110, 196, 120);
    case SocketType::boolean: return gf::Color::rgba(206, 110, 168);
    }
    return gf::Color::rgba(160, 160, 168);
}

} // namespace node_graph
