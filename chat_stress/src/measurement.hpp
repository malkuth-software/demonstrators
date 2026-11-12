#pragma once

// Measurement records (CHAT_STRESS_SPEC section 6): one JSON record per
// workload with its inputs, every counter, per-frame samples, the build and
// the machine. Counts come first; durations are recorded only where a
// person perceives them (startup, frame interval) or where the spec asks
// for a setup time. Nothing here judges a figure.

#include "chat_view.hpp"
#include "process_stats.hpp"

#include <gui_forms/metrics/types/metrics_types.hpp>
#include <gui_forms/window/window.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chat_stress {

// A minimal JSON writer into one reused string: objects, arrays, keys and
// scalars, with commas placed by a depth stack. Strings are escaped (quote,
// backslash and control characters); invalid UTF-8 is written as is.
class JsonWriter final {
public:
    static constexpr std::size_t maximum_depth = 32U;

    JsonWriter();
    void begin_object();
    void end_object();
    void begin_array();
    void end_array();
    void key(std::string_view name);
    void value(std::string_view text);
    void value(const char* text) { value(std::string_view(text)); }
    void value(std::uint64_t number);
    void value(std::int64_t number);
    void value(double number);
    void value(bool flag);
    void null();
    // Writes an already formed JSON value (MetricsSnapshot::to_json).
    void raw(std::string_view json);
    // Writes `number` when present, null otherwise.
    void optional(const std::optional<std::uint64_t>& number);
    [[nodiscard]] const std::string& text() const noexcept { return text_; }

private:
    void separate();
    void write_string(std::string_view text);
    std::string text_{};
    // Per open container: whether it has an element yet.
    std::vector<bool> has_element_{};
    bool after_key_{};
};

// Everything the app counts, read at one moment.
struct CounterSnapshot final {
    gui_forms::VirtualRowsStatistics view{};
    ChatRowsCounters rows{};
    RingCounters ring{};
    WrapCounters wrap{};
    AtlasCounters atlas{};
    CorpusCounters corpus{};
    ChatRootCounters root{};
    gui_forms::DragTrackerStatistics drag{};
    gui_forms::MetricsSnapshot metrics{};
    std::uint64_t rows_total{};
    std::uint64_t semantic_children{};
    std::size_t ring_bytes{};
    std::size_t atlas_bytes{};
    std::size_t corpus_bytes{};
    std::size_t overlay_records{};
    std::optional<std::uint64_t> footprint_bytes{};
    std::optional<std::uint64_t> cpu_microseconds{};
    std::optional<HeapReading> heap{};
};

[[nodiscard]] CounterSnapshot read_counters(gui_forms::Window& window, ChatRoot& root);
void write_counters(JsonWriter& json, const CounterSnapshot& snapshot);
void write_machine(JsonWriter& json, const MachineDescription& machine);
// The build: GUI.Forms version, build type, compiler.
void write_build(JsonWriter& json);

// dev/measurements/<host kind>/<workload>-<seed>.json under `root`.
[[nodiscard]] std::filesystem::path record_path(const std::filesystem::path& root, std::string_view host_kind,
                                                std::string_view workload, std::uint64_t seed);
// Writes `text` to `path`, creating its directories. False on failure.
bool write_record_file(const std::filesystem::path& path, const std::string& text);

} // namespace chat_stress
