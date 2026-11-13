#pragma once

// The ImGui-class window's content (D3): a TabControl of eight pages
// covering an immediate-mode toolkit demo's breadth, built retained:
//
//   Widgets   buttons, check box, radio group, toggle, combo, list, text,
//             numeric up-down, track bar, progress bars with effect
//             programs (the effects the study ranked)
//   Trees     a seeded TreeView with expand and collapse all
//   Tables    a DataGridView of the seeded rows, filtered and sorted by the
//             app, with a value bar and a 32-sample plot drawn in cells
//             (owner draw, SeriesMarkPainter-free: Painter::stroke_polyline)
//   Values    ValueBars with scrub (plain, angle, percent, length), a
//             VectorField of three bars, NumericUpDown with scrub
//   Colour    ColorField (square, hue, alpha) with a ColorValueEditor
//             kept in step, and swatches
//   Plots     Sparklines fed by the table's histories and a PlotView of
//             their mean
//   Panels    a SplitContainer: ExpandableSections on the left, a tree on
//             the right (the docking substitute, GAPS.md I6)
//   Style     a theme sheet generated from an accent hue, a corner radius
//             and light or dark, or edited by hand, compiled through the
//             application's resources and set as the preview's theme
//             override
//
// Everything changes on input or on advance(); nothing redraws on its
// own. Ownership: DemoRoot is a control (shared, owned by its window); it
// owns the model and the theme library, and the pages and controls are
// its descendants. One UI thread.

#include "color_field.hpp"
#include "demo_model.hpp"
#include "form_stack.hpp"
#include "grid_page.hpp"
#include "vector_field.hpp"

#include <gui_forms/control.hpp>
#include <gui_forms/controls/button.hpp>
#include <gui_forms/controls/button_base.hpp>
#include <gui_forms/controls/check_box.hpp>
#include <gui_forms/controls/color_value_editor.hpp>
#include <gui_forms/controls/combo_box.hpp>
#include <gui_forms/controls/data_grid_view.hpp>
#include <gui_forms/controls/expandable_sections.hpp>
#include <gui_forms/controls/label.hpp>
#include <gui_forms/controls/list_box.hpp>
#include <gui_forms/controls/numeric_up_down.hpp>
#include <gui_forms/controls/plot_series_item.hpp>
#include <gui_forms/controls/plot_view.hpp>
#include <gui_forms/controls/progress_bar.hpp>
#include <gui_forms/controls/radio_button.hpp>
#include <gui_forms/controls/sparkline.hpp>
#include <gui_forms/controls/split_container.hpp>
#include <gui_forms/controls/tab_control.hpp>
#include <gui_forms/controls/text_box.hpp>
#include <gui_forms/controls/toggle_switch.hpp>
#include <gui_forms/controls/track_bar.hpp>
#include <gui_forms/controls/tree_view.hpp>
#include <gui_forms/controls/value_bar.hpp>
#include <gui_forms/theme/owner_draw/owner_draw.hpp>
#include <gui_forms/theme/theme_compiler/theme_compiler.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace imgui_class {

enum class DemoPage : std::uint8_t { widgets, trees, tables, values, colour, plots, panels, style };
inline constexpr std::size_t demo_page_count = 8U;

struct DemoRootOptions final {
    std::size_t rows{2000U};
    std::size_t branches{40U};
    std::uint64_t seed{1U};
};

struct DemoRootCounters final {
    std::uint64_t page_switches{};
    std::uint64_t button_clicks{};
    std::uint64_t check_changes{};
    std::uint64_t radio_changes{};
    std::uint64_t toggle_changes{};
    std::uint64_t combo_changes{};
    std::uint64_t list_changes{};
    std::uint64_t text_changes{};
    std::uint64_t slider_changes{};
    std::uint64_t effect_changes{};
    std::uint64_t tree_selections{};
    std::uint64_t tree_expansions{};
    std::uint64_t filter_runs{};
    std::uint64_t sorts{};
    std::uint64_t advances{};
    std::uint64_t grid_cells_written{};
    std::uint64_t cell_plots_painted{};
    std::uint64_t cell_bars_painted{};
    std::uint64_t cell_plot_points{};
    std::uint64_t value_commits{};
    std::uint64_t vector_changes{};
    std::uint64_t colour_changes{};
    std::uint64_t colour_text_changes{};
    std::uint64_t sparkline_pushes{};
    std::uint64_t section_toggles{};
    std::uint64_t style_compiles{};
    std::uint64_t style_refusals{};
    std::uint64_t style_warnings{};
    std::uint64_t style_unattached{};
    std::uint64_t status_updates{};
};

// The style the Style page generates a sheet from.
struct StyleChoice final {
    // Accent hue in turns, [0, 1).
    double accent_hue{0.58};
    // Corner radius in logical units, [0, 12].
    double corner_radius{4.0};
    bool dark{};
};

// The theme sheet for `choice` (GUI.Forms' theme language): colour roles
// for the accent and, when dark, the paper; button faces, check-box
// wells and track-bar tracks in the accent with the radius. Pure.
[[nodiscard]] std::string style_sheet(const StyleChoice& choice);

class DemoRoot final : public gui_forms::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    static constexpr double status_height = 22.0;
    static constexpr std::size_t sparkline_count = 3U;
    static constexpr std::size_t progress_count = 3U;
    static constexpr std::size_t effect_count = 6U;

    DemoRoot(gui_forms::StableId stable_id, DemoRootOptions options);
    explicit DemoRoot(gui_forms::StableId stable_id);
    ~DemoRoot() override;
    DemoRoot(const DemoRoot&) = delete;
    DemoRoot& operator=(const DemoRoot&) = delete;
    void initialize_control_tree();

    // -- parts --------------------------------------------------------------------------
    [[nodiscard]] gui_forms::TabControl& tabs() const noexcept { return *tabs_; }
    void select_page(DemoPage page);
    [[nodiscard]] std::optional<DemoPage> selected_page() const;
    [[nodiscard]] const DemoModel& model() const& noexcept { return model_; }
    const DemoModel& model() const&& = delete;
    [[nodiscard]] gui_forms::Button& counter_button() const noexcept { return *counter_button_; }
    [[nodiscard]] gui_forms::TreeView& tree() const noexcept { return *tree_; }
    [[nodiscard]] gui_forms::DataGridView& grid() const noexcept { return *grid_; }
    [[nodiscard]] gui_forms::ValueBar& plain_bar() const noexcept { return *plain_bar_; }
    [[nodiscard]] VectorField& vector_field() const noexcept { return *vector_field_; }
    [[nodiscard]] gui_forms::NumericUpDown& scrub_number() const noexcept { return *scrub_number_; }
    [[nodiscard]] ColorField& color_field() const noexcept { return *color_field_; }
    [[nodiscard]] gui_forms::ColorValueEditor& color_editor() const noexcept { return *color_editor_; }
    [[nodiscard]] gui_forms::Sparkline& sparkline(std::size_t index) const { return *sparklines_.at(index); }
    [[nodiscard]] gui_forms::PlotView& mean_plot() const noexcept { return *mean_plot_; }
    [[nodiscard]] gui_forms::ExpandableSections& sections() const noexcept { return *sections_; }
    [[nodiscard]] gui_forms::Control& style_preview() const noexcept { return *style_preview_; }
    [[nodiscard]] std::size_t visible_rows() const noexcept { return order_.size(); }
    [[nodiscard]] const std::string& style_text() const& noexcept { return style_text_; }
    const std::string& style_text() const&& = delete;
    [[nodiscard]] StyleChoice style_choice() const noexcept { return style_; }
    // Whether the preview carries a compiled theme override (a later
    // refusal leaves it in place).
    [[nodiscard]] bool style_applied() const noexcept { return style_applied_; }
    [[nodiscard]] DemoRootCounters counters() const noexcept { return counters_; }

    // -- operations (the toolbars, tests and workloads) -----------------------------------
    // Shows the rows whose name or category holds `needle`, in the current
    // sort, and rewrites the grid's cells.
    void set_filter(const std::string& needle);
    void set_sort(TableColumn column, bool ascending);
    // One model step: the histories advance, the grid's value cells, the
    // sparklines and the mean plot follow.
    void advance();
    void set_effect(std::size_t index);
    void set_color(Hsva colour);
    void set_style(StyleChoice choice);
    // Compiles `sheet` and sets it as the preview's theme override.
    // False (the preview keeps its theme) when the root is not attached to
    // a window or the sheet is refused.
    bool apply_style_sheet(const std::string& sheet);
    void expand_all(bool expanded);
    void refresh_status();

    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;

private:
    std::shared_ptr<gui_forms::TabPage> add_page(const char* id, const char* title, const gui_forms::Control::Ptr& content);
    void build_widgets();
    void build_trees();
    void build_tables();
    void build_values();
    void build_colour();
    void build_plots();
    void build_panels();
    void build_style();
    // Writes every visible row's text cells (the history column is drawn).
    void write_grid();
    void write_value_cells();
    void push_sparklines();
    void update_mean_plot();
    void paint_history(gui_forms::ItemContentPaint& item, const TableRow& row);
    void paint_value(gui_forms::ItemContentPaint& item, const TableRow& row);

    void on_page_changed(const gui_forms::TabSelectionChange& change);
    void on_counter_clicked(gui_forms::ButtonBase& button);
    void on_advance_clicked(gui_forms::ButtonBase& button);
    void on_expand_clicked(gui_forms::ButtonBase& button);
    void on_collapse_clicked(gui_forms::ButtonBase& button);
    void on_apply_clicked(gui_forms::ButtonBase& button);
    void on_check_changed(bool checked);
    void on_radio_changed(const std::size_t& index, bool checked);
    void on_toggle_changed(bool checked);
    void on_combo_changed(std::optional<std::size_t> index);
    void on_list_changed(std::optional<std::size_t> index);
    void on_text_changed(const std::string& text);
    void on_slider_changed(double value);
    void on_effect_chosen(std::optional<std::size_t> index);
    void on_tree_selected(const gui_forms::TreeSelectionChange& change);
    void on_tree_expanded(const gui_forms::TreeExpansionChange& change);
    void on_filter_changed(const std::string& text);
    void on_sort_chosen(std::optional<std::size_t> index);
    void on_descending_changed(bool checked);
    void on_draw_cell(gui_forms::ItemContentPaint& item);
    void on_value_committed(double value);
    void on_vector_changed(const Vector3& value);
    void on_color_field_changed(const Hsva& colour);
    void on_color_text_changed(gui_forms::Color colour);
    void on_section_toggled(int index, bool expanded);
    void on_accent_changed(double value);
    void on_radius_changed(double value);
    void on_dark_changed(bool checked);

    DemoRootOptions options_{};
    DemoModel model_;
    // Row indices shown, in order; reserved at the row count.
    std::vector<std::size_t> order_{};
    std::string filter_{};
    TableColumn sort_column_{TableColumn::name};
    bool sort_ascending_{true};
    // The cell plot's points, reused by every cell (history_length).
    std::vector<gui_forms::Point> cell_points_{};
    // The mean plot's values, reused (history_length).
    std::vector<double> mean_values_{};
    gui_forms::ThemeLibrary theme_library_{};
    StyleChoice style_{};
    std::string style_text_{};
    bool style_applied_{};
    bool updating_colour_{};
    bool updating_style_{};

    std::shared_ptr<gui_forms::TabControl> tabs_{};
    std::shared_ptr<gui_forms::Label> status_{};
    // Widgets
    std::shared_ptr<gui_forms::Button> counter_button_{};
    std::shared_ptr<gui_forms::Label> widget_readout_{};
    std::array<std::shared_ptr<gui_forms::RadioButton>, 3> radios_{};
    std::array<std::shared_ptr<gui_forms::ProgressBar>, progress_count> progress_{};
    std::shared_ptr<gui_forms::ComboBox> effect_box_{};
    // Trees
    std::shared_ptr<gui_forms::TreeView> tree_{};
    std::shared_ptr<gui_forms::Label> tree_readout_{};
    // Tables
    std::shared_ptr<gui_forms::DataGridView> grid_{};
    std::shared_ptr<gui_forms::TextBox> filter_box_{};
    std::shared_ptr<gui_forms::ComboBox> sort_box_{};
    // Values
    std::shared_ptr<gui_forms::ValueBar> plain_bar_{};
    std::shared_ptr<VectorField> vector_field_{};
    std::shared_ptr<gui_forms::NumericUpDown> scrub_number_{};
    std::shared_ptr<gui_forms::Label> value_readout_{};
    // Colour
    std::shared_ptr<ColorField> color_field_{};
    std::shared_ptr<gui_forms::ColorValueEditor> color_editor_{};
    // Plots
    std::array<std::shared_ptr<gui_forms::Sparkline>, sparkline_count> sparklines_{};
    std::shared_ptr<gui_forms::PlotView> mean_plot_{};
    std::shared_ptr<gui_forms::PlotSeriesItem> mean_series_{};
    // Panels
    std::shared_ptr<gui_forms::SplitContainer> split_{};
    std::shared_ptr<gui_forms::ExpandableSections> sections_{};
    // Style
    std::shared_ptr<gui_forms::ValueBar> accent_bar_{};
    std::shared_ptr<gui_forms::ValueBar> radius_bar_{};
    std::shared_ptr<gui_forms::CheckBox> dark_box_{};
    std::shared_ptr<gui_forms::TextBox> sheet_box_{};
    std::shared_ptr<gui_forms::Label> style_readout_{};
    std::shared_ptr<GridPage> style_preview_{};

    std::string status_text_{};
    DemoRootCounters counters_{};
};

} // namespace imgui_class
