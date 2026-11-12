// The document and the stress generator without a window: determinism,
// save/load/save stability, 1,000 seeded edits undone back to the same
// bytes, tolerant load (normalised by the next save), refused input with
// its position and the graph unchanged, and unknown kinds and properties
// kept.

#include "../src/generator.hpp"
#include "../src/graph_document.hpp"
#include "../src/kinds.hpp"
#include "test_support.hpp"

#include <cstdio>
#include <exception>
#include <string>
#include <string_view>

namespace gf = gui_forms;
using node_graph_tests::require;

namespace {

std::string saved(node_graph::GraphDocument& document) {
    std::string text{};
    document.save_text(text);
    return text;
}

node_graph::GeneratorReport generate(node_graph::GraphDocument& document, std::size_t nodes, std::uint64_t seed) {
    node_graph::GeneratorOptions options{};
    options.nodes = nodes;
    options.links_per_node = 2U;
    options.seed = seed;
    return node_graph::generate_stress_graph(document, options);
}

void generator_is_deterministic() {
    node_graph::GraphDocument first{};
    node_graph::GraphDocument second{};
    const node_graph::GeneratorReport report = generate(first, 2000U, 7U);
    static_cast<void>(generate(second, 2000U, 7U));
    require(report.nodes == 2000U, "the generator made every node");
    require(report.links > 0U, "the generator made links");
    require(report.local_links >= report.long_links, "most links are local");
    require(saved(first) == saved(second), "the same seed generates the same document");
    node_graph::GraphDocument third{};
    static_cast<void>(generate(third, 2000U, 8U));
    require(saved(first) != saved(third), "another seed generates another document");
    require(first.graph().undo_steps() == 0U, "the generated graph is the start: nothing to undo");
}

void save_load_save_is_stable() {
    node_graph::GraphDocument document{};
    static_cast<void>(generate(document, 500U, 3U));
    const std::string text = saved(document);
    node_graph::GraphDocument loaded{};
    node_graph::LoadReport report{};
    const gf::GraphLoadResult result = loaded.load_text(text, report);
    require(result.error == gf::GraphLoadError::ok, "a saved document loads");
    require(report.unknown_kinds == 0U && report.unknown_properties == 0U, "the generator uses only known kinds");
    require(saved(loaded) == text, "load then save gives the same bytes");
}

void seeded_edits_undo_to_the_same_bytes() {
    node_graph::GraphDocument document{};
    static_cast<void>(generate(document, 300U, 11U));
    const std::string before = saved(document);
    const node_graph::EditReport edits = node_graph::apply_seeded_edits(document, 99U, 1000U);
    require(edits.applied + edits.refused == 1000U, "every seeded edit was tried");
    require(edits.applied > 0U, "seeded edits were applied");
    require(saved(document) != before, "the edits changed the document");
    std::size_t undone = 0U;
    while (document.graph().undo_steps() > 0U && undone < 2000U) {
        require(document.undo().status == gf::GraphStatus::ok, "undo succeeds while steps remain");
        ++undone;
    }
    require(saved(document) == before, "1000 seeded edits, all undone, save the same bytes");
    // And redo brings them back.
    while (document.graph().redo_steps() > 0U) static_cast<void>(document.redo());
    node_graph::GraphDocument replay{};
    static_cast<void>(generate(replay, 300U, 11U));
    static_cast<void>(node_graph::apply_seeded_edits(replay, 99U, 1000U));
    require(saved(document) == saved(replay), "redo all reaches the edited document");
}

void tolerant_load_is_normalised() {
    const std::string_view loose =
        "gui-forms-graph 1;\r\n"
        "# a comment line\r\n"
        "\r\n"
        "node c1 {   # after a statement\r\n"
        "\tkind: \"constant\";\r\n"
        "    title:   \"Constant\";\r\n"
        "    bounds: 0.0 6e1 150 72;\r\n"
        "    port: output right 0.5 0 \"value\";\r\n"
        "    value: \"2\";\r\n"
        "}\r\n"
        "end;";
    node_graph::GraphDocument document{};
    node_graph::LoadReport report{};
    const gf::GraphLoadResult result = document.load_text(loose, report);
    require(result.error == gf::GraphLoadError::ok, "comments, CRLF, tabs and other number spellings load");
    const std::string normal = saved(document);
    require(normal.find('\r') == std::string::npos && normal.find('#') == std::string::npos,
            "the next save is canonical");
    require(normal.find("bounds: 0 60 150 72;") != std::string::npos, "numbers are written shortest");
    node_graph::GraphDocument again{};
    require(again.load_text(normal, report).error == gf::GraphLoadError::ok && saved(again) == normal,
            "the normalised document is stable");
}

void hostile_input_is_refused() {
    node_graph::GraphDocument document{};
    static_cast<void>(generate(document, 50U, 5U));
    const std::string before = saved(document);
    node_graph::LoadReport report{};
    const std::string_view bad =
        "gui-forms-graph 1;\n"
        "node a {\n"
        "    kind: \"constant\";\n"
        "    title: \"A\";\n"
        "    bounds: 0 0 -5 72;\n"
        "}\n"
        "end;\n";
    const gf::GraphLoadResult refused = document.load_text(bad, report);
    require(refused.error == gf::GraphLoadError::bad_number, "a negative size is refused");
    require(refused.line == 5U && refused.column > 1U, "the refusal names its line and column");
    require(saved(document) == before, "a refused load leaves the graph unchanged");
    const std::string_view truncated = "gui-forms-graph 1;\nnode a {\n";
    require(document.load_text(truncated, report).error == gf::GraphLoadError::truncated, "a cut-off document");
    require(document.load_text("not a graph", report).error == gf::GraphLoadError::bad_header, "a wrong header");
    require(saved(document) == before, "refused loads never change the graph");
}

void unknown_kinds_and_properties_are_kept() {
    const std::string_view text =
        "gui-forms-graph 1;\n"
        "node x {\n"
        "    kind: \"from_the_future\";\n"
        "    title: \"Future\";\n"
        "    bounds: 0 0 100 60;\n"
        "    port: output right 0.5 0 \"value\";\n"
        "    mood: \"bright\";\n"
        "}\n"
        "node c {\n"
        "    kind: \"constant\";\n"
        "    title: \"Constant\";\n"
        "    bounds: 200 0 150 72;\n"
        "    port: output right 0.5 0 \"value\";\n"
        "    value: \"1\";\n"
        "    colour_hint: \"teal\";\n"
        "}\n"
        "end;\n";
    node_graph::GraphDocument document{};
    node_graph::LoadReport report{};
    require(document.load_text(text, report).error == gf::GraphLoadError::ok, "a document with unknowns loads");
    require(report.unknown_kinds == 1U, "the unknown kind is reported");
    require(report.unknown_properties >= 1U, "the undeclared property is reported");
    require(!report.unknown_kind_keys.empty() && report.unknown_kind_keys.front() == "x", "its key is named");
    require(saved(document) == text, "unknown kinds and properties are written back as they came");
}

} // namespace

int main() {
    try {
        generator_is_deterministic();
        save_load_save_is_stable();
        seeded_edits_undo_to_the_same_bytes();
        tolerant_load_is_normalised();
        hostile_input_is_refused();
        unknown_kinds_and_properties_are_kept();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "document_tests: %s\n", failure.what());
        return 1;
    }
    std::printf("document_tests: ok\n");
    return 0;
}
