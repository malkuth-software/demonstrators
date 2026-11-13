#include "demo_model.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <cmath>

namespace imgui_class {

namespace {

constexpr std::array<std::string_view, 6> categories{"Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta"};
constexpr std::array<std::string_view, 8> syllables{"ka", "ri", "mo", "ten", "su", "lo", "vi", "dar"};

// SplitMix64, one step.
std::uint64_t next_random(std::uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t mixed = state;
    mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
    const std::uint64_t result = mixed ^ (mixed >> 31U);
    return result;
}

double next_unit(std::uint64_t& state) noexcept {
    const double result = static_cast<double>(next_random(state) >> 11U) * (1.0 / 9007199254740992.0);
    return result;
}

char lower(char character) noexcept {
    if (character >= 'A' && character <= 'Z') return static_cast<char>(character - 'A' + 'a');
    return character;
}

std::string make_name(std::uint64_t& state, std::size_t index) {
    std::string name{};
    name.reserve(16U);
    const std::size_t parts = 2U + static_cast<std::size_t>(next_random(state) % 2U);
    for (std::size_t part = 0U; part < parts; ++part) {
        name.append(syllables[next_random(state) % syllables.size()]);
    }
    name.front() = static_cast<char>(name.front() - 'a' + 'A');
    name.push_back('-');
    name.append(std::to_string(index));
    return name;
}

double next_sample(std::uint64_t& state, double previous) noexcept {
    const double step = (next_unit(state) - 0.5) * 0.2;
    const double sample = std::clamp(previous + step, 0.0, 1.0);
    return sample;
}

// The comparison of two rows by one column; < 0, 0 or > 0.
int compare_rows(const TableRow& left, const TableRow& right, TableColumn column) noexcept {
    switch (column) {
    case TableColumn::name:
        return left.name.compare(right.name);
    case TableColumn::category:
        return left.category.compare(right.category);
    case TableColumn::value:
        if (left.value < right.value) return -1;
        return left.value > right.value ? 1 : 0;
    case TableColumn::enabled:
        if (left.enabled == right.enabled) return 0;
        return left.enabled ? 1 : -1;
    case TableColumn::history:
    default: {
        const double a = left.history.back();
        const double b = right.history.back();
        if (a < b) return -1;
        return a > b ? 1 : 0;
    }
    }
}

// The named ordering predicate for std::sort: by the column, then index.
struct RowOrder final {
    const std::vector<TableRow>* rows{};
    TableColumn column{};
    bool ascending{};

    bool operator()(std::size_t left, std::size_t right) const noexcept {
        const int compared = compare_rows((*rows)[left], (*rows)[right], column);
        if (compared != 0) return ascending ? compared < 0 : compared > 0;
        return left < right;
    }
};

} // namespace

bool contains_ignoring_case(std::string_view text, std::string_view needle) noexcept {
    if (needle.empty()) return true;
    if (needle.size() > text.size()) return false;
    const std::size_t last = text.size() - needle.size();
    for (std::size_t start = 0U; start <= last; ++start) {
        std::size_t matched = 0U;
        while (matched < needle.size() && lower(text[start + matched]) == lower(needle[matched])) ++matched;
        if (matched == needle.size()) return true;
    }
    return false;
}

DemoModel::DemoModel(std::size_t rows, std::size_t branches, std::uint64_t seed) : state_(seed) {
    rows_.reserve(rows);
    for (std::size_t index = 0U; index < rows; ++index) {
        TableRow row{};
        row.name = make_name(state_, index);
        row.category = std::string(categories[next_random(state_) % categories.size()]);
        row.enabled = (next_random(state_) & 1U) != 0U;
        double sample = next_unit(state_);
        for (double& entry : row.history) {
            sample = next_sample(state_, sample);
            entry = sample;
        }
        row.value = row.history.back() * 100.0;
        rows_.push_back(std::move(row));
    }
    // Each branch: up to four children, each with up to three leaves,
    // every third child a deeper folder.
    tree_.reserve(branches * 20U);
    for (std::size_t branch = 0U; branch < branches; ++branch) {
        const std::string branch_id = "node." + std::to_string(branch);
        tree_.push_back(TreeNode{branch_id, "Branch " + std::to_string(branch + 1U), 0U, true});
        const std::size_t children = 1U + static_cast<std::size_t>(next_random(state_) % 4U);
        for (std::size_t child = 0U; child < children; ++child) {
            const std::string child_id = branch_id + "." + std::to_string(child);
            const bool folder = child % 3U == 0U;
            tree_.push_back(TreeNode{child_id, (folder ? "Folder " : "Item ") + std::to_string(child + 1U), 1U, folder});
            if (!folder) continue;
            const std::size_t leaves = 1U + static_cast<std::size_t>(next_random(state_) % 3U);
            for (std::size_t leaf = 0U; leaf < leaves; ++leaf) {
                tree_.push_back(TreeNode{child_id + "." + std::to_string(leaf), "Leaf " + std::to_string(leaf + 1U), 2U, false});
            }
        }
    }
}

void DemoModel::filter(std::string_view needle, std::vector<std::size_t>& out) const {
    out.clear();
    for (std::size_t index = 0U; index < rows_.size(); ++index) {
        const TableRow& row = rows_[index];
        if (contains_ignoring_case(row.name, needle) || contains_ignoring_case(row.category, needle)) out.push_back(index);
    }
}

void DemoModel::sort(std::vector<std::size_t>& order, TableColumn column, bool ascending) const {
    const RowOrder predicate{&rows_, column, ascending};
    std::sort(order.begin(), order.end(), predicate);
}

std::size_t DemoModel::advance() {
    for (TableRow& row : rows_) {
        std::move(row.history.begin() + 1, row.history.end(), row.history.begin());
        const double previous = row.history[history_length - 2U];
        row.history.back() = next_sample(state_, previous);
        row.value = row.history.back() * 100.0;
    }
    ++advances_;
    return rows_.size();
}

} // namespace imgui_class
