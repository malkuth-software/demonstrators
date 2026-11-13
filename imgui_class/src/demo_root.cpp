#include "demo_root.hpp"

#include <gui_forms/controls/plot_axis_item.hpp>
#include <gui_forms/controls/tab_page.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/resources/application_resources/application_resources.hpp>
#include <gui_forms/surface_root/surface_root.hpp>
#include <gui_forms/theme/stock_anatomies/stock_anatomies.hpp>
#include <gui_forms/types/paint_marks/paint_marks.hpp>
#include <gui_forms/types/painter/painter.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <string_view>
#include <utility>

namespace imgui_class {

namespace gf = gui_forms;

namespace {

// The progress bars' effect programs (progress_bar_effects.hpp); the
// first is none.
constexpr std::array<std::string_view, DemoRoot::effect_count> effect_names{
    "", "progress_stripes", "progress_pulse", "progress_laser", "progress_woven_stripes", "progress_bubbles"};
constexpr std::array<std::string_view, DemoRoot::effect_count> effect_labels{
    "No effect", "Stripes", "Pulse", "Laser", "Woven stripes", "Bubbles"};
constexpr std::array<std::string_view, table_column_count> column_labels{"Name", "Category", "Value", "Enabled", "History"};
constexpr std::array<TableColumn, table_column_count> column_order{TableColumn::name, TableColumn::category, TableColumn::value,
                                                                   TableColumn::enabled, TableColumn::history};
constexpr std::string_view theme_name = "imgui-class-style";

constexpr gf::Color cell_line = gf::Color::rgba(38, 110, 190, 255);
constexpr gf::Color cell_bar = gf::Color::rgba(80, 160, 110, 255);
constexpr gf::Color cell_bar_track = gf::Color::rgba(222, 226, 230, 255);
constexpr gf::Color cell_text = gf::Color::rgba(30, 34, 40, 255);

void append_count(std::string& out, std::uint64_t value) {
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    if (written > 0) out.append(buffer, static_cast<std::size_t>(written));
}

std::string format_number(const char* pattern, double value) {
    char buffer[64]{};
    const int written = std::snprintf(buffer, sizeof(buffer), pattern, value);
    std::string text{};
    if (written > 0) text.assign(buffer, static_cast<std::size_t>(written));
    return text;
}

std::shared_ptr<gf::Label> make_label(const std::string& id, std::string text) {
    std::shared_ptr<gf::Label> label = gf::make_control<gf::Label>(gf::StableId(id), std::move(text));
    return label;
}

std::vector<gf::TreeViewItem> tree_items(std::span<const TreeNode> nodes) {
    std::vector<gf::TreeViewItem> items{};
    items.reserve(nodes.size());
    for (const TreeNode& node : nodes) {
        gf::TreeViewItem item{};
        item.stable_id = node.id;
        item.text = node.text;
        item.depth = node.depth;
        item.expandable = node.expandable;
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace

std::string style_sheet(const StyleChoice& choice) {
    const gf::Color accent = hsv_color(Hsva{choice.accent_hue, 0.72, 0.78, 1.0});
    const gf::Color accent_soft = hsv_color(Hsva{choice.accent_hue, 0.25, 0.97, 1.0});
    const std::string accent_text = format_hex(accent);
    const std::string soft_text = format_hex(accent_soft);
    const std::string radius = format_number("%.1fpx", std::clamp(choice.corner_radius, 0.0, 12.0));
    std::string sheet{};
    sheet.reserve(640U);
    sheet.append(":root {\n");
    sheet.append("  selection-ink-on-selection: #ffffff on ").append(accent_text).append(";\n");
    sheet.append("  on-accent-on-accent: #ffffff on ").append(accent_text).append(";\n");
    sheet.append("  focus: ").append(accent_text).append(";\n");
    if (choice.dark) {
        sheet.append("  ink-on-paper: #e6e8eb on #1e2226;\n");
        sheet.append("  ink-on-chrome: #e6e8eb on #2a2f35;\n");
    }
    sheet.append("}\n");
    sheet.append("button::part(face) {\n  background-color: ").append(soft_text).append(";\n");
    sheet.append("  border: 1px solid ").append(accent_text).append(";\n");
    sheet.append("  border-radius: ").append(radius).append(";\n}\n");
    sheet.append("check-box::part(well) {\n  border-radius: ").append(radius).append(";\n}\n");
    sheet.append("track-bar::part(track) {\n  background-color: ").append(accent_text).append(";\n");
    sheet.append("  border-radius: ").append(radius).append(";\n}\n");
    return sheet;
}

DemoRoot::DemoRoot(gf::StableId stable_id, DemoRootOptions options)
    : gf::Control(std::move(stable_id)), options_(options), model_(options.rows, options.branches, options.seed) {
    order_.reserve(options.rows);
    cell_points_.reserve(history_length);
    mean_values_.resize(history_length);
    status_text_.reserve(256U);
    filter_.reserve(64U);
}

DemoRoot::DemoRoot(gf::StableId stable_id) : DemoRoot(std::move(stable_id), DemoRootOptions{}) {}

DemoRoot::~DemoRoot() {}

void DemoRoot::initialize_control_tree() {
    tabs_ = gf::make_control<gf::TabControl>(gf::StableId("imgui.tabs"));
    status_ = gf::make_control<gf::Label>(gf::StableId("imgui.status"));
    add_child(tabs_);
    add_child(status_);
    build_widgets();
    build_trees();
    build_tables();
    build_values();
    build_colour();
    build_plots();
    build_panels();
    build_style();
    gf::on((*tabs_).selected_index_changed(), *this, &DemoRoot::on_page_changed);
    refresh_status();
}

std::shared_ptr<gf::TabPage> DemoRoot::add_page(const char* id, const char* title, const gf::Control::Ptr& content) {
    const std::string base(id);
    std::shared_ptr<gf::TabPage> page = gf::make_control<gf::TabPage>(gf::StableId(base + ".page"), std::string(title));
    (*content).dock = gf::DockStyle::fill;
    (*page).add_child(content);
    (*tabs_).add_page(page);
    return page;
}

// -- pages ----------------------------------------------------------------------------------

void DemoRoot::build_widgets() {
    std::shared_ptr<FormStack> form = gf::make_control<FormStack>(gf::StableId("imgui.widgets"));
    counter_button_ = gf::make_control<gf::Button>(gf::StableId("imgui.widgets.counter"), "Click me");
    gf::on((*counter_button_).clicked(), *this, &DemoRoot::on_counter_clicked);
    (*form).add_row("Button", counter_button_, 26.0, 160.0);

    std::shared_ptr<gf::CheckBox> check = gf::make_control<gf::CheckBox>(gf::StableId("imgui.widgets.check"), "Enable the thing");
    gf::on((*check).checked_changed(), *this, &DemoRoot::on_check_changed);
    (*form).add_row("Check box", check, 24.0);

    // The radio group: three buttons in one row, each its own row here
    // (FormStack rows are single controls; GAPS.md I9).
    constexpr std::array<std::string_view, 3> radio_labels{"Radio a", "Radio b", "Radio c"};
    for (std::size_t index = 0U; index < radios_.size(); ++index) {
        const std::string id = "imgui.widgets.radio-" + std::to_string(index);
        radios_[index] = gf::make_control<gf::RadioButton>(gf::StableId(id), std::string(radio_labels[index]));
        (*radios_[index]).group_name = std::string("imgui-radios");
        gf::on((*radios_[index]).checked_changed(), *this, &DemoRoot::on_radio_changed, index);
        (*form).add_row(index == 0U ? "Radio group" : "", radios_[index], 22.0, 160.0);
    }
    (*radios_[0]).checked = true;

    std::shared_ptr<gf::ToggleSwitch> toggle = gf::make_control<gf::ToggleSwitch>(gf::StableId("imgui.widgets.toggle"), "Toggle");
    gf::on((*toggle).checked_changed(), *this, &DemoRoot::on_toggle_changed);
    (*form).add_row("Toggle switch", toggle, 24.0, 160.0);

    std::shared_ptr<gf::ComboBox> combo = gf::make_control<gf::ComboBox>(gf::StableId("imgui.widgets.combo"));
    std::vector<std::string> fruit{"Apple", "Banana", "Cherry", "Kiwi", "Mango", "Orange", "Pineapple", "Strawberry"};
    (*combo).items = std::move(fruit);
    (*combo).selected_index = std::optional<std::size_t>{0U};
    gf::on((*combo).selected_index_changed(), *this, &DemoRoot::on_combo_changed);
    (*form).add_row("Combo", combo, 24.0, 220.0);

    std::shared_ptr<gf::ListBox> list = gf::make_control<gf::ListBox>(gf::StableId("imgui.widgets.list"));
    std::vector<std::string> entries{};
    entries.reserve(24U);
    for (std::size_t index = 0U; index < 24U; ++index) entries.push_back("List item " + std::to_string(index + 1U));
    (*list).items = std::move(entries);
    gf::on((*list).selected_index_changed(), *this, &DemoRoot::on_list_changed);
    (*form).add_row("List box", list, 96.0, 220.0);

    std::shared_ptr<gf::TextBox> text = gf::make_control<gf::TextBox>(gf::StableId("imgui.widgets.text"), "Hello, world!");
    (*text).placeholder_text = std::string("Type here");
    gf::on((*text).text_changed(), *this, &DemoRoot::on_text_changed);
    (*form).add_row("Text input", text, 24.0, 320.0);

    std::shared_ptr<gf::TextBox> multiline = gf::make_control<gf::TextBox>(gf::StableId("imgui.widgets.multiline"),
                                                                           "Several lines\nof text\nwrapped and scrolled.");
    (*multiline).multiline = true;
    (*multiline).word_wrap = true;
    (*form).add_row("Multiline", multiline, 64.0, 320.0);

    std::shared_ptr<gf::NumericUpDown> number = gf::make_control<gf::NumericUpDown>(gf::StableId("imgui.widgets.number"));
    (*number).set_range(-100.0, 100.0);
    (*number).decimal_places = static_cast<std::uint8_t>(2U);
    (*number).increment = 0.25;
    (*form).add_row("Numeric", number, 24.0, 160.0);

    std::shared_ptr<gf::TrackBar> slider = gf::make_control<gf::TrackBar>(gf::StableId("imgui.widgets.slider"));
    (*slider).set_range(0.0, 100.0);
    (*slider).value = 50.0;
    gf::on((*slider).value_changed(), *this, &DemoRoot::on_slider_changed);
    (*form).add_row("Slider", slider, 28.0, 320.0);

    effect_box_ = gf::make_control<gf::ComboBox>(gf::StableId("imgui.widgets.effect"));
    std::vector<std::string> effects{};
    effects.reserve(effect_count);
    for (const std::string_view label : effect_labels) effects.push_back(std::string(label));
    (*effect_box_).items = std::move(effects);
    (*effect_box_).selected_index = std::optional<std::size_t>{1U};
    gf::on((*effect_box_).selected_index_changed(), *this, &DemoRoot::on_effect_chosen);
    (*form).add_row("Progress effect", effect_box_, 24.0, 220.0);
    for (std::size_t index = 0U; index < progress_.size(); ++index) {
        const std::string id = "imgui.widgets.progress-" + std::to_string(index);
        progress_[index] = gf::make_control<gf::ProgressBar>(gf::StableId(id));
        (*progress_[index]).set_range(0.0, 100.0);
        (*progress_[index]).value = 25.0 + 30.0 * static_cast<double>(index);
        (*progress_[index]).effect = std::string(effect_names[1]);
        (*form).add_row(index == 0U ? "Progress" : "", progress_[index], 18.0, 320.0);
    }

    widget_readout_ = make_label("imgui.widgets.readout", "Nothing clicked yet");
    (*form).add_row("Last event", widget_readout_, 22.0);
    static_cast<void>(add_page("imgui.widgets", "Widgets", form));
}

void DemoRoot::build_trees() {
    std::shared_ptr<GridPage> page = gf::make_control<GridPage>(gf::StableId("imgui.trees"), 1U);
    std::shared_ptr<gf::Button> expand = gf::make_control<gf::Button>(gf::StableId("imgui.trees.expand"), "Expand all");
    std::shared_ptr<gf::Button> collapse = gf::make_control<gf::Button>(gf::StableId("imgui.trees.collapse"), "Collapse all");
    tree_readout_ = make_label("imgui.trees.readout", "No selection");
    (*page).add_tool(expand, 110.0);
    (*page).add_tool(collapse, 110.0);
    (*page).add_tool(tree_readout_, 360.0);
    tree_ = gf::make_control<gf::TreeView>(gf::StableId("imgui.trees.tree"));
    (*tree_).set_items(tree_items(model_.tree()));
    (*page).add_cell(tree_);
    gf::on((*expand).clicked(), *this, &DemoRoot::on_expand_clicked);
    gf::on((*collapse).clicked(), *this, &DemoRoot::on_collapse_clicked);
    gf::on((*tree_).selection_changed(), *this, &DemoRoot::on_tree_selected);
    gf::on((*tree_).expansion_changed(), *this, &DemoRoot::on_tree_expanded);
    static_cast<void>(add_page("imgui.trees", "Trees", page));
}

void DemoRoot::build_tables() {
    std::shared_ptr<GridPage> page = gf::make_control<GridPage>(gf::StableId("imgui.tables"), 1U);
    filter_box_ = gf::make_control<gf::TextBox>(gf::StableId("imgui.tables.filter"));
    (*filter_box_).placeholder_text = std::string("Filter (name or category)");
    sort_box_ = gf::make_control<gf::ComboBox>(gf::StableId("imgui.tables.sort"));
    std::vector<std::string> sorts{};
    sorts.reserve(table_column_count);
    for (const std::string_view label : column_labels) sorts.push_back("Sort by " + std::string(label));
    (*sort_box_).items = std::move(sorts);
    (*sort_box_).selected_index = std::optional<std::size_t>{0U};
    std::shared_ptr<gf::CheckBox> descending = gf::make_control<gf::CheckBox>(gf::StableId("imgui.tables.descending"), "Descending");
    std::shared_ptr<gf::Button> step = gf::make_control<gf::Button>(gf::StableId("imgui.tables.advance"), "Advance");
    (*page).add_tool(filter_box_, 240.0);
    (*page).add_tool(sort_box_, 170.0);
    (*page).add_tool(descending, 110.0);
    (*page).add_tool(step, 90.0);

    grid_ = gf::make_control<gf::DataGridView>(gf::StableId("imgui.tables.grid"));
    std::vector<gf::DataGridViewColumn> columns{};
    columns.reserve(table_column_count);
    for (std::size_t index = 0U; index < table_column_count; ++index) {
        gf::DataGridViewColumn column{};
        column.name = "c" + std::to_string(index);
        column.header_text = std::string(column_labels[index]);
        column.read_only = true;
        // The app sorts (it owns the order the cell plots map through).
        column.sort_mode = gf::DataGridViewColumnSortMode::not_sortable;
        column.width = index == 0U ? 160.0 : (index == 4U ? 180.0 : 110.0);
        columns.push_back(std::move(column));
    }
    (*grid_).set_columns(std::move(columns));
    (*grid_).read_only = true;
    (*grid_).allow_user_to_add_rows = false;
    // Owner draw: every cell is raised to on_draw_cell, which draws the
    // value bar and the history plot and asks for the default text in
    // the rest (GAPS.md I4: no per-column owner draw).
    (*grid_).draw_mode = gf::ItemDrawMode::owner_draw;
    (*page).add_cell(grid_);
    gf::on((*grid_).draw_item_content(), *this, &DemoRoot::on_draw_cell);
    gf::on((*filter_box_).text_changed(), *this, &DemoRoot::on_filter_changed);
    gf::on((*sort_box_).selected_index_changed(), *this, &DemoRoot::on_sort_chosen);
    gf::on((*descending).checked_changed(), *this, &DemoRoot::on_descending_changed);
    gf::on((*step).clicked(), *this, &DemoRoot::on_advance_clicked);
    static_cast<void>(add_page("imgui.tables", "Tables", page));
    set_filter(std::string{});
}

void DemoRoot::build_values() {
    std::shared_ptr<FormStack> form = gf::make_control<FormStack>(gf::StableId("imgui.values"));
    plain_bar_ = gf::make_control<gf::ValueBar>(gf::StableId("imgui.values.plain"));
    (*plain_bar_).set_range(-1000.0, 1000.0);
    (*plain_bar_).soft_minimum = std::optional<double>{0.0};
    (*plain_bar_).soft_maximum = std::optional<double>{10.0};
    (*plain_bar_).value = 2.5;
    (*plain_bar_).label = std::string("Scrub me");
    gf::on((*plain_bar_).value_committed(), *this, &DemoRoot::on_value_committed);
    (*form).add_row("Value (soft 0..10)", plain_bar_, 24.0, 320.0);

    std::shared_ptr<gf::ValueBar> angle = gf::make_control<gf::ValueBar>(gf::StableId("imgui.values.angle"));
    (*angle).set_range(-6.283185307179586, 6.283185307179586);
    (*angle).unit = gf::ValueUnit::angle;
    (*angle).label = std::string("Angle");
    (*angle).value = 0.7853981633974483;
    gf::on((*angle).value_committed(), *this, &DemoRoot::on_value_committed);
    (*form).add_row("Angle", angle, 24.0, 320.0);

    std::shared_ptr<gf::ValueBar> percent = gf::make_control<gf::ValueBar>(gf::StableId("imgui.values.percent"));
    (*percent).set_range(0.0, 1.0);
    (*percent).unit = gf::ValueUnit::percent;
    (*percent).label = std::string("Opacity");
    (*percent).value = 0.8;
    gf::on((*percent).value_committed(), *this, &DemoRoot::on_value_committed);
    (*form).add_row("Percent", percent, 24.0, 320.0);

    std::shared_ptr<gf::ValueBar> length = gf::make_control<gf::ValueBar>(gf::StableId("imgui.values.length"));
    (*length).set_range(0.0, 100.0);
    (*length).unit = gf::ValueUnit::length;
    (*length).presentation = gf::ValueBarStyle::field;
    (*length).label = std::string("Length");
    (*length).value = 1.75;
    gf::on((*length).value_committed(), *this, &DemoRoot::on_value_committed);
    (*form).add_row("Length (field)", length, 24.0, 320.0);

    vector_field_ = gf::make_control<VectorField>(gf::StableId("imgui.values.vector"), -10.0, 10.0);
    (*vector_field_).set_vector(Vector3{1.0, 2.0, 3.0});
    gf::on((*vector_field_).vector_changed(), *this, &DemoRoot::on_vector_changed);
    (*form).add_row("Vector3", vector_field_, 24.0, 420.0);

    scrub_number_ = gf::make_control<gf::NumericUpDown>(gf::StableId("imgui.values.scrub"));
    (*scrub_number_).set_range(0.0, 255.0);
    (*scrub_number_).scrub = true;
    (*scrub_number_).value = 128.0;
    (*form).add_row("Numeric (scrub)", scrub_number_, 24.0, 160.0);

    value_readout_ = make_label("imgui.values.readout", "");
    (*form).add_row("Last commit", value_readout_, 22.0);
    static_cast<void>(add_page("imgui.values", "Values", form));
}

void DemoRoot::build_colour() {
    std::shared_ptr<FormStack> form = gf::make_control<FormStack>(gf::StableId("imgui.colour"));
    color_field_ = gf::make_control<ColorField>(gf::StableId("imgui.colour.field"));
    (*color_field_).set_color(Hsva{0.58, 0.7, 0.8, 1.0});
    (*form).add_row("Picker", color_field_, 220.0, 300.0);
    color_editor_ = gf::make_control<gf::ColorValueEditor>(gf::StableId("imgui.colour.editor"),
                                                           hsv_color((*color_field_).color()));
    (*form).add_row("Hex", color_editor_, 24.0, 220.0);
    gf::on((*color_field_).color_changed(), *this, &DemoRoot::on_color_field_changed);
    gf::on((*color_editor_).value_changed(), *this, &DemoRoot::on_color_text_changed);
    static_cast<void>(add_page("imgui.colour", "Colour", form));
}

void DemoRoot::build_plots() {
    std::shared_ptr<GridPage> page = gf::make_control<GridPage>(gf::StableId("imgui.plots"), 2U);
    for (std::size_t index = 0U; index < sparklines_.size(); ++index) {
        const std::string id = "imgui.plots.spark-" + std::to_string(index);
        sparklines_[index] = gf::make_control<gf::Sparkline>(gf::StableId(id));
        (*sparklines_[index]).capacity = static_cast<std::uint32_t>(120U);
        (*sparklines_[index]).fill = index == 1U;
        (*page).add_cell(sparklines_[index]);
    }
    mean_plot_ = gf::make_control<gf::PlotView>(gf::StableId("imgui.plots.mean"));
    (*mean_plot_).title = std::string("Mean of the visible rows' histories");
    std::shared_ptr<gf::PlotAxisItem> x = gf::make_control<gf::PlotAxisItem>(gf::StableId("imgui.plots.mean.x"));
    (*x).direction = gf::PlotAxisDirection::horizontal;
    (*x).auto_fit = true;
    std::shared_ptr<gf::PlotAxisItem> y = gf::make_control<gf::PlotAxisItem>(gf::StableId("imgui.plots.mean.y"));
    (*y).direction = gf::PlotAxisDirection::vertical;
    (*y).auto_fit = false;
    (*y).minimum = 0.0;
    (*y).maximum = 1.0;
    mean_series_ = gf::make_control<gf::PlotSeriesItem>(gf::StableId("imgui.plots.mean.series"));
    (*mean_series_).kind = gf::PlotSeriesKind::shaded;
    (*mean_series_).label = std::string("mean");
    (*mean_plot_).add_child(x);
    (*mean_plot_).add_child(y);
    (*mean_plot_).add_child(mean_series_);
    (*page).add_cell(mean_plot_);
    static_cast<void>(add_page("imgui.plots", "Plots", page));
    push_sparklines();
    update_mean_plot();
}

void DemoRoot::build_panels() {
    split_ = gf::make_control<gf::SplitContainer>(gf::StableId("imgui.panels"));
    (*split_).orientation = gf::Orientation::vertical;
    sections_ = gf::make_control<gf::ExpandableSections>(gf::StableId("imgui.panels.sections"));
    std::vector<gf::SectionItem> items{};
    items.reserve(3U);
    gf::SectionItem general{};
    general.id = 1;
    general.label = std::string("General");
    general.content = gf::make_control<gf::CheckBox>(gf::StableId("imgui.panels.general"), "Show grid");
    items.push_back(std::move(general));
    gf::SectionItem sizes{};
    sizes.id = 2;
    sizes.label = std::string("Sizes");
    std::shared_ptr<gf::TrackBar> size = gf::make_control<gf::TrackBar>(gf::StableId("imgui.panels.size"));
    (*size).set_range(1.0, 32.0);
    (*size).value = 8.0;
    sizes.content = size;
    items.push_back(std::move(sizes));
    gf::SectionItem notes{};
    notes.id = 3;
    notes.label = std::string("Notes");
    notes.content = make_label("imgui.panels.notes", "Panels stand in for docking (GAPS.md I6).");
    items.push_back(std::move(notes));
    (*sections_).set_items(std::move(items));
    (*sections_).dock = gf::DockStyle::fill;
    (*(*split_).first_panel()).add_child(sections_);
    std::shared_ptr<gf::TreeView> outline = gf::make_control<gf::TreeView>(gf::StableId("imgui.panels.outline"));
    (*outline).set_items(tree_items(model_.tree()));
    (*outline).dock = gf::DockStyle::fill;
    (*(*split_).second_panel()).add_child(outline);
    gf::on((*sections_).toggled(), *this, &DemoRoot::on_section_toggled);
    static_cast<void>(add_page("imgui.panels", "Panels", split_));
}

void DemoRoot::build_style() {
    std::shared_ptr<GridPage> page = gf::make_control<GridPage>(gf::StableId("imgui.style"), 2U);
    accent_bar_ = gf::make_control<gf::ValueBar>(gf::StableId("imgui.style.accent"));
    (*accent_bar_).set_range(0.0, 1.0);
    (*accent_bar_).label = std::string("Accent hue");
    (*accent_bar_).value = style_.accent_hue;
    radius_bar_ = gf::make_control<gf::ValueBar>(gf::StableId("imgui.style.radius"));
    (*radius_bar_).set_range(0.0, 12.0);
    (*radius_bar_).label = std::string("Corner radius");
    (*radius_bar_).value = style_.corner_radius;
    dark_box_ = gf::make_control<gf::CheckBox>(gf::StableId("imgui.style.dark"), "Dark");
    std::shared_ptr<gf::Button> apply = gf::make_control<gf::Button>(gf::StableId("imgui.style.apply"), "Apply sheet");
    (*page).add_tool(accent_bar_, 200.0);
    (*page).add_tool(radius_bar_, 200.0);
    (*page).add_tool(dark_box_, 70.0);
    (*page).add_tool(apply, 100.0);

    std::shared_ptr<FormStack> editor = gf::make_control<FormStack>(gf::StableId("imgui.style.editor"));
    style_text_ = style_sheet(style_);
    sheet_box_ = gf::make_control<gf::TextBox>(gf::StableId("imgui.style.sheet"), style_text_);
    (*sheet_box_).multiline = true;
    (*editor).add_row("Theme sheet", sheet_box_, 300.0);
    style_readout_ = make_label("imgui.style.readout", "Not applied");
    (*editor).add_row("Result", style_readout_, 44.0);
    (*page).add_cell(editor);

    // The preview: stock controls under the compiled theme override.
    style_preview_ = gf::make_control<GridPage>(gf::StableId("imgui.style.preview"), 1U);
    std::shared_ptr<FormStack> sample = gf::make_control<FormStack>(gf::StableId("imgui.style.preview.form"));
    (*sample).add_row("Button", gf::make_control<gf::Button>(gf::StableId("imgui.style.preview.button"), "Accent"), 26.0, 140.0);
    (*sample).add_row("Check box", gf::make_control<gf::CheckBox>(gf::StableId("imgui.style.preview.check"), "Checked"), 24.0);
    std::shared_ptr<gf::TrackBar> track = gf::make_control<gf::TrackBar>(gf::StableId("imgui.style.preview.track"));
    (*track).set_range(0.0, 10.0);
    (*track).value = 6.0;
    (*sample).add_row("Track bar", track, 28.0, 240.0);
    std::shared_ptr<gf::ProgressBar> bar = gf::make_control<gf::ProgressBar>(gf::StableId("imgui.style.preview.progress"));
    (*bar).set_range(0.0, 1.0);
    (*bar).value = 0.6;
    (*bar).effect = std::string("progress_woven_stripes");
    (*sample).add_row("Progress", bar, 18.0, 240.0);
    std::shared_ptr<gf::ValueBar> value = gf::make_control<gf::ValueBar>(gf::StableId("imgui.style.preview.value"));
    (*value).set_range(0.0, 1.0);
    (*value).value = 0.4;
    (*sample).add_row("Value bar", value, 24.0, 240.0);
    (*style_preview_).add_cell(sample);
    (*page).add_cell(style_preview_);

    gf::on((*accent_bar_).value_changed(), *this, &DemoRoot::on_accent_changed);
    gf::on((*radius_bar_).value_changed(), *this, &DemoRoot::on_radius_changed);
    gf::on((*dark_box_).checked_changed(), *this, &DemoRoot::on_dark_changed);
    gf::on((*apply).clicked(), *this, &DemoRoot::on_apply_clicked);
    static_cast<void>(add_page("imgui.style", "Style", page));
}

// -- operations ------------------------------------------------------------------------------

void DemoRoot::select_page(DemoPage page) {
    (*tabs_).selected_index = std::optional<std::size_t>{static_cast<std::size_t>(page)};
}

std::optional<DemoPage> DemoRoot::selected_page() const {
    const std::optional<std::size_t> index = (*tabs_).selected_index.read();
    if (!index.has_value() || *index >= demo_page_count) return std::nullopt;
    return static_cast<DemoPage>(*index);
}

void DemoRoot::set_filter(const std::string& needle) {
    filter_ = needle;
    model_.filter(filter_, order_);
    model_.sort(order_, sort_column_, sort_ascending_);
    ++counters_.filter_runs;
    write_grid();
    update_mean_plot();
}

void DemoRoot::set_sort(TableColumn column, bool ascending) {
    sort_column_ = column;
    sort_ascending_ = ascending;
    model_.sort(order_, sort_column_, sort_ascending_);
    ++counters_.sorts;
    write_grid();
}

void DemoRoot::write_grid() {
    (*grid_).set_row_count(order_.size());
    const std::span<const TableRow> rows = model_.rows();
    for (std::size_t position = 0U; position < order_.size(); ++position) {
        const TableRow& row = rows[order_[position]];
        const gf::DataGridViewRowPosition at{position};
        static_cast<void>((*grid_).set_cell_value(at, 0U, gf::BindingValue{row.name}));
        static_cast<void>((*grid_).set_cell_value(at, 1U, gf::BindingValue{row.category}));
        static_cast<void>((*grid_).set_cell_value(at, 2U, gf::BindingValue{format_number("%.1f", row.value)}));
        static_cast<void>((*grid_).set_cell_value(at, 3U, gf::BindingValue{std::string(row.enabled ? "yes" : "no")}));
        counters_.grid_cells_written += 4U;
    }
}

void DemoRoot::write_value_cells() {
    const std::span<const TableRow> rows = model_.rows();
    for (std::size_t position = 0U; position < order_.size(); ++position) {
        const TableRow& row = rows[order_[position]];
        static_cast<void>((*grid_).set_cell_value(gf::DataGridViewRowPosition{position}, 2U,
                                                  gf::BindingValue{format_number("%.1f", row.value)}));
        ++counters_.grid_cells_written;
    }
    // The history column holds no value; its plots must repaint.
    (*grid_).invalidate(gf::Dirty::paint);
}

void DemoRoot::advance() {
    static_cast<void>(model_.advance());
    ++counters_.advances;
    // A value sort follows the new values; other sorts keep their order.
    if (sort_column_ == TableColumn::value || sort_column_ == TableColumn::history) {
        model_.sort(order_, sort_column_, sort_ascending_);
        write_grid();
        (*grid_).invalidate(gf::Dirty::paint);
    } else {
        write_value_cells();
    }
    push_sparklines();
    update_mean_plot();
    refresh_status();
}

void DemoRoot::push_sparklines() {
    const std::span<const TableRow> rows = model_.rows();
    for (std::size_t index = 0U; index < sparklines_.size() && index < rows.size(); ++index) {
        (*sparklines_[index]).push(rows[index].history.back());
        ++counters_.sparkline_pushes;
    }
}

void DemoRoot::update_mean_plot() {
    std::fill(mean_values_.begin(), mean_values_.end(), 0.0);
    if (order_.empty()) {
        (*mean_series_).set_values(std::span<const double>(mean_values_));
        return;
    }
    const std::span<const TableRow> rows = model_.rows();
    for (const std::size_t index : order_) {
        const TableRow& row = rows[index];
        for (std::size_t sample = 0U; sample < history_length; ++sample) mean_values_[sample] += row.history[sample];
    }
    const double count = static_cast<double>(order_.size());
    for (double& value : mean_values_) value /= count;
    (*mean_series_).set_values(std::span<const double>(mean_values_));
}

void DemoRoot::set_effect(std::size_t index) {
    if (index >= effect_names.size()) return;
    for (const std::shared_ptr<gf::ProgressBar>& bar : progress_) (*bar).effect = std::string(effect_names[index]);
    ++counters_.effect_changes;
}

void DemoRoot::set_color(Hsva colour) {
    updating_colour_ = true;
    (*color_field_).set_color(colour);
    (*color_editor_).value = hsv_color((*color_field_).color());
    updating_colour_ = false;
}

void DemoRoot::set_style(StyleChoice choice) {
    style_ = choice;
    updating_style_ = true;
    (*accent_bar_).value = std::clamp(choice.accent_hue, 0.0, 1.0);
    (*radius_bar_).value = std::clamp(choice.corner_radius, 0.0, 12.0);
    (*dark_box_).checked = choice.dark;
    updating_style_ = false;
    style_text_ = style_sheet(style_);
    (*sheet_box_).text = style_text_;
    static_cast<void>(apply_style_sheet(style_text_));
}

bool DemoRoot::apply_style_sheet(const std::string& sheet) {
    gf::SurfaceRoot* surface = attached_surface_root();
    if (surface == nullptr) {
        ++counters_.style_unattached;
        (*style_readout_).text = std::string("Not attached to a window");
        return false;
    }
    std::shared_ptr<gf::ApplicationResources> resources = (*surface).resources();
    if (resources == nullptr) {
        ++counters_.style_unattached;
        (*style_readout_).text = std::string("The window has no resources");
        return false;
    }
    const gf::ThemePackage package = gf::theme_sheet_package(sheet, theme_name);
    gf::ThemeCompileOptions options{};
    options.controls = gf::stock_control_anatomies();
    const gf::ThemeCompileResult result = (*resources).compile_theme_package(package, theme_library_, options);
    ++counters_.style_compiles;
    std::string readout{};
    readout.reserve(160U);
    const std::span<const gf::ThemeCompileDiagnostic> diagnostics = result.diagnostics();
    if (!result.succeeded()) {
        ++counters_.style_refusals;
        readout.append("Refused: ");
        if (!diagnostics.empty()) {
            const gf::ThemeCompileDiagnostic& first = diagnostics.front();
            readout.append(first.code).append(" ").append(first.name);
        }
        // The preview keeps the theme it had (style_applied_ unchanged).
        (*style_readout_).text = readout;
        return false;
    }
    counters_.style_warnings += diagnostics.size();
    (*style_preview_).set_theme_override(result.theme());
    style_applied_ = true;
    readout.append("Applied, ");
    append_count(readout, diagnostics.size());
    readout.append(" warnings");
    if (!diagnostics.empty()) readout.append(" (first ").append(diagnostics.front().code).append(")");
    (*style_readout_).text = readout;
    return true;
}

void DemoRoot::expand_all(bool expanded) {
    for (const TreeNode& node : model_.tree()) {
        if (node.expandable) (*tree_).set_expanded(node.id, expanded);
    }
}

void DemoRoot::refresh_status() {
    status_text_.clear();
    append_count(status_text_, order_.size());
    status_text_.append(" of ");
    append_count(status_text_, model_.rows().size());
    status_text_.append(" rows shown; ");
    append_count(status_text_, counters_.advances);
    status_text_.append(" advances; ");
    append_count(status_text_, counters_.cell_plots_painted);
    status_text_.append(" cell plots painted");
    (*status_).text = status_text_;
    ++counters_.status_updates;
}

// -- cell painting ---------------------------------------------------------------------------

void DemoRoot::on_draw_cell(gf::ItemContentPaint& item) {
    if (item.index >= order_.size()) {
        item.draw_default = true;
        return;
    }
    const TableRow& row = model_.rows()[order_[item.index]];
    if (item.column == static_cast<std::size_t>(TableColumn::history)) {
        paint_history(item, row);
    } else if (item.column == static_cast<std::size_t>(TableColumn::value)) {
        paint_value(item, row);
    } else {
        item.draw_default = true;
    }
}

void DemoRoot::paint_history(gf::ItemContentPaint& item, const TableRow& row) {
    const gf::Rect area{item.content.x + 2.0, item.content.y + 3.0, std::max(0.0, item.content.width - 4.0),
                        std::max(0.0, item.content.height - 6.0)};
    if (area.width <= 0.0 || area.height <= 0.0) return;
    cell_points_.clear();
    const double step = area.width / static_cast<double>(history_length - 1U);
    for (std::size_t sample = 0U; sample < history_length; ++sample) {
        const double x = area.x + step * static_cast<double>(sample);
        const double y = area.y + (1.0 - row.history[sample]) * area.height;
        cell_points_.push_back(gf::Point{x, y});
    }
    gf::StrokeStyle stroke{};
    stroke.color = cell_line;
    stroke.width = 1.25;
    stroke.join = gf::StrokeJoin::round;
    item.painter.stroke_polyline(std::span<const gf::Point>(cell_points_), stroke);
    ++counters_.cell_plots_painted;
    counters_.cell_plot_points += history_length;
}

void DemoRoot::paint_value(gf::ItemContentPaint& item, const TableRow& row) {
    const gf::Rect track{item.content.x + 2.0, item.content.y + 4.0, std::max(0.0, item.content.width - 4.0),
                         std::max(0.0, item.content.height - 8.0)};
    if (track.width <= 0.0 || track.height <= 0.0) return;
    item.painter.fill_rect(track, cell_bar_track);
    const double share = std::clamp(row.value / 100.0, 0.0, 1.0);
    item.painter.fill_rect(gf::Rect{track.x, track.y, track.width * share, track.height}, cell_bar);
    const std::string text = format_number("%.1f", row.value);
    item.painter.draw_text_utf8(gf::Point{track.x + 4.0, track.y + track.height - 3.0}, text, item.font, cell_text);
    ++counters_.cell_bars_painted;
}

// -- handlers --------------------------------------------------------------------------------

void DemoRoot::on_page_changed(const gf::TabSelectionChange& change) {
    static_cast<void>(change);
    ++counters_.page_switches;
    refresh_status();
}

void DemoRoot::on_counter_clicked(gf::ButtonBase& button) {
    static_cast<void>(button);
    ++counters_.button_clicks;
    std::string text("Clicked ");
    append_count(text, counters_.button_clicks);
    text.append(counters_.button_clicks == 1U ? " time" : " times");
    (*widget_readout_).text = text;
}

void DemoRoot::on_advance_clicked(gf::ButtonBase& button) {
    static_cast<void>(button);
    advance();
}

void DemoRoot::on_expand_clicked(gf::ButtonBase& button) {
    static_cast<void>(button);
    expand_all(true);
}

void DemoRoot::on_collapse_clicked(gf::ButtonBase& button) {
    static_cast<void>(button);
    expand_all(false);
}

void DemoRoot::on_apply_clicked(gf::ButtonBase& button) {
    static_cast<void>(button);
    const std::string sheet = (*sheet_box_).text.read();
    static_cast<void>(apply_style_sheet(sheet));
}

void DemoRoot::on_check_changed(bool checked) {
    ++counters_.check_changes;
    (*widget_readout_).text = std::string(checked ? "Check box on" : "Check box off");
}

void DemoRoot::on_radio_changed(const std::size_t& index, bool checked) {
    if (!checked) return;
    ++counters_.radio_changes;
    std::string text("Radio ");
    text.push_back(static_cast<char>('a' + static_cast<int>(index)));
    text.append(" chosen");
    (*widget_readout_).text = text;
}

void DemoRoot::on_toggle_changed(bool checked) {
    ++counters_.toggle_changes;
    (*widget_readout_).text = std::string(checked ? "Toggle on" : "Toggle off");
}

void DemoRoot::on_combo_changed(std::optional<std::size_t> index) {
    static_cast<void>(index);
    ++counters_.combo_changes;
}

void DemoRoot::on_list_changed(std::optional<std::size_t> index) {
    static_cast<void>(index);
    ++counters_.list_changes;
}

void DemoRoot::on_text_changed(const std::string& text) {
    static_cast<void>(text);
    ++counters_.text_changes;
}

void DemoRoot::on_slider_changed(double value) {
    ++counters_.slider_changes;
    for (const std::shared_ptr<gf::ProgressBar>& bar : progress_) (*bar).value = value;
}

void DemoRoot::on_effect_chosen(std::optional<std::size_t> index) {
    if (!index.has_value()) return;
    set_effect(*index);
}

void DemoRoot::on_tree_selected(const gf::TreeSelectionChange& change) {
    ++counters_.tree_selections;
    (*tree_readout_).text = "Selected " + change.current_id;
}

void DemoRoot::on_tree_expanded(const gf::TreeExpansionChange& change) {
    static_cast<void>(change);
    ++counters_.tree_expansions;
}

void DemoRoot::on_filter_changed(const std::string& text) {
    set_filter(text);
    refresh_status();
}

void DemoRoot::on_sort_chosen(std::optional<std::size_t> index) {
    if (!index.has_value() || *index >= column_order.size()) return;
    set_sort(column_order[*index], sort_ascending_);
}

void DemoRoot::on_descending_changed(bool checked) {
    set_sort(sort_column_, !checked);
}

void DemoRoot::on_value_committed(double value) {
    ++counters_.value_commits;
    (*value_readout_).text = format_number("%.4f", value);
}

void DemoRoot::on_vector_changed(const Vector3& value) {
    ++counters_.vector_changes;
    char text[96]{};
    const int written = std::snprintf(text, sizeof(text), "(%.3f, %.3f, %.3f)", value.x, value.y, value.z);
    if (written > 0) (*value_readout_).text = std::string(text, static_cast<std::size_t>(written));
}

void DemoRoot::on_color_field_changed(const Hsva& colour) {
    if (updating_colour_) return;
    ++counters_.colour_changes;
    updating_colour_ = true;
    (*color_editor_).value = hsv_color(colour);
    updating_colour_ = false;
}

void DemoRoot::on_color_text_changed(gf::Color colour) {
    if (updating_colour_) return;
    ++counters_.colour_text_changes;
    updating_colour_ = true;
    (*color_field_).set_color(rgb_to_hsv(from_color(colour)));
    updating_colour_ = false;
}

void DemoRoot::on_section_toggled(int index, bool expanded) {
    static_cast<void>(index);
    static_cast<void>(expanded);
    ++counters_.section_toggles;
}

void DemoRoot::on_accent_changed(double value) {
    if (updating_style_) return;
    StyleChoice choice = style_;
    choice.accent_hue = std::min(value, 0.999999);
    set_style(choice);
}

void DemoRoot::on_radius_changed(double value) {
    if (updating_style_) return;
    StyleChoice choice = style_;
    choice.corner_radius = value;
    set_style(choice);
}

void DemoRoot::on_dark_changed(bool checked) {
    if (updating_style_) return;
    StyleChoice choice = style_;
    choice.dark = checked;
    set_style(choice);
}

// -- layout ----------------------------------------------------------------------------------

gf::Size DemoRoot::measure(gf::Size available) {
    return available;
}

void DemoRoot::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    const double tabs_height = std::max(0.0, final_bounds.height - status_height);
    set_child_layout(tabs_, gf::Rect{0.0, 0.0, final_bounds.width, tabs_height});
    set_child_layout(status_, gf::Rect{6.0, tabs_height, std::max(0.0, final_bounds.width - 12.0), status_height});
}

} // namespace imgui_class
