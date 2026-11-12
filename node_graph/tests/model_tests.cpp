// The model without a window: socket conversions, the kind table, the
// evaluator (values, lamps, failures), the bounded expression parser and
// the document's cycle refusal.

#include "../src/evaluator.hpp"
#include "../src/graph_document.hpp"
#include "../src/kinds.hpp"
#include "../src/socket_types.hpp"
#include "test_support.hpp"

#include <cmath>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>

namespace gf = gui_forms;
using node_graph_tests::require;

namespace {

node_graph::KindIndex kind_named(const char* name) {
    const std::optional<node_graph::KindIndex> kind = node_graph::find_kind(name);
    require(kind.has_value(), std::string("the kind table has ") + name);
    return *kind;
}

gf::NodeId add(node_graph::GraphDocument& document, const char* kind, double x, double y) {
    const gf::GraphResult result = document.add_node(kind_named(kind), gf::WorldPoint{x, y});
    require(result.status == gf::GraphStatus::ok, std::string("a node of kind ") + kind + " is added");
    return result.node;
}

gf::PortRef port(gf::NodeId node, std::uint16_t index) {
    return gf::PortRef{node, gf::PortIndex{index}};
}

void conversions() {
    using node_graph::Conversion;
    using node_graph::SocketType;
    require(node_graph::conversion_between(SocketType::float_value, SocketType::float_value) == Conversion::identity,
            "float to float is identity");
    require(node_graph::conversion_between(SocketType::curve, SocketType::colour) == Conversion::refused,
            "curve to colour is refused");
    for (std::uint16_t from = 0U; from < node_graph::socket_type_count; ++from) {
        const std::optional<SocketType> type = node_graph::socket_type_from_number(from);
        require(type.has_value(), "every socket number names a type");
        require(node_graph::socket_type_from_number(node_graph::socket_type_number(*type)) == type,
                "socket numbers round-trip");
        require(node_graph::connectable(from, from), "a type connects to itself");
    }
    require(!node_graph::socket_type_from_number(node_graph::socket_type_count).has_value(),
            "a number past the table names no type");
}

void kind_table_is_well_formed() {
    for (const node_graph::KindDescriptor& kind : node_graph::kind_table()) {
        require(kind.port_count <= node_graph::maximum_kind_ports, "port counts fit their arrays");
        require(kind.property_count <= node_graph::maximum_kind_properties, "property counts fit their arrays");
        require(node_graph::find_kind(kind.name).has_value(), "every kind is found by its name");
        if (!kind.primary_property.empty()) {
            require(node_graph::kind_default(kind, kind.primary_property).has_value(),
                    "a primary property is a declared property");
        }
    }
    gf::PortIndex input{};
    require(node_graph::kind_accepts(node_graph::kind_at(kind_named("add")), 0U, input), "add accepts a float");
}

void evaluation_and_lamps() {
    node_graph::GraphDocument document{};
    node_graph::Evaluator evaluator(document);
    const gf::NodeId two = add(document, "constant", 0.0, 0.0);
    const gf::NodeId three = add(document, "constant", 0.0, 120.0);
    const gf::NodeId sum = add(document, "add", 240.0, 40.0);
    const gf::NodeId readout = add(document, "readout", 480.0, 40.0);
    require(document.set_property(two, "value", "2").status == gf::GraphStatus::ok, "the first constant is 2");
    require(document.set_property(three, "value", "3").status == gf::GraphStatus::ok, "the second constant is 3");
    require(document.connect(port(two, 0U), port(sum, 0U)).status == node_graph::ConnectStatus::connected, "2 -> a");
    require(document.connect(port(three, 0U), port(sum, 1U)).status == node_graph::ConnectStatus::connected, "3 -> b");
    require(document.connect(port(sum, 2U), port(readout, 0U)).status == node_graph::ConnectStatus::connected,
            "sum -> readout");
    static_cast<void>(evaluator.evaluate(0.0));
    const node_graph::NodeState* state = evaluator.state(sum.index);
    require(state != nullptr && (*state).evaluation == node_graph::NodeEvaluation::ok, "add evaluates");
    require((*state).outputs[2].number == 5.0, "2 + 3 is 5");
    const node_graph::NodeState* shown = evaluator.state(readout.index);
    require(shown != nullptr && (*shown).inputs[0].number == 5.0, "the readout reads 5");
    const gf::PortRecord* lamp = document.graph().port(port(sum, 2U));
    require(lamp != nullptr && (*lamp).lamp == gf::PortLamp::on, "a good output's lamp is on");

    // An expression that divides by zero fails, and its lamp says so.
    const gf::NodeId expression = add(document, "expression", 240.0, 240.0);
    require(document.set_property(expression, "text", "a / b").status == gf::GraphStatus::ok, "a / b");
    static_cast<void>(evaluator.evaluate(0.0));
    const gf::PortRecord* failed = document.graph().port(port(expression, 2U));
    require(failed != nullptr && (*failed).lamp == gf::PortLamp::error, "0 / 0 lights the error lamp");
    require(evaluator.counters().failures > 0U, "the failure is counted");

    // Lamp writes are not undo steps.
    const std::size_t steps = document.graph().undo_steps();
    static_cast<void>(evaluator.evaluate(1.0));
    require(document.graph().undo_steps() == steps, "evaluation records no undo step");
}

void expressions_are_bounded() {
    double out = 0.0;
    const node_graph::ExpressionVariables variables{2.0, 3.0, 0.5};
    require(node_graph::evaluate_expression("a * b + 1", variables, out) == node_graph::ExpressionStatus::ok && out == 7.0,
            "a * b + 1 is 7");
    require(node_graph::evaluate_expression("-(a - b) * t", variables, out) == node_graph::ExpressionStatus::ok &&
                out == 0.5,
            "unary minus and parentheses");
    require(node_graph::evaluate_expression("a +", variables, out) == node_graph::ExpressionStatus::syntax,
            "a dangling operator is a syntax error");
    std::string deep(node_graph::maximum_expression_depth + 1U, '(');
    deep.push_back('1');
    deep.append(node_graph::maximum_expression_depth + 1U, ')');
    require(node_graph::evaluate_expression(deep, variables, out) == node_graph::ExpressionStatus::too_deep,
            "nesting past the bound is refused");
    std::string long_text("1");
    for (std::size_t index = 0U; index < node_graph::maximum_expression_tokens; ++index) long_text.append("+1");
    require(node_graph::evaluate_expression(long_text, variables, out) == node_graph::ExpressionStatus::too_long,
            "a text past the token bound is refused");

    node_graph::CurvePoints points{};
    require(node_graph::parse_curve_points("0 0 1 1", points) && points.count == 2U, "curve points parse");
    require(node_graph::sample_curve_points(points, 0.25) == 0.25, "linear between points");
    require(node_graph::sample_curve_points(points, 4.0) == 1.0, "held past the last point");
}

void cycles_are_refused() {
    node_graph::GraphDocument document{};
    const gf::NodeId first = add(document, "add", 0.0, 0.0);
    const gf::NodeId second = add(document, "add", 240.0, 0.0);
    require(document.connect(port(first, 2U), port(second, 0U)).status == node_graph::ConnectStatus::connected,
            "first -> second");
    std::string before{};
    document.save_text(before);
    const node_graph::ConnectResult back = document.connect(port(second, 2U), port(first, 0U));
    require(back.status == node_graph::ConnectStatus::refused_cycle, "second -> first would close a cycle");
    std::string after{};
    document.save_text(after);
    require(before == after, "a refused connection leaves the document unchanged");
    require(document.counters().connects_refused_cycle == 1U, "the refusal is counted");
    const gf::NodeId colour = add(document, "rgb", 0.0, 200.0);
    const gf::NodeId curve_view = add(document, "curve_view", 240.0, 200.0);
    require(document.connect(port(colour, 3U), port(curve_view, 0U)).status == node_graph::ConnectStatus::refused_type,
            "a colour does not reach a curve input");
}

} // namespace

int main() {
    try {
        conversions();
        kind_table_is_well_formed();
        evaluation_and_lamps();
        expressions_are_bounded();
        cycles_are_refused();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "model_tests: %s\n", failure.what());
        return 1;
    }
    std::printf("model_tests: ok\n");
    return 0;
}
