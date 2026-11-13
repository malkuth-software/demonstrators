#pragma once

// DetailsTable: one row per function (TraceModel::functions) with its
// total, self time, calls and share of the whole, sortable by any column,
// filtered by the search. VirtualRowsView draws the rows; it has no columns
// or header (GAPS.md F4), so this control paints the header strip itself,
// hit-tests its column edges, and lays the same column edges out in each
// row's paint callback.
//
// Rows: row_functions_ holds the shown functions' indices in display order
// (filtered, then sorted with a named comparator); function_rows_ is its
// inverse. Both are rebuilt on load, search and sort, reserved once per
// load.
//
// Events: function_picked when the user selects a row (not when the root
// calls select_function: that sets the row under a guard and raises
// nothing), function_activated on double click or Enter.
//
// The view's row targets name this control; the destructor clears them.
// One UI thread.

#include "profile_inputs.hpp"

#include <gui_forms/control.hpp>
#include <gui_forms/controls/virtual_rows_view.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/events/mouse_events/mouse_events.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flame_graph {

enum class TableColumn : std::uint8_t { name, total, self, calls, share };

inline constexpr std::size_t table_column_count = 5U;

struct DetailsTableStatistics final {
    std::uint64_t rebuilds{};
    std::uint64_t sorts{};
    std::uint64_t header_clicks{};
    std::uint64_t rows_painted{};
    std::uint64_t picks{};
    std::uint64_t activations{};
};

struct FunctionPick final {
    std::size_t function{};
    NameId name{};
};

class DetailsTable final : public gui_forms::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    static constexpr double header_height = 22.0;
    static constexpr double row_height = 20.0;
    static constexpr double total_width = 96.0;
    static constexpr double self_width = 96.0;
    static constexpr double calls_width = 76.0;
    static constexpr double share_width = 64.0;
    static constexpr double cell_padding = 6.0;

    explicit DetailsTable(gui_forms::StableId stable_id);
    ~DetailsTable() override;
    DetailsTable(const DetailsTable&) = delete;
    DetailsTable& operator=(const DetailsTable&) = delete;
    void initialize_control_tree();

    void set_inputs(const ProfileInputs& inputs) noexcept;
    void model_changed();
    // After the search changed: the rows are filtered again.
    void search_changed();
    void selection_changed_outside() noexcept;
    void set_sort(TableColumn column, bool descending);
    [[nodiscard]] TableColumn sort_column() const noexcept { return sort_column_; }
    [[nodiscard]] bool sort_descending() const noexcept { return sort_descending_; }

    [[nodiscard]] std::size_t row_count() const noexcept { return row_functions_.size(); }
    [[nodiscard]] std::optional<std::size_t> function_at_row(std::size_t row) const noexcept;
    [[nodiscard]] std::optional<std::size_t> row_of_function(std::size_t function) const noexcept;
    // Selects and reveals the function's row (none clears); raises nothing.
    void select_function(std::optional<std::size_t> function);
    // The column under a local x of the header (none past the edges).
    [[nodiscard]] std::optional<TableColumn> column_at(double x) const noexcept;
    [[nodiscard]] gui_forms::VirtualRowsView& rows() const noexcept { return *rows_; }

    [[nodiscard]] gui_forms::Event<const FunctionPick&>& function_picked() noexcept { return function_picked_; }
    [[nodiscard]] gui_forms::Event<const FunctionPick&>& function_activated() noexcept { return function_activated_; }
    [[nodiscard]] const DetailsTableStatistics& statistics() const& noexcept { return statistics_; }
    const DetailsTableStatistics& statistics() const&& = delete;

    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect local_damage) override;

protected:
    void on_mouse_down(gui_forms::MouseEventArgs& arguments) override;

private:
    struct FunctionOrder;
    // A column's left and right edges, local x.
    struct ColumnEdges final {
        double left{};
        double right{};
    };

    [[nodiscard]] ColumnEdges column_edges(TableColumn column, double width) const noexcept;
    void rebuild_rows();
    void measure_row(gui_forms::VirtualRowMeasure& row);
    void paint_row(gui_forms::VirtualRowPaint& row);
    void describe_row(gui_forms::VirtualRowDescription& row);
    void on_rows_selection(const gui_forms::VirtualRowSelection& selection);
    void on_row_activated(gui_forms::VirtualRowIndex row);

    ProfileInputs inputs_{};
    std::shared_ptr<gui_forms::VirtualRowsView> rows_{};
    TableColumn sort_column_{TableColumn::total};
    bool sort_descending_{true};
    // True while select_function sets the view's row (its event is ours).
    bool selecting_{};
    std::vector<std::uint32_t> row_functions_{};
    // Per function: its row plus one, 0 when filtered out.
    std::vector<std::uint32_t> function_rows_{};
    std::vector<float> name_widths_{};
    double ellipsis_width_{-1.0};
    std::string label_{};
    DetailsTableStatistics statistics_{};
    gui_forms::Event<const FunctionPick&> function_picked_{};
    gui_forms::Event<const FunctionPick&> function_activated_{};
};

} // namespace flame_graph
