#include "evaluator.hpp"

#include <charconv>
#include <cmath>
#include <system_error>

namespace node_graph {

namespace gf = gui_forms;

namespace {

constexpr double two_pi = 6.283185307179586;

// -- expressions ------------------------------------------------------------------------

int precedence(char op) noexcept {
    if (op == 'u') return 3;
    if (op == '*' || op == '/') return 2;
    if (op == '+' || op == '-') return 1;
    return 0;
}

// Applies `op` to the top of the value stack. False on a missing operand.
bool apply_operator(char op, std::array<double, maximum_expression_tokens>& values, std::size_t& count) noexcept {
    if (op == 'u') {
        if (count < 1U) return false;
        values[count - 1U] = -values[count - 1U];
        return true;
    }
    if (count < 2U) return false;
    const double right = values[count - 1U];
    const double left = values[count - 2U];
    double result = 0.0;
    if (op == '+') {
        result = left + right;
    } else if (op == '-') {
        result = left - right;
    } else if (op == '*') {
        result = left * right;
    } else if (op == '/') {
        result = left / right;
    } else {
        return false;
    }
    values[count - 2U] = result;
    --count;
    return true;
}

bool number_byte(char byte) noexcept {
    const bool digit = byte >= '0' && byte <= '9';
    return digit || byte == '.';
}

// -- small value helpers ---------------------------------------------------------------

double clamp_unit(double value) noexcept {
    if (!(value > 0.0)) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
}

double fraction(double value) noexcept {
    const double whole = std::floor(value);
    return value - whole;
}

std::uint64_t mix_bits(std::uint64_t value) noexcept {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    value = value ^ (value >> 31U);
    return value;
}

bool parse_number(std::string_view text, double& out) noexcept {
    std::size_t first = 0U;
    while (first < text.size() && text[first] == ' ') ++first;
    std::size_t last = text.size();
    while (last > first && text[last - 1U] == ' ') --last;
    if (first == last) return false;
    const char* begin = text.data() + first;
    const char* end = text.data() + last;
    double value = 0.0;
    const std::from_chars_result parsed = std::from_chars(begin, end, value, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return false;
    out = value;
    return true;
}

int hex_digit(char byte) noexcept {
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
    return -1;
}

// "#rrggbb" into linear 0..1 channels with alpha 1. False when malformed.
bool parse_hex_colour(std::string_view text, std::array<double, 4>& out) noexcept {
    if (text.size() != 7U || text[0] != '#') return false;
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
        const int high = hex_digit(text[1U + channel * 2U]);
        const int low = hex_digit(text[2U + channel * 2U]);
        if (high < 0 || low < 0) return false;
        out[channel] = static_cast<double>(high * 16 + low) / 255.0;
    }
    out[3] = 1.0;
    return true;
}

void hsv_to_rgb(double hue, double saturation, double value, std::array<double, 4>& out) noexcept {
    const double h = fraction(hue) * 6.0;
    const double s = clamp_unit(saturation);
    const double v = clamp_unit(value);
    const double sector = std::floor(h);
    const double f = h - sector;
    const double p = v * (1.0 - s);
    const double q = v * (1.0 - s * f);
    const double t = v * (1.0 - s * (1.0 - f));
    const int index = static_cast<int>(sector) % 6;
    double red = v;
    double green = t;
    double blue = p;
    switch (index) {
    case 0: red = v; green = t; blue = p; break;
    case 1: red = q; green = v; blue = p; break;
    case 2: red = p; green = v; blue = t; break;
    case 3: red = p; green = q; blue = v; break;
    case 4: red = t; green = p; blue = v; break;
    case 5: red = v; green = p; blue = q; break;
    default: break;
    }
    out[0] = red;
    out[1] = green;
    out[2] = blue;
    out[3] = 1.0;
}

bool finite_value(const Value& value) noexcept {
    if (!std::isfinite(value.number)) return false;
    for (const double channel : value.colour) {
        if (!std::isfinite(channel)) return false;
    }
    return true;
}

// Converts `value` into `type` along the conversion table; false when the
// table refuses it.
bool convert_value(const Value& value, SocketType type, Value& out) noexcept {
    const Conversion conversion = conversion_between(value.type, type);
    if (conversion == Conversion::refused) return false;
    out = value;
    out.type = type;
    if (value.type == type) return true;
    const bool from_number = value.type == SocketType::float_value || value.type == SocketType::signal ||
                             value.type == SocketType::boolean;
    if (type == SocketType::colour && from_number) {
        const double grey = clamp_unit(value.number);
        out.colour = {grey, grey, grey, 1.0};
        return true;
    }
    if (value.type == SocketType::colour) {
        out.number = 0.2126 * value.colour[0] + 0.7152 * value.colour[1] + 0.0722 * value.colour[2];
    }
    if (type == SocketType::boolean) out.number = out.number > 0.5 ? 1.0 : 0.0;
    return true;
}

struct PendingLamp final {
    gf::PortRef port{};
    gf::PortLamp lamp{gf::PortLamp::off};
};

} // namespace

ExpressionStatus evaluate_expression(std::string_view text, const ExpressionVariables& variables,
                                     double& out) noexcept {
    std::array<double, maximum_expression_tokens> values{};
    std::array<char, maximum_expression_tokens> operators{};
    std::size_t value_count = 0U;
    std::size_t operator_count = 0U;
    std::size_t tokens = 0U;
    std::size_t depth = 0U;
    bool expect_operand = true;
    std::size_t at = 0U;
    while (at < text.size()) {
        const char byte = text[at];
        if (byte == ' ' || byte == '\t') {
            ++at;
            continue;
        }
        ++tokens;
        if (tokens > maximum_expression_tokens) return ExpressionStatus::too_long;
        if (expect_operand) {
            if (byte == '-') {
                operators[operator_count] = 'u';
                ++operator_count;
                ++at;
                continue;
            }
            if (byte == '(') {
                ++depth;
                if (depth > maximum_expression_depth) return ExpressionStatus::too_deep;
                operators[operator_count] = '(';
                ++operator_count;
                ++at;
                continue;
            }
            double operand = 0.0;
            if (byte == 'a') {
                operand = variables.a;
                ++at;
            } else if (byte == 'b') {
                operand = variables.b;
                ++at;
            } else if (byte == 't') {
                operand = variables.t;
                ++at;
            } else if (number_byte(byte)) {
                std::size_t end = at;
                while (end < text.size() && number_byte(text[end])) ++end;
                if (!parse_number(text.substr(at, end - at), operand)) return ExpressionStatus::syntax;
                at = end;
            } else {
                return ExpressionStatus::syntax;
            }
            values[value_count] = operand;
            ++value_count;
            expect_operand = false;
            continue;
        }
        if (byte == ')') {
            if (depth == 0U) return ExpressionStatus::syntax;
            while (operator_count > 0U && operators[operator_count - 1U] != '(') {
                if (!apply_operator(operators[operator_count - 1U], values, value_count)) return ExpressionStatus::syntax;
                --operator_count;
            }
            if (operator_count == 0U) return ExpressionStatus::syntax;
            --operator_count;
            --depth;
            ++at;
            continue;
        }
        if (byte != '+' && byte != '-' && byte != '*' && byte != '/') return ExpressionStatus::syntax;
        // Left-associative binary operators: apply every stacked operator of
        // at least this precedence first.
        while (operator_count > 0U && operators[operator_count - 1U] != '(' &&
               precedence(operators[operator_count - 1U]) >= precedence(byte)) {
            if (!apply_operator(operators[operator_count - 1U], values, value_count)) return ExpressionStatus::syntax;
            --operator_count;
        }
        operators[operator_count] = byte;
        ++operator_count;
        expect_operand = true;
        ++at;
    }
    if (expect_operand || depth != 0U) return ExpressionStatus::syntax;
    while (operator_count > 0U) {
        if (!apply_operator(operators[operator_count - 1U], values, value_count)) return ExpressionStatus::syntax;
        --operator_count;
    }
    if (value_count != 1U) return ExpressionStatus::syntax;
    if (!std::isfinite(values[0])) return ExpressionStatus::not_finite;
    out = values[0];
    return ExpressionStatus::ok;
}

bool parse_curve_points(std::string_view text, CurvePoints& points) noexcept {
    points.count = 0U;
    std::array<double, maximum_curve_points * 2U> numbers{};
    std::size_t number_count = 0U;
    std::size_t at = 0U;
    while (at < text.size() && number_count < numbers.size()) {
        while (at < text.size() && text[at] == ' ') ++at;
        if (at >= text.size()) break;
        std::size_t end = at;
        while (end < text.size() && text[end] != ' ') ++end;
        double number = 0.0;
        if (!parse_number(text.substr(at, end - at), number) || !std::isfinite(number)) return false;
        numbers[number_count] = number;
        ++number_count;
        at = end;
    }
    const std::size_t pairs = number_count / 2U;
    // Insertion by x: at most maximum_curve_points points.
    for (std::size_t pair = 0U; pair < pairs; ++pair) {
        const double x = numbers[pair * 2U];
        const double y = numbers[pair * 2U + 1U];
        std::size_t place = points.count;
        while (place > 0U && points.x[place - 1U] > x) {
            points.x[place] = points.x[place - 1U];
            points.y[place] = points.y[place - 1U];
            --place;
        }
        points.x[place] = x;
        points.y[place] = y;
        ++points.count;
    }
    return true;
}

double sample_curve_points(const CurvePoints& points, double x) noexcept {
    if (points.count == 0U) return 0.0;
    if (x <= points.x[0]) return points.y[0];
    const std::size_t last = points.count - 1U;
    if (x >= points.x[last]) return points.y[last];
    for (std::size_t index = 1U; index < points.count; ++index) {
        if (x > points.x[index]) continue;
        const double span = points.x[index] - points.x[index - 1U];
        if (span <= 0.0) return points.y[index];
        const double t = (x - points.x[index - 1U]) / span;
        const double y = points.y[index - 1U] + (points.y[index] - points.y[index - 1U]) * t;
        return y;
    }
    return points.y[last];
}

// -- Evaluator ------------------------------------------------------------------------------

Evaluator::Evaluator(GraphDocument& document) : document_(document) {}

const NodeState* Evaluator::state(std::uint32_t slot) const noexcept {
    if (slot >= states_.size()) return nullptr;
    return &states_[slot];
}

const PlotHistory* Evaluator::history(const NodeState& state) const noexcept {
    if (!state.history.has_value() || *state.history >= histories_.size()) return nullptr;
    return &histories_[*state.history];
}

const CurvePoints* Evaluator::curve(std::uint32_t slot) const noexcept {
    if (slot >= curve_index_.size() || !curve_index_[slot].has_value()) return nullptr;
    const std::uint32_t index = *curve_index_[slot];
    if (index >= curves_.size()) return nullptr;
    return &curves_[index];
}

bool Evaluator::evaluate(double time_seconds) {
    const gf::NodeGraph& graph = document_.graph();
    if (graph.revision() != evaluated_revision_) {
        full_pass(time_seconds);
        return true;
    }
    if (time_seconds != evaluated_time_ && time_dependents_ > 0U) {
        time_pass(time_seconds);
        return true;
    }
    return false;
}

double Evaluator::number_property(const gf::NodeRecord& node, std::string_view name, double fallback) const noexcept {
    for (const gf::NodeProperty& property : node.properties) {
        if (property.name != name) continue;
        double value = 0.0;
        if (!parse_number(property.value, value)) return std::nan("");
        return value;
    }
    return fallback;
}

bool Evaluator::read_input(std::uint32_t slot, const gf::NodeRecord& node, std::uint16_t port, SocketType type,
                           Value& out) const {
    const gf::NodeGraph& graph = document_.graph();
    const gf::PortRef input{gf::NodeId{slot, node.generation}, gf::PortIndex{port}};
    const gf::LinkId link_id = graph.link_into(input);
    const gf::LinkRecord* link = graph.link(link_id);
    if (link != nullptr) {
        const std::uint32_t source = (*link).from.node.index;
        if (source >= states_.size()) return false;
        const NodeState& from = states_[source];
        if (from.evaluation != NodeEvaluation::ok) return false;
        const std::size_t from_port = (*link).from.port.value;
        if (from_port >= maximum_kind_ports) return false;
        const bool converted = convert_value(from.outputs[from_port], type, out);
        return converted;
    }
    // Unconnected: the property of the port's name (the value bar), or 0.
    Value fallback{};
    fallback.type = type;
    const std::string_view name = node.ports[port].name;
    fallback.number = number_property(node, name, 0.0);
    if (type == SocketType::colour) fallback.colour = {0.5, 0.5, 0.5, 1.0};
    out = fallback;
    return std::isfinite(fallback.number);
}

void Evaluator::full_pass(double time_seconds) {
    ++counters_.full_passes;
    const Topology& topology = document_.topology();
    const gf::NodeGraph& graph = document_.graph();
    const std::span<const gf::NodeRecord> nodes = graph.node_slots();
    const std::size_t slots = nodes.size();
    states_.resize(slots);
    curve_index_.assign(slots, std::nullopt);
    curves_.clear();
    // Plot histories survive a full pass for the same node (slot and
    // generation), so a plot keeps its trace while the graph is edited.
    std::size_t plots = 0U;
    for (std::size_t slot = 0U; slot < slots; ++slot) {
        const gf::NodeRecord& node = nodes[slot];
        NodeState& state = states_[slot];
        const bool same_node = node.live && state.generation == node.generation;
        state.evaluation = NodeEvaluation::unevaluated;
        state.time_dependent = false;
        state.generation = node.generation;
        if (!node.live) {
            state.kind.reset();
            state.history.reset();
            continue;
        }
        state.kind = find_kind(node.kind);
        const bool plot = state.kind.has_value() && kind_at(*state.kind).preview == BodyPreview::sparkline;
        if (!plot) {
            state.history.reset();
            continue;
        }
        if (!same_node || !state.history.has_value()) state.history = static_cast<std::uint32_t>(histories_.size() + plots);
        ++plots;
    }
    // Compact the histories: keep each live plot's in a fresh array.
    std::vector<PlotHistory> kept{};
    kept.reserve(plots);
    for (std::size_t slot = 0U; slot < slots; ++slot) {
        NodeState& state = states_[slot];
        if (!state.history.has_value()) continue;
        PlotHistory history{};
        if (*state.history < histories_.size()) history = histories_[*state.history];
        state.history = static_cast<std::uint32_t>(kept.size());
        kept.push_back(history);
    }
    histories_ = std::move(kept);

    // Kahn's order: in-degrees from the topology, then a queue of ready
    // slots. Slots left over sit on a cycle.
    in_degree_.assign(slots, 0U);
    for (const std::uint32_t target : topology.targets) ++in_degree_[target];
    queue_.clear();
    order_.clear();
    for (std::size_t slot = 0U; slot < slots; ++slot) {
        if (nodes[slot].live && in_degree_[slot] == 0U) queue_.push_back(static_cast<std::uint32_t>(slot));
    }
    std::size_t head = 0U;
    while (head < queue_.size()) {
        const std::uint32_t slot = queue_[head];
        ++head;
        order_.push_back(slot);
        const std::uint32_t first = topology.offsets[slot];
        const std::uint32_t last = topology.offsets[static_cast<std::size_t>(slot) + 1U];
        for (std::uint32_t at = first; at < last; ++at) {
            const std::uint32_t target = topology.targets[at];
            --in_degree_[target];
            if (in_degree_[target] == 0U) queue_.push_back(target);
        }
    }
    time_dependents_ = 0U;
    for (const std::uint32_t slot : order_) {
        NodeState& state = states_[slot];
        bool dependent = state.kind.has_value() && kind_at(*state.kind).time_driven;
        const gf::NodeRecord& node = nodes[slot];
        for (std::size_t port = 0U; port < node.ports.size() && !dependent; ++port) {
            if (node.ports[port].direction != gf::PortDirection::input) continue;
            const gf::PortRef input{gf::NodeId{slot, node.generation}, gf::PortIndex{static_cast<std::uint16_t>(port)}};
            const gf::LinkRecord* link = graph.link(graph.link_into(input));
            if (link == nullptr) continue;
            const std::uint32_t source = (*link).from.node.index;
            if (source < states_.size() && states_[source].time_dependent) dependent = true;
        }
        state.time_dependent = dependent;
        if (dependent) ++time_dependents_;
        evaluate_node(slot, time_seconds, true);
    }
    std::size_t cycle_nodes = 0U;
    for (std::size_t slot = 0U; slot < slots; ++slot) {
        if (!nodes[slot].live || states_[slot].evaluation != NodeEvaluation::unevaluated) continue;
        states_[slot].evaluation = NodeEvaluation::failed;
        ++cycle_nodes;
    }
    counters_.cycle_nodes += cycle_nodes;
    // Lamps last: a lamp write is an operation, which ends the borrows of
    // the records read above.
    std::vector<PendingLamp> lamps{};
    for (std::size_t slot = 0U; slot < slots; ++slot) {
        const gf::NodeRecord& node = nodes[slot];
        if (!node.live) continue;
        const NodeEvaluation evaluation = states_[slot].evaluation;
        for (std::size_t port = 0U; port < node.ports.size(); ++port) {
            if (node.ports[port].direction != gf::PortDirection::output) continue;
            gf::PortLamp lamp = gf::PortLamp::off;
            if (evaluation == NodeEvaluation::ok) lamp = gf::PortLamp::on;
            if (evaluation == NodeEvaluation::failed) lamp = gf::PortLamp::error;
            if (node.ports[port].lamp == lamp) continue;
            const gf::PortRef ref{gf::NodeId{static_cast<std::uint32_t>(slot), node.generation},
                                  gf::PortIndex{static_cast<std::uint16_t>(port)}};
            lamps.push_back(PendingLamp{ref, lamp});
        }
    }
    for (const PendingLamp& pending : lamps) {
        if (document_.set_lamp(pending.port, pending.lamp)) ++counters_.lamp_writes;
    }
    evaluated_revision_ = document_.graph().revision();
    evaluated_time_ = time_seconds;
}

void Evaluator::time_pass(double time_seconds) {
    ++counters_.time_passes;
    for (const std::uint32_t slot : order_) {
        if (slot >= states_.size() || !states_[slot].time_dependent) continue;
        evaluate_node(slot, time_seconds, true);
    }
    evaluated_time_ = time_seconds;
}

void Evaluator::evaluate_node(std::uint32_t slot, double time_seconds, bool record_history) {
    const gf::NodeGraph& graph = document_.graph();
    const gf::NodeRecord& node = graph.node_slots()[slot];
    NodeState& state = states_[slot];
    ++counters_.nodes_evaluated;
    if (!state.kind.has_value()) {
        state.evaluation = NodeEvaluation::failed;
        ++counters_.failures;
        return;
    }
    const KindDescriptor& kind = kind_at(*state.kind);
    // Read every input first (a failed read fails the node).
    bool readable = true;
    for (std::size_t port = 0U; port < kind.port_count && port < node.ports.size(); ++port) {
        if (kind.ports[port].direction != gf::PortDirection::input) continue;
        Value value{};
        if (!read_input(slot, node, static_cast<std::uint16_t>(port), kind.ports[port].type, value)) readable = false;
        state.inputs[port] = value;
    }
    if (!readable) {
        state.evaluation = NodeEvaluation::failed;
        ++counters_.failures;
        return;
    }
    const std::array<Value, maximum_kind_ports>& in = state.inputs;
    std::array<Value, maximum_kind_ports>& out = state.outputs;
    for (std::size_t port = 0U; port < kind.port_count; ++port) out[port] = Value{kind.ports[port].type, 0.0, {}, 0U};
    const std::string_view name = kind.name;
    bool ok = true;
    if (name == "constant") {
        out[0].number = number_property(node, "value", 0.0);
    } else if (name == "time") {
        out[0].number = time_seconds * number_property(node, "speed", 1.0);
    } else if (name == "oscillator") {
        const double phase = fraction(time_seconds * in[0].number);
        const double amplitude = in[1].number;
        const gf::NodeProperty* shape = graph.property(gf::NodeId{slot, node.generation}, "shape");
        double wave = std::sin(two_pi * phase);
        if (shape != nullptr && (*shape).value == "square") wave = phase < 0.5 ? 1.0 : -1.0;
        if (shape != nullptr && (*shape).value == "saw") wave = 2.0 * phase - 1.0;
        out[2].number = amplitude * wave;
    } else if (name == "noise") {
        const double seed = number_property(node, "seed", 1.0);
        const double cell = std::floor(in[0].number * 1000.0);
        const std::uint64_t bits = mix_bits(static_cast<std::uint64_t>(static_cast<std::int64_t>(seed)) ^
                                            mix_bits(static_cast<std::uint64_t>(static_cast<std::int64_t>(cell))));
        out[1].number = static_cast<double>(bits >> 11U) / 9007199254740992.0;
    } else if (name == "add") {
        out[2].number = in[0].number + in[1].number;
    } else if (name == "multiply") {
        out[2].number = in[0].number * in[1].number;
    } else if (name == "mix") {
        out[3].number = in[0].number + (in[1].number - in[0].number) * in[2].number;
    } else if (name == "clamp") {
        double value = in[0].number;
        if (value < in[1].number) value = in[1].number;
        if (value > in[2].number) value = in[2].number;
        out[3].number = value;
    } else if (name == "map_range") {
        const double span = in[2].number - in[1].number;
        const double t = (in[0].number - in[1].number) / span;
        out[5].number = in[3].number + (in[4].number - in[3].number) * t;
    } else if (name == "expression") {
        const gf::NodeProperty* text = graph.property(gf::NodeId{slot, node.generation}, "text");
        const ExpressionVariables variables{in[0].number, in[1].number, time_seconds};
        double result = 0.0;
        const std::string_view source = text != nullptr ? std::string_view((*text).value) : std::string_view();
        ok = evaluate_expression(source, variables, result) == ExpressionStatus::ok;
        out[2].number = result;
    } else if (name == "rgb") {
        out[3].colour = {clamp_unit(in[0].number), clamp_unit(in[1].number), clamp_unit(in[2].number), 1.0};
    } else if (name == "hsv") {
        hsv_to_rgb(in[0].number, in[1].number, in[2].number, out[3].colour);
    } else if (name == "gradient_sample") {
        std::array<double, 4> from{};
        std::array<double, 4> to{};
        const gf::NodeProperty* from_text = graph.property(gf::NodeId{slot, node.generation}, "from");
        const gf::NodeProperty* to_text = graph.property(gf::NodeId{slot, node.generation}, "to");
        ok = from_text != nullptr && to_text != nullptr && parse_hex_colour((*from_text).value, from) &&
             parse_hex_colour((*to_text).value, to);
        const double t = clamp_unit(in[0].number);
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
            out[1].colour[channel] = from[channel] + (to[channel] - from[channel]) * t;
        }
    } else if (name == "mix_colour") {
        const double t = clamp_unit(in[2].number);
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
            out[3].colour[channel] = in[0].colour[channel] + (in[1].colour[channel] - in[0].colour[channel]) * t;
        }
    } else if (name == "curve") {
        const gf::NodeProperty* points_text = graph.property(gf::NodeId{slot, node.generation}, "points");
        CurvePoints points{};
        ok = points_text != nullptr && parse_curve_points((*points_text).value, points);
        if (ok) {
            if (!curve_index_[slot].has_value()) {
                curve_index_[slot] = static_cast<std::uint32_t>(curves_.size());
                curves_.push_back(points);
            } else {
                curves_[*curve_index_[slot]] = points;
            }
        }
        out[1].number = sample_curve_points(points, in[0].number);
        out[2].curve_slot = slot;
    } else if (name == "envelope") {
        const double attack = number_property(node, "attack", 0.1);
        const double decay = number_property(node, "decay", 0.2);
        const double sustain = number_property(node, "sustain", 0.6);
        const double release = number_property(node, "release", 0.4);
        const double cycle = attack + decay + 1.0 + release;
        ok = attack >= 0.0 && decay >= 0.0 && release >= 0.0 && cycle > 0.0;
        const double t = ok ? std::fmod(std::fabs(in[0].number), cycle) : 0.0;
        double level = 0.0;
        if (t < attack) {
            level = attack > 0.0 ? t / attack : 1.0;
        } else if (t < attack + decay) {
            level = 1.0 - (1.0 - sustain) * (t - attack) / decay;
        } else if (t < attack + decay + 1.0) {
            level = sustain;
        } else {
            level = release > 0.0 ? sustain * (1.0 - (t - attack - decay - 1.0) / release) : 0.0;
        }
        out[1].number = level;
    } else if (name == "plot") {
        if (record_history && state.history.has_value() && *state.history < histories_.size()) {
            PlotHistory& history = histories_[*state.history];
            history.samples[history.head] = in[0].number;
            history.head = (history.head + 1U) % plot_history_samples;
            if (history.count < plot_history_samples) ++history.count;
        }
    } else if (name == "readout" || name == "swatch" || name == "curve_view") {
        // Sinks read their input; the canvas shows it from `inputs`.
    } else if (name == "reroute") {
        out[1] = in[0];
    } else {
        ok = false;
    }
    for (std::size_t port = 0U; port < kind.port_count && ok; ++port) {
        if (!finite_value(out[port])) ok = false;
    }
    state.evaluation = ok ? NodeEvaluation::ok : NodeEvaluation::failed;
    if (!ok) ++counters_.failures;
}

} // namespace node_graph
