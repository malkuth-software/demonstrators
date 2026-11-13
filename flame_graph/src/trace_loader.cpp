#include "trace_loader.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <system_error>
#include <vector>

namespace flame_graph {

namespace {

// Microseconds beyond this are refused (the nanosecond value stays far
// inside the 64-bit range, and a double holds the microseconds to well
// under a nanosecond's error up to 2^53 / 1000).
constexpr double maximum_microseconds = 9.0e12;

std::optional<std::int64_t> to_nanoseconds(double microseconds) noexcept {
    if (!std::isfinite(microseconds) || std::fabs(microseconds) > maximum_microseconds) return std::nullopt;
    const double nanoseconds = microseconds * 1000.0;
    return static_cast<std::int64_t>(std::llround(nanoseconds));
}

std::int64_t fnv_hash_id(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char byte : text) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    // Kept non-negative, so it never equals a negative numeric id.
    return static_cast<std::int64_t>(hash >> 1U);
}

std::string_view trim(std::string_view text) noexcept {
    std::size_t first = 0U;
    while (first < text.size() && (text[first] == ' ' || text[first] == '\t')) ++first;
    std::size_t last = text.size();
    while (last > first && (text[last - 1U] == ' ' || text[last - 1U] == '\t' || text[last - 1U] == '\r')) --last;
    return text.substr(first, last - first);
}

// Consumes the rest of a container whose opening token was just read; a
// scalar token needs nothing. False on a reader error.
bool finish_value(JsonReader& reader, JsonToken token) {
    if (token != JsonToken::begin_object && token != JsonToken::begin_array) return token != JsonToken::error;
    std::size_t open = 1U;
    while (open > 0U) {
        const JsonToken inner = reader.next();
        if (inner == JsonToken::error || inner == JsonToken::end) return false;
        if (inner == JsonToken::begin_object || inner == JsonToken::begin_array) ++open;
        if (inner == JsonToken::end_object || inner == JsonToken::end_array) --open;
    }
    return true;
}

enum class EventField : std::uint8_t { other, phase, name, timestamp, duration, pid, tid, args };

EventField field_of(std::string_view key) noexcept {
    if (key == "ph") return EventField::phase;
    if (key == "name") return EventField::name;
    if (key == "ts") return EventField::timestamp;
    if (key == "dur") return EventField::duration;
    if (key == "pid") return EventField::pid;
    if (key == "tid") return EventField::tid;
    if (key == "args") return EventField::args;
    return EventField::other;
}

// One event's fields, reused between events (the strings keep their
// storage).
struct ChromeEvent final {
    char phase{};
    std::string name{};
    std::optional<double> timestamp{};
    std::optional<double> duration{};
    std::int64_t pid{};
    std::int64_t tid{};
    std::string argument_name{};
    bool has_argument_name{};

    void reset() noexcept {
        phase = '\0';
        name.clear();
        timestamp.reset();
        duration.reset();
        pid = 0;
        tid = 0;
        argument_name.clear();
        has_argument_name = false;
    }
};

struct OpenBegin final {
    std::int64_t start{};
    NameId name{};
};

// The Chrome loader's state for one load.
class ChromeLoad final {
public:
    ChromeLoad(std::string_view text, TraceModel& model, LoadReport& report)
        : reader_(text), model_(model), report_(report) {
        event_.name.reserve(128U);
        event_.argument_name.reserve(64U);
    }

    // True when the whole text was read.
    bool run() {
        const JsonToken top = reader_.next();
        if (top == JsonToken::begin_array) {
            if (!read_events()) return false;
        } else if (top == JsonToken::begin_object) {
            bool found = false;
            while (true) {
                const JsonToken token = reader_.next();
                if (token == JsonToken::end_object) break;
                if (token != JsonToken::key) return refuse("bad top-level object");
                if (reader_.string() == "traceEvents") {
                    const JsonToken events = reader_.next();
                    if (events != JsonToken::begin_array) return refuse("traceEvents is not an array");
                    if (!read_events()) return false;
                    found = true;
                    continue;
                }
                if (!reader_.skip_value()) return refuse("bad top-level member");
            }
            if (!found) return refuse("no traceEvents member");
        } else {
            return refuse("the text is neither an array nor an object");
        }
        if (reader_.next() != JsonToken::end) return refuse("text after the trace");
        close_open_begins();
        return true;
    }

private:
    bool refuse(std::string_view message) {
        report_.message.assign(message);
        if (reader_.failed()) {
            report_.json_error = reader_.error();
            report_.message.append(": ");
            report_.message.append(reader_.error().message);
        }
        return false;
    }

    bool read_events() {
        while (true) {
            const JsonToken token = reader_.next();
            if (token == JsonToken::end_array) return true;
            if (token == JsonToken::begin_object) {
                if (!read_event()) return false;
                apply_event();
                continue;
            }
            ++report_.malformed_events;
            if (!finish_value(reader_, token)) return refuse("bad event");
        }
    }

    // pid or tid: a number, or a string (an integer's text, else hashed).
    bool read_id(std::int64_t& id) {
        const JsonToken token = reader_.next();
        if (token == JsonToken::number) {
            const std::optional<std::int64_t> integer = reader_.integer();
            if (integer.has_value()) {
                id = *integer;
            } else {
                id = static_cast<std::int64_t>(reader_.number().value_or(0.0));
            }
            return true;
        }
        if (token == JsonToken::string) {
            const std::string_view text = reader_.string();
            std::int64_t value = 0;
            const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            const bool numeric = parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && !text.empty();
            id = numeric ? value : fnv_hash_id(text);
            return true;
        }
        return finish_value(reader_, token);
    }

    bool read_number_field(std::optional<double>& field) {
        const JsonToken token = reader_.next();
        if (token == JsonToken::number) {
            field = reader_.number();
            return true;
        }
        return finish_value(reader_, token);
    }

    bool read_arguments() {
        const JsonToken token = reader_.next();
        if (token != JsonToken::begin_object) return finish_value(reader_, token);
        while (true) {
            const JsonToken inner = reader_.next();
            if (inner == JsonToken::end_object) return true;
            if (inner != JsonToken::key) return false;
            if (reader_.string() == "name") {
                const JsonToken value = reader_.next();
                if (value == JsonToken::string) {
                    event_.argument_name.assign(reader_.string());
                    event_.has_argument_name = true;
                } else if (!finish_value(reader_, value)) {
                    return false;
                }
                continue;
            }
            if (!reader_.skip_value()) return false;
        }
    }

    bool read_event() {
        event_.reset();
        ++report_.events;
        while (true) {
            const JsonToken token = reader_.next();
            if (token == JsonToken::end_object) return true;
            if (token != JsonToken::key) return refuse("bad event member");
            // Classified before the value replaces the key's bytes.
            const EventField field = field_of(reader_.string());
            bool read = true;
            if (field == EventField::phase || field == EventField::name) {
                const JsonToken value = reader_.next();
                if (value == JsonToken::string) {
                    if (field == EventField::phase) {
                        const std::string_view phase = reader_.string();
                        event_.phase = phase.empty() ? '\0' : phase[0];
                    } else {
                        event_.name.assign(reader_.string());
                    }
                } else {
                    read = finish_value(reader_, value);
                }
            } else if (field == EventField::timestamp) {
                read = read_number_field(event_.timestamp);
            } else if (field == EventField::duration) {
                read = read_number_field(event_.duration);
            } else if (field == EventField::pid) {
                read = read_id(event_.pid);
            } else if (field == EventField::tid) {
                read = read_id(event_.tid);
            } else if (field == EventField::args) {
                read = read_arguments();
            } else {
                read = reader_.skip_value();
            }
            if (!read) return refuse("bad event value");
        }
    }

    std::optional<ThreadIndex> thread_of_event() {
        const std::optional<ThreadIndex> thread = model_.thread(event_.pid, event_.tid);
        if (!thread.has_value()) {
            ++report_.threads_refused;
            return std::nullopt;
        }
        const std::size_t index = (*thread).value;
        if (index >= open_.size()) {
            open_.resize(index + 1U);
            latest_.resize(index + 1U, 0);
        }
        return thread;
    }

    std::optional<NameId> intern(std::string_view text) {
        const std::optional<NameId> name = model_.names().intern(text);
        if (!name.has_value()) ++report_.names_refused;
        return name;
    }

    void note_time(ThreadIndex thread, std::int64_t time) {
        std::int64_t& latest = latest_[thread.value];
        latest = std::max(latest, time);
    }

    void add_span(ThreadIndex thread, std::int64_t start, std::int64_t duration, NameId name) {
        if (!model_.add_span(thread, start, duration, name)) ++report_.spans_refused;
    }

    void apply_event() {
        const char phase = event_.phase;
        if (phase == 'M') {
            ++report_.metadata_events;
            if (event_.name != "thread_name" || !event_.has_argument_name) return;
            const std::optional<ThreadIndex> thread = thread_of_event();
            const std::optional<NameId> name = intern(event_.argument_name);
            if (thread.has_value() && name.has_value()) model_.set_thread_name(*thread, *name);
            return;
        }
        if (phase != 'X' && phase != 'B' && phase != 'E') {
            ++report_.other_events;
            return;
        }
        const std::optional<std::int64_t> start =
            event_.timestamp.has_value() ? to_nanoseconds(*event_.timestamp) : std::nullopt;
        if (!start.has_value()) {
            ++report_.malformed_events;
            return;
        }
        const std::optional<ThreadIndex> thread = thread_of_event();
        if (!thread.has_value()) return;
        note_time(*thread, *start);
        if (phase == 'E') {
            ++report_.end_events;
            std::vector<OpenBegin>& stack = open_[(*thread).value];
            if (stack.empty()) {
                ++report_.unmatched_ends;
                return;
            }
            const OpenBegin begin = stack.back();
            stack.pop_back();
            add_span(*thread, begin.start, std::max<std::int64_t>(0, *start - begin.start), begin.name);
            return;
        }
        const std::optional<NameId> name = intern(event_.name);
        if (!name.has_value()) return;
        if (phase == 'B') {
            ++report_.begin_events;
            std::vector<OpenBegin>& stack = open_[(*thread).value];
            if (stack.size() >= TraceModel::maximum_depth) {
                ++report_.spans_refused;
                return;
            }
            stack.push_back(OpenBegin{*start, *name});
            return;
        }
        ++report_.complete_events;
        std::int64_t duration = 0;
        if (event_.duration.has_value()) {
            const std::optional<std::int64_t> converted = to_nanoseconds(*event_.duration);
            if (!converted.has_value() || *converted < 0) {
                ++report_.malformed_events;
                return;
            }
            duration = *converted;
        }
        note_time(*thread, *start + duration);
        add_span(*thread, *start, duration, *name);
    }

    void close_open_begins() {
        for (std::size_t index = 0U; index < open_.size(); ++index) {
            std::vector<OpenBegin>& stack = open_[index];
            const ThreadIndex thread{static_cast<std::uint32_t>(index)};
            while (!stack.empty()) {
                const OpenBegin begin = stack.back();
                stack.pop_back();
                ++report_.unclosed_begins;
                add_span(thread, begin.start, std::max<std::int64_t>(0, latest_[index] - begin.start), begin.name);
            }
        }
    }

    JsonReader reader_;
    TraceModel& model_;
    LoadReport& report_;
    ChromeEvent event_{};
    // Per thread (ThreadIndex): open B events and the latest time seen.
    std::vector<std::vector<OpenBegin>> open_{};
    std::vector<std::int64_t> latest_{};
};

struct OpenFrame final {
    NameId name{};
    std::int64_t start{};
};

} // namespace

TraceFormat detect_format(std::string_view text) noexcept {
    for (const char byte : text) {
        if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r') continue;
        if (byte == '[' || byte == '{') return TraceFormat::chrome_json;
        return TraceFormat::folded;
    }
    return TraceFormat::folded;
}

std::string_view format_name(TraceFormat format) noexcept {
    switch (format) {
    case TraceFormat::chrome_json:
        return "chrome-json";
    case TraceFormat::folded:
        return "folded";
    case TraceFormat::automatic:
    default:
        return "automatic";
    }
}

LoadReport load_chrome_trace(std::string_view text, TraceModel& model) {
    LoadReport report{};
    report.format = TraceFormat::chrome_json;
    report.bytes = text.size();
    model.clear();
    model.set_unit(TraceUnit::nanoseconds);
    // About one event per 100 bytes of a typical trace.
    model.names().reserve(4096U, 64U * 1024U);
    bool read = false;
    {
        ChromeLoad load(text, model, report);
        read = load.run();
    }
    if (!read) {
        model.clear();
        return report;
    }
    model.finish();
    report.loaded = true;
    return report;
}

LoadReport load_folded_stacks(std::string_view text, TraceModel& model) {
    LoadReport report{};
    report.format = TraceFormat::folded;
    report.bytes = text.size();
    model.clear();
    model.set_unit(TraceUnit::samples);
    const std::optional<ThreadIndex> lane = model.thread(0, 0);
    const std::optional<NameId> lane_name = model.names().intern("samples");
    if (!lane.has_value() || !lane_name.has_value()) {
        report.message = "the model refused the samples lane";
        return report;
    }
    model.set_thread_name(*lane, *lane_name);
    std::vector<OpenFrame> open{};
    std::vector<NameId> frames{};
    open.reserve(TraceModel::maximum_depth);
    frames.reserve(TraceModel::maximum_depth);
    std::int64_t at = 0;
    std::size_t position = 0U;
    while (position < text.size()) {
        std::size_t end = text.find('\n', position);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = trim(text.substr(position, end - position));
        position = end + 1U;
        ++report.lines;
        if (line.empty() || line[0] == '#') continue;
        const std::size_t space = line.rfind(' ');
        if (space == std::string_view::npos) {
            ++report.malformed_lines;
            continue;
        }
        const std::string_view count_text = line.substr(space + 1U);
        std::uint64_t count = 0U;
        const std::from_chars_result parsed =
            std::from_chars(count_text.data(), count_text.data() + count_text.size(), count);
        const std::string_view stack = trim(line.substr(0U, space));
        if (parsed.ec != std::errc{} || parsed.ptr != count_text.data() + count_text.size() || stack.empty()) {
            ++report.malformed_lines;
            continue;
        }
        if (count == 0U) continue;
        if (count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() - at)) {
            ++report.malformed_lines;
            break;
        }
        // The line's frames, root first, cut at the model's depth.
        frames.clear();
        std::size_t frame_start = 0U;
        bool refused = false;
        while (frame_start <= stack.size() && frames.size() < TraceModel::maximum_depth) {
            std::size_t frame_end = stack.find(';', frame_start);
            if (frame_end == std::string_view::npos) frame_end = stack.size();
            std::string_view frame = stack.substr(frame_start, frame_end - frame_start);
            if (frame.empty()) frame = "[unknown]";
            const std::optional<NameId> name = model.names().intern(frame);
            if (!name.has_value()) {
                ++report.names_refused;
                refused = true;
                break;
            }
            frames.push_back(*name);
            frame_start = frame_end + 1U;
        }
        if (refused) continue;
        // Frames shared with the open stack carry on; the rest close here.
        std::size_t shared = 0U;
        while (shared < open.size() && shared < frames.size() && open[shared].name == frames[shared]) ++shared;
        while (open.size() > shared) {
            const OpenFrame frame = open.back();
            open.pop_back();
            if (!model.add_span(*lane, frame.start, at - frame.start, frame.name)) ++report.spans_refused;
        }
        for (std::size_t index = shared; index < frames.size(); ++index) open.push_back(OpenFrame{frames[index], at});
        at += static_cast<std::int64_t>(count);
        report.samples += static_cast<std::size_t>(count);
    }
    while (!open.empty()) {
        const OpenFrame frame = open.back();
        open.pop_back();
        if (!model.add_span(*lane, frame.start, at - frame.start, frame.name)) ++report.spans_refused;
    }
    model.finish();
    report.loaded = true;
    return report;
}

LoadReport load_trace_text(std::string_view text, TraceFormat format, TraceModel& model) {
    const TraceFormat chosen = format == TraceFormat::automatic ? detect_format(text) : format;
    if (chosen == TraceFormat::chrome_json) return load_chrome_trace(text, model);
    return load_folded_stacks(text, model);
}

LoadReport load_trace_file(const std::filesystem::path& path, TraceFormat format, TraceModel& model,
                           std::string& buffer) {
    LoadReport report{};
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        model.clear();
        report.message = "the file could not be opened";
        return report;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > maximum_trace_bytes) {
        model.clear();
        report.message = "the file is larger than the loader's limit";
        return report;
    }
    file.seekg(0, std::ios::beg);
    buffer.resize(static_cast<std::size_t>(size));
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    if (!file) {
        model.clear();
        report.message = "the file could not be read";
        return report;
    }
    report = load_trace_text(buffer, format, model);
    return report;
}

} // namespace flame_graph
