#pragma once

// The stress graphs and the seeded edit script (NODE_GRAPH_SPEC section 8).
//
// generate_stress_graph writes a deterministic graph: N nodes from the kind
// table in a jittered grid (spacing 1.6 times the mean node size), up to
// links_per_node * N links of which 90 % join nodes within three grid
// cells and 10 % are long links across the graph, and about 1 % reroutes.
// Every link runs from a lower node index to a higher one, so the graph is
// acyclic by construction; an attempt whose ends have no free compatible
// ports is skipped and counted, so the link count is recorded, not assumed.
// Frames around every 64 nodes are not generated: NodeGraph has no frames
// yet (GAPS.md G7).
//
// apply_seeded_edits applies a seeded sequence of the app's edits through
// GraphDocument (each one undo step), for the round-trip invariant: undo
// all, save, and the bytes equal the document before the edits (spec 8.8).

#include "graph_document.hpp"

#include <cstddef>
#include <cstdint>

namespace node_graph {

// splitmix64: a small deterministic generator, the same on every machine.
class SeededRandom final {
public:
    explicit SeededRandom(std::uint64_t seed) noexcept : state_(seed) {}
    [[nodiscard]] std::uint64_t next() noexcept;
    // Uniform in [0, bound); 0 for bound 0. Uses the high bits by
    // multiplication (no division per draw); the bias is below 2^-32 for
    // bounds under 2^32.
    [[nodiscard]] std::uint64_t below(std::uint64_t bound) noexcept;
    // Uniform in [0, 1).
    [[nodiscard]] double unit() noexcept;

private:
    std::uint64_t state_{};
};

struct GeneratorOptions final {
    std::size_t nodes{1000U};
    std::size_t links_per_node{2U};
    std::uint64_t seed{1U};
};

struct GeneratorReport final {
    std::size_t nodes{};
    std::size_t links{};
    std::size_t link_attempts{};
    std::size_t local_links{};
    std::size_t long_links{};
    std::size_t skipped_no_port{};
    std::size_t reroutes{};
    std::size_t grid_columns{};
    double cell_width{};
    double cell_height{};
};

// Replaces the document's graph with the generated one (a fresh graph
// loaded from nothing first, so the undo log is empty and the generated
// operations are then dropped from it: the generated graph is the start).
[[nodiscard]] GeneratorReport generate_stress_graph(GraphDocument& document, const GeneratorOptions& options);

struct EditReport final {
    std::size_t applied{};
    std::size_t refused{};
    std::size_t moves{};
    std::size_t property_edits{};
    std::size_t additions{};
    std::size_t removals{};
    std::size_t connects{};
    std::size_t disconnects{};
    std::size_t title_edits{};
};

[[nodiscard]] EditReport apply_seeded_edits(GraphDocument& document, std::uint64_t seed, std::size_t count);

} // namespace node_graph
