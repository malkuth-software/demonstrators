#include "generator.hpp"

#include "kinds.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace node_graph {

namespace gf = gui_forms;

namespace {

constexpr std::string_view empty_document = "gui-forms-graph 1;\nend;\n";

void append_number(std::string& out, std::uint64_t value) {
    char digits[24]{};
    const std::to_chars_result written = std::to_chars(digits, digits + sizeof(digits), value);
    out.append(digits, static_cast<std::size_t>(written.ptr - digits));
}

// The first output of `from` and the first free input of `to` that it can
// feed; false when there is none.
bool find_ports(const gf::NodeGraph& graph, gf::NodeId from, gf::NodeId to, gf::PortRef& output, gf::PortRef& input) {
    const gf::NodeRecord* from_node = graph.node(from);
    const gf::NodeRecord* to_node = graph.node(to);
    if (from_node == nullptr || to_node == nullptr) return false;
    for (std::size_t out_port = 0U; out_port < (*from_node).ports.size(); ++out_port) {
        const gf::PortRecord& source = (*from_node).ports[out_port];
        if (source.direction != gf::PortDirection::output) continue;
        for (std::size_t in_port = 0U; in_port < (*to_node).ports.size(); ++in_port) {
            const gf::PortRecord& target = (*to_node).ports[in_port];
            if (target.direction != gf::PortDirection::input) continue;
            if (!connectable(source.socket_type, target.socket_type)) continue;
            const gf::PortRef candidate{to, gf::PortIndex{static_cast<std::uint16_t>(in_port)}};
            const gf::LinkId occupant = graph.link_into(candidate);
            if (graph.link(occupant) != nullptr) continue;
            output = gf::PortRef{from, gf::PortIndex{static_cast<std::uint16_t>(out_port)}};
            input = candidate;
            return true;
        }
    }
    return false;
}

// A live node chosen at random, or false after a bounded number of tries.
bool random_live_node(const gf::NodeGraph& graph, SeededRandom& random, gf::NodeId& out) {
    const std::span<const gf::NodeRecord> slots = graph.node_slots();
    if (slots.empty()) return false;
    for (std::size_t attempt = 0U; attempt < 64U; ++attempt) {
        const std::size_t slot = static_cast<std::size_t>(random.below(slots.size()));
        if (!slots[slot].live) continue;
        out = gf::NodeId{static_cast<std::uint32_t>(slot), slots[slot].generation};
        return true;
    }
    return false;
}

bool random_live_link(const gf::NodeGraph& graph, SeededRandom& random, gf::LinkId& out) {
    const std::span<const gf::LinkRecord> slots = graph.link_slots();
    if (slots.empty()) return false;
    for (std::size_t attempt = 0U; attempt < 64U; ++attempt) {
        const std::size_t slot = static_cast<std::size_t>(random.below(slots.size()));
        if (!slots[slot].live) continue;
        out = gf::LinkId{static_cast<std::uint32_t>(slot), slots[slot].generation};
        return true;
    }
    return false;
}

} // namespace

std::uint64_t SeededRandom::next() noexcept {
    state_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t value = state_;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    value = value ^ (value >> 31U);
    return value;
}

std::uint64_t SeededRandom::below(std::uint64_t bound) noexcept {
    if (bound == 0U) return 0U;
    const std::uint64_t high = next() >> 32U;
    const std::uint64_t scaled = (high * (bound & 0xFFFFFFFFULL)) >> 32U;
    if (bound <= 0xFFFFFFFFULL) return scaled;
    const std::uint64_t value = next() % bound;
    return value;
}

double SeededRandom::unit() noexcept {
    const std::uint64_t bits = next() >> 11U;
    const double value = static_cast<double>(bits) / 9007199254740992.0;
    return value;
}

GeneratorReport generate_stress_graph(GraphDocument& document, const GeneratorOptions& options) {
    GeneratorReport report{};
    LoadReport ignored{};
    static_cast<void>(document.load_text(empty_document, ignored));
    gf::NodeGraph& graph = document.editable_graph();
    const gf::UndoLimits limits = graph.undo_limits();
    // Recording off while generating: the generated graph is the start.
    graph.set_undo_limits(gf::UndoLimits{0U, limits.maximum_bytes});
    graph.reserve(options.nodes, options.nodes * options.links_per_node);

    SeededRandom random(options.seed);
    const std::span<const KindDescriptor> table = kind_table();
    double mean_width = 0.0;
    double mean_height = 0.0;
    for (const KindDescriptor& kind : table) {
        mean_width += kind.width;
        mean_height += kind.height;
    }
    mean_width /= static_cast<double>(table.size());
    mean_height /= static_cast<double>(table.size());
    const std::size_t columns = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(options.nodes))));
    report.grid_columns = columns;
    report.cell_width = 1.6 * mean_width;
    report.cell_height = 1.6 * mean_height;

    std::optional<KindIndex> reroute = find_kind("reroute");
    std::vector<gf::NodeId> ids{};
    ids.reserve(options.nodes);
    gf::NodeSpec spec{};
    std::string key{};
    key.reserve(24U);
    for (std::size_t index = 0U; index < options.nodes; ++index) {
        std::size_t kind_number = static_cast<std::size_t>(random.below(table.size() - 1U));
        const bool make_reroute = random.below(100U) == 0U && reroute.has_value();
        if (make_reroute) kind_number = (*reroute).value;
        const std::size_t column = index % columns;
        const std::size_t row = index / columns;
        const double jitter_x = (random.unit() - 0.5) * 0.4 * report.cell_width;
        const double jitter_y = (random.unit() - 0.5) * 0.4 * report.cell_height;
        const gf::WorldPoint at{static_cast<double>(column) * report.cell_width + jitter_x,
                                static_cast<double>(row) * report.cell_height + jitter_y};
        key.assign("g");
        append_number(key, index);
        fill_node_spec(table[kind_number], key, at, spec);
        const gf::GraphResult added = graph.add_node(spec);
        if (added.status != gf::GraphStatus::ok) continue;
        ids.push_back(added.node);
        if (make_reroute) ++report.reroutes;
    }
    report.nodes = ids.size();

    const std::size_t attempts = options.nodes * options.links_per_node;
    const std::size_t count = ids.size();
    for (std::size_t attempt = 0U; attempt < attempts && count > 1U; ++attempt) {
        ++report.link_attempts;
        const std::size_t first = static_cast<std::size_t>(random.below(count));
        std::size_t second = 0U;
        const bool local = random.below(10U) != 0U;
        if (local) {
            // Within three grid cells on each axis.
            const std::int64_t dx = static_cast<std::int64_t>(random.below(7U)) - 3;
            const std::int64_t dy = static_cast<std::int64_t>(random.below(7U)) - 3;
            const std::int64_t column = static_cast<std::int64_t>(first % columns) + dx;
            const std::int64_t row = static_cast<std::int64_t>(first / columns) + dy;
            const std::int64_t target = row * static_cast<std::int64_t>(columns) + column;
            if (column < 0 || column >= static_cast<std::int64_t>(columns) || target < 0 ||
                target >= static_cast<std::int64_t>(count)) {
                ++report.skipped_no_port;
                continue;
            }
            second = static_cast<std::size_t>(target);
        } else {
            second = static_cast<std::size_t>(random.below(count));
        }
        if (first == second) {
            ++report.skipped_no_port;
            continue;
        }
        // Lower index to higher: acyclic by construction.
        const std::size_t low = first < second ? first : second;
        const std::size_t high = first < second ? second : first;
        gf::PortRef output{};
        gf::PortRef input{};
        if (!find_ports(graph, ids[low], ids[high], output, input)) {
            ++report.skipped_no_port;
            continue;
        }
        const gf::GraphResult linked = graph.connect(output, input);
        if (linked.status != gf::GraphStatus::ok) {
            ++report.skipped_no_port;
            continue;
        }
        ++report.links;
        if (local) {
            ++report.local_links;
        } else {
            ++report.long_links;
        }
    }
    graph.set_undo_limits(limits);
    return report;
}

EditReport apply_seeded_edits(GraphDocument& document, std::uint64_t seed, std::size_t count) {
    EditReport report{};
    SeededRandom random(seed);
    const std::span<const KindDescriptor> table = kind_table();
    std::string text{};
    text.reserve(32U);
    for (std::size_t edit = 0U; edit < count; ++edit) {
        const gf::NodeGraph& graph = document.graph();
        const std::uint64_t operation = random.below(7U);
        bool applied = false;
        gf::NodeId node{};
        if (operation == 0U) {
            if (random_live_node(graph, random, node)) {
                const gf::WorldPoint delta{random.unit() * 200.0 - 100.0, random.unit() * 200.0 - 100.0};
                const std::array<gf::NodeId, 1> moved{node};
                applied = document.move_nodes(std::span<const gf::NodeId>(moved.data(), moved.size()), delta).status == gf::GraphStatus::ok;
                if (applied) ++report.moves;
            }
        } else if (operation == 1U) {
            if (random_live_node(graph, random, node)) {
                const std::optional<KindIndex> kind = document.kind_of(node);
                text.assign("v");
                append_number(text, random.below(100000U));
                if (kind.has_value() && !kind_at(*kind).primary_property.empty()) {
                    applied = document.set_property(node, kind_at(*kind).primary_property, text).status ==
                              gf::GraphStatus::ok;
                } else {
                    applied = document.set_property(node, "note", text).status == gf::GraphStatus::ok;
                }
                if (applied) ++report.property_edits;
            }
        } else if (operation == 2U) {
            const KindIndex kind{static_cast<std::uint16_t>(random.below(table.size()))};
            const gf::WorldPoint at{random.unit() * 4000.0, random.unit() * 4000.0};
            applied = document.add_node(kind, at).status == gf::GraphStatus::ok;
            if (applied) ++report.additions;
        } else if (operation == 3U) {
            if (random_live_node(graph, random, node)) {
                const std::array<gf::NodeId, 1> removed{node};
                applied = document.remove_nodes(std::span<const gf::NodeId>(removed.data(), removed.size())) == 1U;
                if (applied) ++report.removals;
            }
        } else if (operation == 4U) {
            gf::NodeId other{};
            if (random_live_node(graph, random, node) && random_live_node(graph, random, other)) {
                gf::PortRef output{};
                gf::PortRef input{};
                if (find_ports(graph, node, other, output, input)) {
                    applied = document.connect(output, input).status == ConnectStatus::connected;
                }
                if (applied) ++report.connects;
            }
        } else if (operation == 5U) {
            gf::LinkId link{};
            if (random_live_link(graph, random, link)) {
                applied = document.disconnect(link).status == gf::GraphStatus::ok;
                if (applied) ++report.disconnects;
            }
        } else {
            if (random_live_node(graph, random, node)) {
                text.assign("Node ");
                append_number(text, random.below(100000U));
                applied = document.set_title(node, text).status == gf::GraphStatus::ok;
                if (applied) ++report.title_edits;
            }
        }
        if (applied) {
            ++report.applied;
        } else {
            ++report.refused;
        }
    }
    return report;
}

} // namespace node_graph
