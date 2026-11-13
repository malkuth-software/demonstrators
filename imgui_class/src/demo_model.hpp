#pragma once

// The demo's data, apart from any control: a seeded table of rows (name,
// category, value, flag and a 32-sample history drawn as a plot in its
// cell), a seeded tree, and the operations the Tables page offers
// (filter, sort, advance the histories). Pure and deterministic for a
// seed; no GUI.Forms type except none.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace imgui_class {

inline constexpr std::size_t history_length = 32U;

struct TableRow final {
    std::string name{};
    std::string category{};
    double value{};
    bool enabled{};
    // Oldest first; values in [0, 1].
    std::array<double, history_length> history{};
};

enum class TableColumn : std::uint8_t { name, category, value, enabled, history };
inline constexpr std::size_t table_column_count = 5U;

struct TreeNode final {
    std::string id{};
    std::string text{};
    std::size_t depth{};
    bool expandable{};
};

class DemoModel final {
public:
    // `rows` table rows and a tree of `branches` top-level branches, each
    // with up to three levels below, from `seed`. Allocates.
    DemoModel(std::size_t rows, std::size_t branches, std::uint64_t seed);

    [[nodiscard]] std::span<const TableRow> rows() const& noexcept { return rows_; }
    std::span<const TableRow> rows() const&& = delete;
    [[nodiscard]] std::span<const TreeNode> tree() const& noexcept { return tree_; }
    std::span<const TreeNode> tree() const&& = delete;

    // Writes into `out` (cleared first; reserve it at rows().size()) the
    // indices of rows whose name or category contains `needle`, ignoring
    // ASCII case; an empty needle keeps every row. In row order.
    void filter(std::string_view needle, std::vector<std::size_t>& out) const;
    // Sorts `order` (row indices) by `column`, ascending or descending,
    // ties broken by index so the order is total. History sorts by the
    // newest sample.
    void sort(std::vector<std::size_t>& order, TableColumn column, bool ascending) const;
    // Shifts every history one sample left and appends a new sample
    // (a bounded random walk), and moves `value` to the newest sample
    // times 100. Returns the number of rows changed.
    std::size_t advance();
    [[nodiscard]] std::uint64_t advances() const noexcept { return advances_; }

private:
    std::vector<TableRow> rows_{};
    std::vector<TreeNode> tree_{};
    std::uint64_t state_{};
    std::uint64_t advances_{};
};

// Whether `text` contains `needle`, ignoring ASCII case.
[[nodiscard]] bool contains_ignoring_case(std::string_view text, std::string_view needle) noexcept;

} // namespace imgui_class
