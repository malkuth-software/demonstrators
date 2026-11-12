#include "measurement.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <system_error>

#ifndef CHAT_STRESS_GUI_FORMS_VERSION
#define CHAT_STRESS_GUI_FORMS_VERSION "unknown"
#endif
#ifndef CHAT_STRESS_BUILD_TYPE
#define CHAT_STRESS_BUILD_TYPE "unknown"
#endif

namespace chat_stress {

namespace gf = gui_forms;

namespace {

void field(JsonWriter& json, std::string_view name, std::uint64_t number) {
    json.key(name);
    json.value(number);
}

} // namespace

// -- JsonWriter ------------------------------------------------------------------------

JsonWriter::JsonWriter() {
    text_.reserve(64U * 1024U);
    has_element_.reserve(maximum_depth);
}

void JsonWriter::separate() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (has_element_.empty()) return;
    if (has_element_.back()) text_.push_back(',');
    has_element_.back() = true;
}

void JsonWriter::begin_object() {
    separate();
    text_.push_back('{');
    has_element_.push_back(false);
}

void JsonWriter::end_object() {
    text_.push_back('}');
    if (!has_element_.empty()) has_element_.pop_back();
}

void JsonWriter::begin_array() {
    separate();
    text_.push_back('[');
    has_element_.push_back(false);
}

void JsonWriter::end_array() {
    text_.push_back(']');
    if (!has_element_.empty()) has_element_.pop_back();
}

void JsonWriter::key(std::string_view name) {
    separate();
    write_string(name);
    text_.push_back(':');
    after_key_ = true;
}

void JsonWriter::value(std::string_view text) {
    separate();
    write_string(text);
}

void JsonWriter::write_string(std::string_view text) {
    text_.push_back('"');
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            text_.push_back('\\');
            text_.push_back(character);
        } else if (byte < 0x20U) {
            char escaped[8]{};
            const int written = std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned int>(byte));
            if (written > 0) text_.append(escaped, static_cast<std::size_t>(written));
        } else {
            text_.push_back(character);
        }
    }
    text_.push_back('"');
}

void JsonWriter::value(std::uint64_t number) {
    separate();
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(number));
    if (written > 0) text_.append(buffer, static_cast<std::size_t>(written));
}

void JsonWriter::value(std::int64_t number) {
    separate();
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(number));
    if (written > 0) text_.append(buffer, static_cast<std::size_t>(written));
}

void JsonWriter::value(double number) {
    separate();
    if (!std::isfinite(number)) {
        text_.append("null");
        return;
    }
    char buffer[40]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%.17g", number);
    if (written > 0) text_.append(buffer, static_cast<std::size_t>(written));
}

void JsonWriter::value(bool flag) {
    separate();
    if (flag) {
        text_.append("true");
    } else {
        text_.append("false");
    }
}

void JsonWriter::null() {
    separate();
    text_.append("null");
}

void JsonWriter::raw(std::string_view json) {
    separate();
    text_.append(json);
}

void JsonWriter::optional(const std::optional<std::uint64_t>& number) {
    if (number.has_value()) {
        value(*number);
    } else {
        null();
    }
}

// -- counters ----------------------------------------------------------------------------

CounterSnapshot read_counters(gf::Window& window, ChatRoot& root) {
    CounterSnapshot snapshot{};
    gf::VirtualRowsView& view = root.view();
    snapshot.view = view.statistics();
    snapshot.rows = root.rows().counters();
    snapshot.ring = root.rows().ring_counters();
    snapshot.text = root.rows().text_counters();
    snapshot.avatars = root.atlas().counters();
    snapshot.atlas = root.atlas().statistics();
    snapshot.corpus = root.corpus().counters();
    snapshot.root = root.counters();
    snapshot.metrics = window.metrics_snapshot();
    snapshot.rows_total = root.corpus().row_count();
    snapshot.semantic_children = view.realized_item_count();
    snapshot.ring_bytes = root.rows().storage_bytes();
    snapshot.atlas_bytes = root.atlas().storage_bytes();
    snapshot.corpus_bytes = root.corpus().resident_bytes();
    snapshot.overlay_records = root.corpus().overlay_size();
    snapshot.footprint_bytes = process_footprint_bytes();
    snapshot.cpu_microseconds = process_cpu_microseconds();
    snapshot.allocations = gf::allocation_statistics();
    snapshot.selected_rows = view.selected_row_count();
    return snapshot;
}

void write_counters(JsonWriter& json, const CounterSnapshot& snapshot) {
    json.begin_object();
    field(json, "rows", snapshot.rows_total);
    json.key("view");
    json.begin_object();
    field(json, "rows_measured", snapshot.view.rows_measured);
    field(json, "rows_painted", snapshot.view.rows_painted);
    field(json, "rows_described", snapshot.view.rows_described);
    field(json, "anchor_corrections", snapshot.view.anchor_corrections);
    field(json, "scroll_copies", snapshot.view.scroll_copies);
    field(json, "scroll_repaints", snapshot.view.scroll_repaints);
    field(json, "last_settle_measured", snapshot.view.last_settle_measured);
    field(json, "last_settle_passes", snapshot.view.last_settle_passes);
    field(json, "last_paint_rows", snapshot.view.last_paint_rows);
    field(json, "index_runs", snapshot.view.index_runs);
    field(json, "index_nodes", snapshot.view.index_nodes);
    field(json, "index_depth", snapshot.view.index_depth);
    field(json, "index_bytes", snapshot.view.index_bytes);
    field(json, "index_measured", snapshot.view.index_measured);
    field(json, "rows_forgotten", snapshot.view.rows_forgotten);
    field(json, "budget_compactions", snapshot.view.budget_compactions);
    field(json, "partial_repaints", snapshot.view.partial_repaints);
    field(json, "selected_rows", snapshot.selected_rows);
    field(json, "semantic_children", snapshot.semantic_children);
    json.end_object();
    json.key("painter");
    json.begin_object();
    field(json, "rows_measured", snapshot.rows.rows_measured);
    field(json, "rows_painted", snapshot.rows.rows_painted);
    field(json, "rows_described", snapshot.rows.rows_described);
    field(json, "lines_drawn", snapshot.rows.lines_drawn);
    field(json, "font_generations", snapshot.rows.font_generations);
    field(json, "stale_rows_refused", snapshot.rows.stale_rows_refused);
    field(json, "layout_ring_hits", snapshot.ring.hits);
    field(json, "layout_ring_misses", snapshot.ring.misses);
    field(json, "layout_ring_replacements", snapshot.ring.replacements);
    field(json, "layout_ring_bytes", snapshot.ring_bytes);
    field(json, "paragraphs_laid_out", snapshot.text.paragraphs);
    field(json, "lines_laid_out", snapshot.text.lines);
    field(json, "text_shaping_calls", snapshot.text.shaping_calls);
    field(json, "text_shaped_bytes", snapshot.text.shaped_bytes);
    field(json, "overflowing_lines", snapshot.text.overflowing_lines);
    field(json, "paragraph_storage_retries", snapshot.text.storage_retries);
    field(json, "paragraph_refusals", snapshot.text.refusals);
    json.end_object();
    json.key("atlas");
    json.begin_object();
    field(json, "hits", snapshot.atlas.hits);
    field(json, "assignments", snapshot.atlas.assignments);
    field(json, "replacements", snapshot.atlas.replacements);
    field(json, "fills", snapshot.atlas.fills);
    field(json, "fill_refusals", snapshot.atlas.fill_refusals);
    field(json, "deferred_fills", snapshot.atlas.deferred_fills);
    field(json, "deferred_drops", snapshot.atlas.deferred_drops);
    field(json, "draws", snapshot.atlas.draws);
    field(json, "draw_refusals", snapshot.atlas.draw_refusals);
    field(json, "users_damaged", snapshot.atlas.users_damaged);
    field(json, "slots_in_use", static_cast<std::uint64_t>(snapshot.atlas.slots_in_use));
    field(json, "patterns_drawn", snapshot.avatars.patterns_drawn);
    field(json, "paint_fallbacks", snapshot.avatars.paint_fallbacks);
    field(json, "atlas_failures", snapshot.avatars.atlas_failures);
    field(json, "bytes", snapshot.atlas_bytes);
    json.end_object();
    json.key("corpus");
    json.begin_object();
    field(json, "timestamp_hash_calls", snapshot.corpus.timestamp_hash_calls);
    field(json, "timestamp_block_totals", snapshot.corpus.timestamp_block_totals);
    field(json, "author_walk_steps", snapshot.corpus.author_walk_steps);
    field(json, "texts_generated", snapshot.corpus.texts_generated);
    field(json, "text_bytes_generated", snapshot.corpus.text_bytes_generated);
    field(json, "overlay_lookups", snapshot.corpus.overlay_lookups);
    field(json, "overlay_records", snapshot.overlay_records);
    field(json, "resident_bytes", snapshot.corpus_bytes);
    json.end_object();
    json.key("operations");
    json.begin_object();
    field(json, "appended_generated", snapshot.root.appended_generated);
    field(json, "appended_local", snapshot.root.appended_local);
    field(json, "edits", snapshot.root.edits);
    field(json, "deletions", snapshot.root.deletions);
    field(json, "erased_rows", snapshot.root.erased_rows);
    field(json, "erase_operations", snapshot.root.erase_operations);
    field(json, "jumps", snapshot.root.jumps);
    field(json, "scroll_reports", snapshot.root.scroll_reports);
    field(json, "badge_updates", snapshot.root.badge_updates);
    field(json, "status_updates", snapshot.root.status_updates);
    json.end_object();
    json.key("process");
    json.begin_object();
    json.key("footprint_bytes");
    json.optional(snapshot.footprint_bytes);
    json.key("cpu_microseconds");
    json.optional(snapshot.cpu_microseconds);
    json.end_object();
    // GUI.Forms' own counted allocation paths (image resources, the atlas,
    // effects, display chunks, damage lists, text layout), process-wide
    // since start; its "scope" member says so. Not the process's allocations.
    json.key("gui_forms_allocations");
    json.raw(snapshot.allocations.to_json());
    json.key("window_metrics");
    json.raw(snapshot.metrics.to_json());
    json.end_object();
}

void write_machine(JsonWriter& json, const MachineDescription& machine) {
    json.begin_object();
    json.key("host_kind");
    json.value(machine.host_kind);
    json.key("model");
    json.value(machine.model);
    json.key("processor");
    json.value(machine.processor);
    field(json, "memory_bytes", machine.memory_bytes);
    field(json, "logical_processors", machine.logical_processors);
    json.key("os_name");
    json.value(machine.os_name);
    json.key("os_version");
    json.value(machine.os_version);
    json.key("os_build");
    json.value(machine.os_build);
    json.end_object();
}

void write_build(JsonWriter& json) {
    json.begin_object();
    json.key("gui_forms_version");
    json.value(CHAT_STRESS_GUI_FORMS_VERSION);
    json.key("build_type");
    json.value(CHAT_STRESS_BUILD_TYPE);
    json.key("compiler");
#if defined(__clang__)
    json.value(__clang_version__);
#elif defined(_MSC_VER)
    json.value("msvc");
#else
    json.value("other");
#endif
#if defined(NDEBUG)
    json.key("assertions");
    json.value(false);
#else
    json.key("assertions");
    json.value(true);
#endif
    json.end_object();
}

std::filesystem::path record_path(const std::filesystem::path& root, std::string_view host_kind,
                                  std::string_view workload, std::uint64_t seed) {
    char seed_text[24]{};
    const int written = std::snprintf(seed_text, sizeof(seed_text), "%016llx", static_cast<unsigned long long>(seed));
    std::string name(workload);
    name.push_back('-');
    if (written > 0) name.append(seed_text, static_cast<std::size_t>(written));
    name.append(".json");
    const std::filesystem::path path = root / "dev" / "measurements" / std::string(host_kind) / name;
    return path;
}

bool write_record_file(const std::filesystem::path& path, const std::string& text) {
    std::error_code error{};
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.put('\n');
    const bool written = static_cast<bool>(file);
    return written;
}

} // namespace chat_stress
