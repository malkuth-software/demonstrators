#include "details_table.hpp"

#include "profile_ink.hpp"

#include <gui_forms/delegate.hpp>
#include <gui_forms/types/painter/painter.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace flame_graph {

namespace gf = gui_forms;

namespace {

constexpr std::array<std::string_view, table_column_count> column_titles{"Function", "Total", "Self", "Calls",
                                                                        "Total %"};

gf::FontSpec cell_font() {
    gf::FontSpec font{};
    font.size = 12.0;
    return font;
}

} // namespace

// Display order of function rows for the chosen column and direction;
// equal keys fall back to the name's text, then the index, so the order is
// total and repeatable.
struct DetailsTable::FunctionOrder final {
    const TraceModel* model{};
    TableColumn column{TableColumn::total};
    bool descending{true};

    bool operator()(std::uint32_t left, std::uint32_t right) const noexcept {
        const FunctionRow& a = (*model).functions()[left];
        const FunctionRow& b = (*model).functions()[right];
        std::int64_t key_a = 0;
        std::int64_t key_b = 0;
        if (column == TableColumn::self) {
            key_a = a.self;
            key_b = b.self;
        } else if (column == TableColumn::calls) {
            key_a = static_cast<std::int64_t>(a.calls);
            key_b = static_cast<std::int64_t>(b.calls);
        } else if (column == TableColumn::total || column == TableColumn::share) {
            key_a = a.total;
            key_b = b.total;
        }
        if (key_a != key_b) return descending ? key_a > key_b : key_a < key_b;
        const std::string_view text_a = (*model).names().text(a.name);
        const std::string_view text_b = (*model).names().text(b.name);
        if (text_a != text_b) {
            // The name column sorts by text in the chosen direction.
            const bool by_name_descending = column == TableColumn::name && descending;
            return by_name_descending ? text_a > text_b : text_a < text_b;
        }
        return left < right;
    }
};

DetailsTable::DetailsTable(gf::StableId stable_id) : gf::Control(std::move(stable_id)) {
    label_.reserve(256U);
}

DetailsTable::~DetailsTable() {
    // The view may outlive this control (a shared child): its targets name
    // this object.
    if (rows_ != nullptr) (*rows_).set_row_callbacks(gf::VirtualRowsCallbacks{});
}

void DetailsTable::initialize_control_tree() {
    rows_ = gf::make_control<gf::VirtualRowsView>(gf::StableId("flame-graph.table.rows"));
    (*rows_).set_estimated_row_height(row_height);
    gf::VirtualRowsCallbacks callbacks{};
    callbacks.measure = gf::Delegate<gf::VirtualRowMeasure&>::bind<DetailsTable, &DetailsTable::measure_row>(*this);
    callbacks.paint = gf::Delegate<gf::VirtualRowPaint&>::bind<DetailsTable, &DetailsTable::paint_row>(*this);
    callbacks.describe =
        gf::Delegate<gf::VirtualRowDescription&>::bind<DetailsTable, &DetailsTable::describe_row>(*this);
    (*rows_).set_row_callbacks(callbacks);
    add_child(rows_);
    gf::on((*rows_).selection_changed(), *this, &DetailsTable::on_rows_selection);
    gf::on((*rows_).row_activated(), *this, &DetailsTable::on_row_activated);
}

void DetailsTable::set_inputs(const ProfileInputs& inputs) noexcept {
    inputs_ = inputs;
    invalidate(gf::Dirty::paint);
}

void DetailsTable::model_changed() {
    ellipsis_width_ = -1.0;
    if (inputs_.ready()) {
        const TraceModel& model = *inputs_.model;
        row_functions_.reserve(model.functions().size());
        function_rows_.reserve(model.functions().size());
        name_widths_.assign(model.names().size(), -1.0F);
    }
    rebuild_rows();
}

void DetailsTable::search_changed() {
    rebuild_rows();
}

void DetailsTable::selection_changed_outside() noexcept {
    invalidate(gf::Dirty::paint);
}

void DetailsTable::set_sort(TableColumn column, bool descending) {
    sort_column_ = column;
    sort_descending_ = descending;
    rebuild_rows();
}

void DetailsTable::rebuild_rows() {
    row_functions_.clear();
    function_rows_.clear();
    ++statistics_.rebuilds;
    if (inputs_.ready()) {
        const TraceModel& model = *inputs_.model;
        const SearchFlags& search = *inputs_.search;
        const std::span<const FunctionRow> functions = model.functions();
        for (std::size_t index = 0U; index < functions.size(); ++index) {
            if (search.active() && !search.matches(functions[index].name)) continue;
            row_functions_.push_back(static_cast<std::uint32_t>(index));
        }
        std::sort(row_functions_.begin(), row_functions_.end(), FunctionOrder{&model, sort_column_, sort_descending_});
        ++statistics_.sorts;
        function_rows_.assign(functions.size(), 0U);
        for (std::size_t row = 0U; row < row_functions_.size(); ++row) {
            function_rows_[row_functions_[row]] = static_cast<std::uint32_t>(row + 1U);
        }
    }
    selecting_ = true;
    (*rows_).reset_rows(row_functions_.size());
    selecting_ = false;
    invalidate(gf::Dirty::paint);
}

std::optional<std::size_t> DetailsTable::function_at_row(std::size_t row) const noexcept {
    if (row >= row_functions_.size()) return std::nullopt;
    return static_cast<std::size_t>(row_functions_[row]);
}

std::optional<std::size_t> DetailsTable::row_of_function(std::size_t function) const noexcept {
    if (function >= function_rows_.size() || function_rows_[function] == 0U) return std::nullopt;
    return static_cast<std::size_t>(function_rows_[function] - 1U);
}

void DetailsTable::select_function(std::optional<std::size_t> function) {
    std::optional<gf::VirtualRowIndex> row{};
    if (function.has_value()) {
        const std::optional<std::size_t> found = row_of_function(*function);
        if (found.has_value()) row = gf::VirtualRowIndex{*found};
    }
    selecting_ = true;
    (*rows_).set_selected_row(row);
    if (row.has_value()) (*rows_).scroll_to_row(*row, gf::VirtualRowAlignment::nearest);
    selecting_ = false;
}

// -- columns ------------------------------------------------------------------------------------

DetailsTable::ColumnEdges DetailsTable::column_edges(TableColumn column, double width) const noexcept {
    // From the right: share, calls, self, total; the name takes the rest.
    const double share_left = width - share_width;
    const double calls_left = share_left - calls_width;
    const double self_left = calls_left - self_width;
    const double total_left = self_left - total_width;
    switch (column) {
    case TableColumn::share:
        return ColumnEdges{share_left, width};
    case TableColumn::calls:
        return ColumnEdges{calls_left, share_left};
    case TableColumn::self:
        return ColumnEdges{self_left, calls_left};
    case TableColumn::total:
        return ColumnEdges{total_left, self_left};
    case TableColumn::name:
    default:
        return ColumnEdges{0.0, std::max(0.0, total_left)};
    }
}

std::optional<TableColumn> DetailsTable::column_at(double x) const noexcept {
    const double width = client_rectangle().width;
    for (std::size_t index = 0U; index < table_column_count; ++index) {
        const TableColumn column = static_cast<TableColumn>(index);
        const ColumnEdges edges = column_edges(column, width);
        if (x >= edges.left && x < edges.right) return column;
    }
    return std::nullopt;
}

// -- layout and painting ------------------------------------------------------------------------

gf::Size DetailsTable::measure(gf::Size available) {
    return available;
}

void DetailsTable::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    const double height = std::max(0.0, final_bounds.height - header_height);
    set_child_layout(rows_, gf::Rect{0.0, header_height, final_bounds.width, height});
}

void DetailsTable::on_paint(gf::Painter& painter, gf::Rect local_damage) {
    static_cast<void>(local_damage);
    const gf::Rect bounds = client_rectangle();
    painter.fill_rect(gf::Rect{0.0, 0.0, bounds.width, header_height}, lane_header_fill);
    gf::FontSpec font = cell_font();
    font.weight = 600U;
    for (std::size_t index = 0U; index < table_column_count; ++index) {
        const TableColumn column = static_cast<TableColumn>(index);
        const ColumnEdges edges = column_edges(column, bounds.width);
        label_.assign(column_titles[index]);
        if (column == sort_column_) label_.append(sort_descending_ ? " \xe2\x96\xbc" : " \xe2\x96\xb2");
        const gf::Size size = painter.measure_text_utf8(label_, font);
        const double x = column == TableColumn::name ? edges.left + cell_padding : edges.right - cell_padding - size.width;
        painter.draw_text_utf8(gf::Point{x, 15.0}, label_, font, label_ink);
        if (index > 0U) painter.fill_rect(gf::Rect{edges.left, 3.0, 1.0, header_height - 6.0}, rule_ink);
    }
    painter.fill_rect(gf::Rect{0.0, header_height - 1.0, bounds.width, 1.0}, rule_ink);
}

void DetailsTable::measure_row(gf::VirtualRowMeasure& row) {
    row.height = row_height;
}

void DetailsTable::paint_row(gf::VirtualRowPaint& row) {
    if (row.painter == nullptr || !inputs_.ready() || row.row.value >= row_functions_.size()) return;
    gf::Painter& painter = *row.painter;
    const TraceModel& model = *inputs_.model;
    const FunctionRow& function = model.functions()[row_functions_[row.row.value]];
    const gf::FontSpec font = cell_font();
    const double baseline = row.bounds.y + row.bounds.height * 0.5 + 4.0;
    const double width = row.bounds.width;
    ++statistics_.rows_painted;
    // The name, cut to its column.
    const ColumnEdges name_edges = column_edges(TableColumn::name, width);
    const std::string_view name = model.names().text(function.name);
    if (ellipsis_width_ < 0.0) ellipsis_width_ = painter.measure_text_utf8(label_ellipsis, font).width;
    if (function.name.value < name_widths_.size()) {
        float& measured = name_widths_[function.name.value];
        if (measured < 0.0F) measured = static_cast<float>(painter.measure_text_utf8(name, font).width);
        const double available = name_edges.right - name_edges.left - 2.0 * cell_padding;
        const gf::Point origin{row.bounds.x + name_edges.left + cell_padding, baseline};
        const gf::Color ink = (*inputs_.search).matches(function.name) ? search_fill : label_ink;
        if (static_cast<double>(measured) <= available) {
            painter.draw_text_utf8(origin, name, font, ink);
        } else if (cut_label(name, static_cast<double>(measured), available - font.size * 0.5, ellipsis_width_, label_)) {
            painter.draw_text_utf8(origin, label_, font, ink);
        }
    }
    // The numbers, right aligned in their columns.
    std::array<FormattedNumber, 4> cells{};
    format_weight(function.total, model.unit(), cells[0]);
    format_weight(function.self, model.unit(), cells[1]);
    format_count(function.calls, cells[2]);
    format_share(function.total, model.total_weight(), cells[3]);
    const std::array<TableColumn, 4> columns{TableColumn::total, TableColumn::self, TableColumn::calls,
                                             TableColumn::share};
    for (std::size_t index = 0U; index < cells.size(); ++index) {
        const ColumnEdges edges = column_edges(columns[index], width);
        const std::string_view text = cells[index].text();
        const gf::Size size = painter.measure_text_utf8(text, font);
        painter.draw_text_utf8(gf::Point{row.bounds.x + edges.right - cell_padding - size.width, baseline}, text, font,
                               label_ink);
    }
}

void DetailsTable::describe_row(gf::VirtualRowDescription& row) {
    if (!inputs_.ready() || row.row.value >= row_functions_.size()) return;
    const TraceModel& model = *inputs_.model;
    const FunctionRow& function = model.functions()[row_functions_[row.row.value]];
    row.name.append(model.names().text(function.name));
    FormattedNumber total{};
    format_weight(function.total, model.unit(), total);
    FormattedNumber self{};
    format_weight(function.self, model.unit(), self);
    row.value.append("total ");
    row.value.append(total.text());
    row.value.append(", self ");
    row.value.append(self.text());
}

// -- input --------------------------------------------------------------------------------------

void DetailsTable::on_mouse_down(gf::MouseEventArgs& arguments) {
    if (arguments.button != gf::PointerButton::primary || arguments.location.y > header_height) return;
    const std::optional<TableColumn> column = column_at(arguments.location.x);
    if (!column.has_value()) return;
    ++statistics_.header_clicks;
    // A second click on the sorted column turns the direction; a new column
    // starts descending (largest first), the name column ascending.
    const bool descending = *column == sort_column_ ? !sort_descending_ : *column != TableColumn::name;
    set_sort(*column, descending);
    arguments.handled = true;
}

void DetailsTable::on_rows_selection(const gf::VirtualRowSelection& selection) {
    if (selecting_ || !inputs_.ready() || !selection.current.has_value()) return;
    const std::optional<std::size_t> function = function_at_row((*selection.current).value);
    if (!function.has_value()) return;
    ++statistics_.picks;
    FunctionPick pick{};
    pick.function = *function;
    pick.name = (*inputs_.model).functions()[*function].name;
    function_picked_.emit(pick);
}

void DetailsTable::on_row_activated(gf::VirtualRowIndex row) {
    if (!inputs_.ready()) return;
    const std::optional<std::size_t> function = function_at_row(row.value);
    if (!function.has_value()) return;
    ++statistics_.activations;
    FunctionPick pick{};
    pick.function = *function;
    pick.name = (*inputs_.model).functions()[*function].name;
    function_activated_.emit(pick);
}

} // namespace flame_graph
