#pragma once

// Evaluation (NODE_GRAPH_SPEC section 4.1): pull-based, topological and
// bounded. The engine never evaluates; this is the app's. A full pass runs
// when the graph's revision has moved on (Kahn's order over the document's
// topology, an explicit queue, never recursion); between full passes a time
// step re-evaluates only the nodes downstream of a time-driven source. A
// node whose inputs cannot be converted, whose arithmetic gives NaN or an
// infinity, whose kind the table lacks, or which sits on a cycle a loaded
// document brought in, fails: its outputs' lamps show `error`.
//
// Ownership: the evaluator borrows the document (which must outlive it)
// and owns its per-slot state, sized to the graph's node slots and reused.
// One thread: the window's.

#include "graph_document.hpp"
#include "kinds.hpp"
#include "socket_types.hpp"

#include <gui_forms/controls/node_graph.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace node_graph {

// One value on a port. `number` serves float, signal and boolean (0 or 1);
// `colour` is linear red, green, blue and alpha in 0..1; `curve_slot` names
// the node slot whose control points a curve value samples.
struct Value final {
    SocketType type{SocketType::float_value};
    double number{};
    std::array<double, 4> colour{};
    std::uint32_t curve_slot{};
};

// The most samples a plot node keeps (its sparkline's width in samples).
inline constexpr std::size_t plot_history_samples = 64U;
// The most control points a curve keeps; more are ignored.
inline constexpr std::size_t maximum_curve_points = 16U;

struct PlotHistory final {
    std::array<double, plot_history_samples> samples{};
    std::size_t head{};
    std::size_t count{};
};

struct CurvePoints final {
    std::array<double, maximum_curve_points> x{};
    std::array<double, maximum_curve_points> y{};
    std::size_t count{};
};

enum class NodeEvaluation : std::uint8_t { unevaluated, ok, failed };

// A node slot's evaluation state. `inputs` hold the converted values its
// inputs read (for value bars and previews), `outputs` what it produced.
struct NodeState final {
    std::array<Value, maximum_kind_ports> inputs{};
    std::array<Value, maximum_kind_ports> outputs{};
    NodeEvaluation evaluation{NodeEvaluation::unevaluated};
    std::optional<KindIndex> kind{};
    // Downstream of a time-driven source (re-evaluated on each time step).
    bool time_dependent{};
    // Index into the plot histories, or none.
    std::optional<std::uint32_t> history{};
    std::uint32_t generation{};
};

struct EvaluatorCounters final {
    std::uint64_t full_passes{};
    std::uint64_t time_passes{};
    std::uint64_t nodes_evaluated{};
    std::uint64_t failures{};
    std::uint64_t cycle_nodes{};
    std::uint64_t lamp_writes{};
};

// Bounded arithmetic for the Expression kind: numbers, the variables a, b
// and t, + - * / and unary minus, parentheses nested at most
// maximum_expression_depth deep, at most maximum_expression_tokens tokens.
inline constexpr std::size_t maximum_expression_tokens = 256U;
inline constexpr std::size_t maximum_expression_depth = 32U;

enum class ExpressionStatus : std::uint8_t { ok, syntax, too_long, too_deep, not_finite };

struct ExpressionVariables final {
    double a{};
    double b{};
    double t{};
};

// Evaluates `text` with an explicit operator and value stack (shunting
// yard), never recursion; `out` is written only on ok.
[[nodiscard]] ExpressionStatus evaluate_expression(std::string_view text, const ExpressionVariables& variables,
                                                   double& out) noexcept;

// Parses "x0 y0 x1 y1 ..." into `points` (x ascending is not required; the
// points are sorted by x). False when a number is malformed.
[[nodiscard]] bool parse_curve_points(std::string_view text, CurvePoints& points) noexcept;
// Piecewise linear through the points, held at the ends; 0 for no points.
[[nodiscard]] double sample_curve_points(const CurvePoints& points, double x) noexcept;

class Evaluator final {
public:
    explicit Evaluator(GraphDocument& document);
    Evaluator(const Evaluator&) = delete;
    Evaluator& operator=(const Evaluator&) = delete;

    // Brings every value up to date at `time_seconds`: a full pass when the
    // graph changed, otherwise a time pass when the time moved and a node
    // depends on it, otherwise nothing. Writes changed lamps to the graph.
    // Returns whether anything was evaluated.
    bool evaluate(double time_seconds);

    // Whether some node is downstream of a time-driven source.
    [[nodiscard]] bool has_time_dependents() const noexcept { return time_dependents_ > 0U; }
    // The state of node slot `slot`, or null past the slots.
    [[nodiscard]] const NodeState* state(std::uint32_t slot) const noexcept;
    [[nodiscard]] const PlotHistory* history(const NodeState& state) const noexcept;
    // The control points a curve value samples (the curve node's own).
    [[nodiscard]] const CurvePoints* curve(std::uint32_t slot) const noexcept;
    [[nodiscard]] EvaluatorCounters counters() const noexcept { return counters_; }

private:
    void full_pass(double time_seconds);
    void time_pass(double time_seconds);
    void evaluate_node(std::uint32_t slot, double time_seconds, bool record_history);
    // The value input `port` of slot `slot` reads, converted to `type`;
    // false when it cannot be read or converted.
    [[nodiscard]] bool read_input(std::uint32_t slot, const gui_forms::NodeRecord& node, std::uint16_t port,
                                  SocketType type, Value& out) const;
    [[nodiscard]] double number_property(const gui_forms::NodeRecord& node, std::string_view name,
                                         double fallback) const noexcept;

    GraphDocument& document_;
    std::vector<NodeState> states_{};
    std::vector<PlotHistory> histories_{};
    std::vector<CurvePoints> curves_{};
    // Per slot: index into curves_ (curve kinds only), or none.
    std::vector<std::optional<std::uint32_t>> curve_index_{};
    // Kahn's order and its work storage: in-degrees and the queue.
    std::vector<std::uint32_t> order_{};
    std::vector<std::uint32_t> in_degree_{};
    std::vector<std::uint32_t> queue_{};
    std::uint64_t evaluated_revision_{};
    double evaluated_time_{-1.0};
    std::size_t time_dependents_{};
    EvaluatorCounters counters_{};
};

} // namespace node_graph
