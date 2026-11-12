#pragma once

// The app's socket types (NODE_GRAPH_SPEC section 4.1) and their conversion
// table. A port's socket type is stored in GUI.Forms' PortRecord as a plain
// number (PortRecord::socket_type); this header gives those numbers their
// meaning. GUI.Forms' NodeGraph has no conversion table yet (GAPS.md G6), so
// the app keeps one and checks every connection against it before calling
// NodeGraph::connect.

#include <gui_forms/types/paint_types/paint_types.hpp>
#include <gui_forms/types/paint_marks/paint_marks.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace node_graph {

// The numbers written into PortRecord::socket_type (and so into documents).
enum class SocketType : std::uint16_t { float_value = 0, colour = 1, curve = 2, signal = 3, boolean = 4 };

inline constexpr std::size_t socket_type_count = 5U;

// What a link from one socket type into another does (study section 2.3):
// identity, an implicit widening, a lossy conversion (drawn with a mark), or
// refused (the link cannot be made).
enum class Conversion : std::uint8_t { identity, implicit, lossy, refused };

// The socket type a stored number names; none for a number no type uses (a
// document written by a later version), which the app treats as refused.
[[nodiscard]] std::optional<SocketType> socket_type_from_number(std::uint16_t number) noexcept;
[[nodiscard]] std::uint16_t socket_type_number(SocketType type) noexcept;
[[nodiscard]] std::string_view socket_type_name(SocketType type) noexcept;

// The conversion from an output of type `from` into an input of type `to`.
[[nodiscard]] Conversion conversion_between(SocketType from, SocketType to) noexcept;
// The same for stored numbers; refused when either is unknown.
[[nodiscard]] Conversion conversion_between_numbers(std::uint16_t from, std::uint16_t to) noexcept;
[[nodiscard]] bool connectable(std::uint16_t from, std::uint16_t to) noexcept;

// The glyph a port of this type is drawn with: shape by type, never colour
// alone (spec 4.1).
[[nodiscard]] gui_forms::MarkerShape socket_glyph(SocketType type) noexcept;
// The colour a port and its links are drawn in. GUI.Forms' theme has no
// socket-type roles for a consumer's canvas (GAPS.md G2), so these are the
// app's own.
[[nodiscard]] gui_forms::Color socket_colour(SocketType type) noexcept;

} // namespace node_graph
