#include "kinds.hpp"

namespace node_graph {

namespace gf = gui_forms;

namespace {

constexpr gf::PortDirection in = gf::PortDirection::input;
constexpr gf::PortDirection out = gf::PortDirection::output;
constexpr SocketType f = SocketType::float_value;
constexpr SocketType c = SocketType::colour;
constexpr SocketType cv = SocketType::curve;
constexpr SocketType s = SocketType::signal;

// The table. Each entry is written out in full so that a reader sees every
// kind's ports and defaults in one place.
constexpr std::array<KindDescriptor, 21> kinds{{
    // -- sources --
    {"constant", "Constant", KindFamily::source, BodyPreview::parameters, 150.0, 72.0,
     {{{"value", out, f}}}, 1U, {{{"value", "1"}}}, 1U, "value", false},
    {"time", "Time", KindFamily::source, BodyPreview::readout, 150.0, 72.0,
     {{{"seconds", out, s}}}, 1U, {{{"speed", "1"}}}, 1U, "speed", true},
    {"oscillator", "Oscillator", KindFamily::source, BodyPreview::parameters, 170.0, 104.0,
     {{{"frequency", in, f}, {"amplitude", in, f}, {"signal", out, s}}}, 3U,
     {{{"frequency", "1"}, {"amplitude", "1"}, {"shape", "sine"}}}, 3U, "shape", true},
    {"noise", "Noise", KindFamily::source, BodyPreview::parameters, 160.0, 88.0,
     {{{"scale", in, f}, {"value", out, f}}}, 2U, {{{"scale", "1"}, {"seed", "1"}}}, 2U, "seed", false},
    // -- math --
    {"add", "Add", KindFamily::math, BodyPreview::parameters, 150.0, 88.0,
     {{{"a", in, f}, {"b", in, f}, {"sum", out, f}}}, 3U, {{{"a", "0"}, {"b", "0"}}}, 2U, "b", false},
    {"multiply", "Multiply", KindFamily::math, BodyPreview::parameters, 150.0, 88.0,
     {{{"a", in, f}, {"b", in, f}, {"product", out, f}}}, 3U, {{{"a", "1"}, {"b", "1"}}}, 2U, "b", false},
    {"mix", "Mix", KindFamily::math, BodyPreview::parameters, 150.0, 104.0,
     {{{"a", in, f}, {"b", in, f}, {"t", in, f}, {"result", out, f}}}, 4U,
     {{{"a", "0"}, {"b", "1"}, {"t", "0.5"}}}, 3U, "t", false},
    {"clamp", "Clamp", KindFamily::math, BodyPreview::parameters, 150.0, 104.0,
     {{{"x", in, f}, {"minimum", in, f}, {"maximum", in, f}, {"result", out, f}}}, 4U,
     {{{"x", "0"}, {"minimum", "0"}, {"maximum", "1"}}}, 3U, "maximum", false},
    {"map_range", "Map range", KindFamily::math, BodyPreview::parameters, 170.0, 136.0,
     {{{"x", in, f}, {"from_low", in, f}, {"from_high", in, f}, {"to_low", in, f}, {"to_high", in, f},
       {"result", out, f}}}, 6U,
     {{{"x", "0"}, {"from_low", "0"}, {"from_high", "1"}, {"to_low", "0"}, {"to_high", "1"}}}, 5U, "to_high", false},
    {"expression", "Expression", KindFamily::math, BodyPreview::readout, 190.0, 96.0,
     {{{"a", in, f}, {"b", in, f}, {"result", out, f}}}, 3U,
     {{{"a", "0"}, {"b", "0"}, {"text", "a * b + 1"}}}, 3U, "text", false},
    // -- colour --
    {"rgb", "RGB", KindFamily::colour, BodyPreview::swatch, 150.0, 104.0,
     {{{"red", in, f}, {"green", in, f}, {"blue", in, f}, {"colour", out, c}}}, 4U,
     {{{"red", "0.8"}, {"green", "0.4"}, {"blue", "0.2"}}}, 3U, "red", false},
    {"hsv", "HSV", KindFamily::colour, BodyPreview::swatch, 150.0, 104.0,
     {{{"hue", in, f}, {"saturation", in, f}, {"value", in, f}, {"colour", out, c}}}, 4U,
     {{{"hue", "0.6"}, {"saturation", "0.7"}, {"value", "0.9"}}}, 3U, "hue", false},
    {"gradient_sample", "Gradient sample", KindFamily::colour, BodyPreview::swatch, 180.0, 88.0,
     {{{"t", in, f}, {"colour", out, c}}}, 2U, {{{"t", "0.5"}, {"from", "#203060"}, {"to", "#f0c040"}}}, 3U,
     "to", false},
    {"mix_colour", "Mix colour", KindFamily::colour, BodyPreview::swatch, 160.0, 104.0,
     {{{"a", in, c}, {"b", in, c}, {"t", in, f}, {"colour", out, c}}}, 4U, {{{"t", "0.5"}}}, 1U, "t", false},
    // -- curves --
    {"curve", "Curve", KindFamily::curve, BodyPreview::curve, 180.0, 120.0,
     {{{"x", in, f}, {"value", out, f}, {"curve", out, cv}}}, 3U, {{{"x", "0.5"}, {"points", "0 0 0.5 1 1 0"}}}, 2U,
     "points", false},
    {"envelope", "Envelope", KindFamily::curve, BodyPreview::curve, 180.0, 120.0,
     {{{"time", in, s}, {"value", out, f}}}, 2U,
     {{{"attack", "0.1"}, {"decay", "0.2"}, {"sustain", "0.6"}, {"release", "0.4"}}}, 4U, "attack", false},
    // -- sinks --
    {"plot", "Plot", KindFamily::sink, BodyPreview::sparkline, 200.0, 120.0,
     {{{"signal", in, s}}}, 1U, {}, 0U, "", false},
    {"swatch", "Swatch", KindFamily::sink, BodyPreview::swatch, 150.0, 104.0,
     {{{"colour", in, c}}}, 1U, {}, 0U, "", false},
    {"readout", "Value readout", KindFamily::sink, BodyPreview::readout, 160.0, 72.0,
     {{{"value", in, f}}}, 1U, {{{"digits", "3"}}}, 1U, "digits", false},
    {"curve_view", "Curve view", KindFamily::sink, BodyPreview::curve, 180.0, 120.0,
     {{{"curve", in, cv}}}, 1U, {}, 0U, "", false},
    // -- structure --
    {"reroute", "Reroute", KindFamily::structure, BodyPreview::none, 40.0, 24.0,
     {{{"in", in, f}, {"out", out, f}}}, 2U, {}, 0U, "", false},
}};

} // namespace

std::span<const KindDescriptor> kind_table() noexcept {
    return std::span<const KindDescriptor>(kinds.data(), kinds.size());
}

const KindDescriptor& kind_at(KindIndex index) noexcept {
    // Callers hold indices from find_kind or the table's own range.
    std::size_t at = index.value;
    if (at >= kinds.size()) at = 0U;
    return kinds[at];
}

std::optional<KindIndex> find_kind(std::string_view name) noexcept {
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        if (kinds[index].name == name) return KindIndex{static_cast<std::uint16_t>(index)};
    }
    return std::nullopt;
}

std::string_view kind_family_name(KindFamily family) noexcept {
    switch (family) {
    case KindFamily::source: return "source";
    case KindFamily::math: return "math";
    case KindFamily::colour: return "colour";
    case KindFamily::curve: return "curve";
    case KindFamily::sink: return "sink";
    case KindFamily::structure: return "structure";
    }
    return "unknown";
}

std::optional<std::string_view> kind_default(const KindDescriptor& kind, std::string_view property) noexcept {
    for (std::size_t index = 0U; index < kind.property_count; ++index) {
        if (kind.properties[index].name == property) return kind.properties[index].default_value;
    }
    return std::nullopt;
}

void fill_node_spec(const KindDescriptor& kind, std::string_view key, gf::WorldPoint top_left, gf::NodeSpec& spec) {
    spec.key.assign(key);
    spec.kind.assign(kind.name);
    spec.title.assign(kind.title);
    spec.bounds = gf::WorldRect{top_left.x, top_left.y, kind.width, kind.height};
    spec.ports.clear();
    spec.properties.clear();
    std::size_t inputs = 0U;
    std::size_t outputs = 0U;
    for (std::size_t index = 0U; index < kind.port_count; ++index) {
        if (kind.ports[index].direction == gf::PortDirection::input) {
            ++inputs;
        } else {
            ++outputs;
        }
    }
    // Ports sit below the caption band (24 of the node's height), spread
    // evenly over the rest of their edge.
    const double caption = 24.0;
    const double body = kind.height - caption;
    std::size_t input_number = 0U;
    std::size_t output_number = 0U;
    for (std::size_t index = 0U; index < kind.port_count; ++index) {
        const PortTemplate& source = kind.ports[index];
        gf::PortRecord port{};
        port.name.assign(source.name);
        port.direction = source.direction;
        port.socket_type = socket_type_number(source.type);
        double number = 0.0;
        double count = 1.0;
        if (source.direction == gf::PortDirection::input) {
            port.side = gf::PortSide::left;
            number = static_cast<double>(input_number);
            count = static_cast<double>(inputs);
            ++input_number;
        } else {
            port.side = gf::PortSide::right;
            number = static_cast<double>(output_number);
            count = static_cast<double>(outputs);
            ++output_number;
        }
        const double along = caption + body * (number + 0.5) / count;
        port.offset = along / kind.height;
        if (kind.height <= caption) port.offset = 0.5;
        spec.ports.push_back(std::move(port));
    }
    for (std::size_t index = 0U; index < kind.property_count; ++index) {
        gf::NodeProperty property{};
        property.name.assign(kind.properties[index].name);
        property.value.assign(kind.properties[index].default_value);
        spec.properties.push_back(std::move(property));
    }
}

bool kind_accepts(const KindDescriptor& kind, std::uint16_t from, gf::PortIndex& port) noexcept {
    for (std::size_t index = 0U; index < kind.port_count; ++index) {
        const PortTemplate& candidate = kind.ports[index];
        if (candidate.direction != gf::PortDirection::input) continue;
        if (!connectable(from, socket_type_number(candidate.type))) continue;
        port = gf::PortIndex{static_cast<std::uint16_t>(index)};
        return true;
    }
    return false;
}

} // namespace node_graph
