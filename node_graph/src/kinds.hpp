#pragma once

// The node kinds: what the app's graph means (NODE_GRAPH_SPEC section 4.1,
// owner decision 1 option (a): a small value, colour, curve and signal
// dataflow with live previews). The table is fixed at compile time; a kind
// is named in documents by its `name` (NodeRecord::kind), and a document
// naming a kind the table lacks keeps the node verbatim and reports it
// (the evaluator marks its outputs `error`).

#include "socket_types.hpp"

#include <gui_forms/controls/node_graph.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace node_graph {

enum class KindFamily : std::uint8_t { source, math, colour, curve, sink, structure };

// What a node's body shows (spec 4.1, "Body" column).
enum class BodyPreview : std::uint8_t { parameters, swatch, curve, sparkline, readout, none };

// An index into the kind table (not an identity: the table is fixed).
struct KindIndex final {
    std::uint16_t value{};
    friend bool operator==(KindIndex left, KindIndex right) noexcept { return left.value == right.value; }
};

struct PortTemplate final {
    std::string_view name{};
    gui_forms::PortDirection direction{gui_forms::PortDirection::input};
    SocketType type{SocketType::float_value};
};

struct PropertyTemplate final {
    std::string_view name{};
    std::string_view default_value{};
};

inline constexpr std::size_t maximum_kind_ports = 6U;
inline constexpr std::size_t maximum_kind_properties = 6U;

// One kind. `ports` and `properties` hold `port_count` and
// `property_count` live entries. An input port whose name equals a
// property's name reads that property while it is unconnected (the value
// bar the canvas paints for it). `primary_property` is what the edit host
// edits (empty: the title).
struct KindDescriptor final {
    std::string_view name{};
    std::string_view title{};
    KindFamily family{KindFamily::math};
    BodyPreview preview{BodyPreview::none};
    double width{160.0};
    double height{96.0};
    std::array<PortTemplate, maximum_kind_ports> ports{};
    std::size_t port_count{};
    std::array<PropertyTemplate, maximum_kind_properties> properties{};
    std::size_t property_count{};
    std::string_view primary_property{};
    // A time-driven source: it ticks on the motion clock (spec 4.1, the idle
    // rule).
    bool time_driven{};
};

// The whole table, in palette order.
[[nodiscard]] std::span<const KindDescriptor> kind_table() noexcept;
[[nodiscard]] const KindDescriptor& kind_at(KindIndex index) noexcept;
[[nodiscard]] std::optional<KindIndex> find_kind(std::string_view name) noexcept;
[[nodiscard]] std::string_view kind_family_name(KindFamily family) noexcept;

// The property's default for `kind`, or none when the kind has no such
// property.
[[nodiscard]] std::optional<std::string_view> kind_default(const KindDescriptor& kind, std::string_view property) noexcept;

// Fills `spec` for a new node of `kind`: key, kind, title, bounds with its
// top-left at `top_left`, its ports (fixed sides: inputs left, outputs
// right, spread evenly) and its default properties. `spec`'s vectors keep
// their capacity (cleared, then filled).
void fill_node_spec(const KindDescriptor& kind, std::string_view key, gui_forms::WorldPoint top_left,
                    gui_forms::NodeSpec& spec);

// Whether `kind` has an input that an output of socket type `from` can feed
// (the palette's filter after a link drag released on empty canvas); its
// first such input is written to `port` when it has one.
[[nodiscard]] bool kind_accepts(const KindDescriptor& kind, std::uint16_t from, gui_forms::PortIndex& port) noexcept;

} // namespace node_graph
