#pragma once

// ProfilerRoot: the viewer's window content. A toolbar (search box, the
// flame/icicle switch, zoom reset), then the flame graph, the timeline and
// the details table stacked, and a status line.
//
// It owns the loaded trace (TraceModel), the per-name colours, the search
// flags and the selection, and lends them to the three views through
// ProfileInputs. Declared first, so they outlive the views' use of them;
// the destructor clears the views' inputs anyway, since the views are
// shared children that may outlive this control.
//
// Selection sync: a pick in any view becomes the root's selection (node and
// name), which the root pushes to the others through calls that raise
// nothing. A table row's selection sets the name only; activating a row
// (double click, Enter) searches for the function's exact name.
//
// The search runs on every text change of the search box (the model's
// search is one pass over the names and one over the nodes).
//
// One UI thread; loading runs on it too.

#include "details_table.hpp"
#include "flame_view.hpp"
#include "profile_inputs.hpp"
#include "timeline_view.hpp"
#include "trace_generator.hpp"
#include "trace_loader.hpp"
#include "trace_model.hpp"

#include <gui_forms/control.hpp>
#include <gui_forms/controls/button.hpp>
#include <gui_forms/controls/label.hpp>
#include <gui_forms/controls/text_box.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace flame_graph {

struct ProfilerCounters final {
    std::uint64_t loads{};
    std::uint64_t load_failures{};
    std::uint64_t searches{};
    std::uint64_t flame_picks{};
    std::uint64_t timeline_picks{};
    std::uint64_t table_picks{};
    std::uint64_t table_activations{};
    std::uint64_t orientation_switches{};
    std::uint64_t zoom_resets{};
    std::uint64_t status_updates{};
};

class ProfilerRoot final : public gui_forms::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    static constexpr double toolbar_height = 32.0;
    static constexpr double status_height = 22.0;
    static constexpr double search_width = 260.0;
    static constexpr double button_width = 96.0;
    // Shares of the space between the toolbar and the status line.
    static constexpr double flame_share = 0.45;
    static constexpr double timeline_share = 0.30;

    explicit ProfilerRoot(gui_forms::StableId stable_id);
    ~ProfilerRoot() override;
    ProfilerRoot(const ProfilerRoot&) = delete;
    ProfilerRoot& operator=(const ProfilerRoot&) = delete;
    void initialize_control_tree();

    // Loading replaces the trace (a refusal leaves an empty one) and resets
    // the views, the search's results (the query is kept) and the selection.
    LoadReport load_text(std::string_view text, TraceFormat format);
    LoadReport load_file(const std::filesystem::path& path, TraceFormat format);
    void generate(const GeneratorOptions& options);

    void set_search(std::string_view query);
    void select(const ProfileSelection& selection);
    void set_orientation(FlameOrientation orientation);
    void toggle_orientation();
    void reset_zoom();

    [[nodiscard]] const TraceModel& model() const& noexcept { return model_; }
    const TraceModel& model() const&& = delete;
    [[nodiscard]] const SearchFlags& search() const& noexcept { return search_; }
    const SearchFlags& search() const&& = delete;
    [[nodiscard]] const ProfileSelection& selection() const& noexcept { return selection_; }
    const ProfileSelection& selection() const&& = delete;
    [[nodiscard]] const LoadReport& last_report() const& noexcept { return last_report_; }
    const LoadReport& last_report() const&& = delete;
    [[nodiscard]] FlameView& flame() const noexcept { return *flame_; }
    [[nodiscard]] TimelineView& timeline() const noexcept { return *timeline_; }
    [[nodiscard]] DetailsTable& table() const noexcept { return *table_; }
    [[nodiscard]] gui_forms::TextBox& search_box() const noexcept { return *search_box_; }
    [[nodiscard]] gui_forms::Label& status() const noexcept { return *status_; }
    [[nodiscard]] const ProfilerCounters& counters() const& noexcept { return counters_; }
    const ProfilerCounters& counters() const&& = delete;

    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;

private:
    void model_loaded();
    void push_inputs();
    void refresh_status();
    void on_search_text(const std::string& text);
    void on_orientation_clicked(gui_forms::ButtonBase& button);
    void on_reset_clicked(gui_forms::ButtonBase& button);
    void on_flame_pick(const NodePick& pick);
    void on_timeline_pick(const NodePick& pick);
    void on_table_pick(const FunctionPick& pick);
    void on_table_activated(const FunctionPick& pick);

    // The data the views borrow: declared before the views.
    TraceModel model_{};
    SearchFlags search_{};
    ProfileSelection selection_{};
    std::vector<gui_forms::Color> colors_{};
    LoadReport last_report_{};
    std::string load_buffer_{};

    std::shared_ptr<gui_forms::TextBox> search_box_{};
    std::shared_ptr<gui_forms::Button> orientation_button_{};
    std::shared_ptr<gui_forms::Button> reset_button_{};
    std::shared_ptr<FlameView> flame_{};
    std::shared_ptr<TimelineView> timeline_{};
    std::shared_ptr<DetailsTable> table_{};
    std::shared_ptr<gui_forms::Label> status_{};
    std::string status_text_{};
    ProfilerCounters counters_{};
};

} // namespace flame_graph
