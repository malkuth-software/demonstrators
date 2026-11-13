// The demo model: the same seed gives the same rows and tree, the filter
// keeps exactly the rows that match (ignoring case), every sort is a
// total order over its column, and advancing keeps the histories' shape
// and bounds. Headless.

#include "../src/demo_model.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <numeric>
#include <set>
#include <string>
#include <vector>

using imgui_class::DemoModel;
using imgui_class::TableColumn;
using imgui_class::TableRow;
using imgui_class_tests::require;

namespace {

void test_determinism() {
    const DemoModel first(500U, 20U, 7U);
    const DemoModel second(500U, 20U, 7U);
    require(first.rows().size() == 500U, "the model has its rows");
    for (std::size_t index = 0U; index < first.rows().size(); ++index) {
        const TableRow& a = first.rows()[index];
        const TableRow& b = second.rows()[index];
        require(a.name == b.name && a.category == b.category && a.value == b.value && a.history == b.history,
                "the same seed gives the same rows");
    }
    require(first.tree().size() == second.tree().size(), "the same seed gives the same tree");
    const DemoModel other(500U, 20U, 8U);
    require(other.rows()[0].name != first.rows()[0].name || other.rows()[1].name != first.rows()[1].name,
            "another seed gives other rows");
}

void test_tree() {
    const DemoModel model(10U, 25U, 3U);
    std::set<std::string> ids{};
    std::size_t roots = 0U;
    std::size_t previous_depth = 0U;
    for (std::size_t index = 0U; index < model.tree().size(); ++index) {
        const imgui_class::TreeNode& node = model.tree()[index];
        require(ids.insert(node.id).second, "tree ids are unique");
        if (node.depth == 0U) ++roots;
        require(index == 0U ? node.depth == 0U : node.depth <= previous_depth + 1U, "depth grows one level at a time");
        previous_depth = node.depth;
    }
    require(roots == 25U, "one root per branch");
}

void test_filter() {
    const DemoModel model(800U, 4U, 11U);
    std::vector<std::size_t> out{};
    out.reserve(model.rows().size());
    model.filter("", out);
    require(out.size() == model.rows().size(), "an empty needle keeps every row");
    model.filter("KA", out);
    std::size_t expected = 0U;
    for (const TableRow& row : model.rows()) {
        if (imgui_class::contains_ignoring_case(row.name, "ka") || imgui_class::contains_ignoring_case(row.category, "ka")) {
            ++expected;
        }
    }
    require(out.size() == expected, "the filter keeps exactly the matching rows");
    require(std::is_sorted(out.begin(), out.end()), "filtered rows keep row order");
    model.filter("alpha", out);
    for (const std::size_t index : out) {
        require(imgui_class::contains_ignoring_case(model.rows()[index].category, "Alpha") ||
                    imgui_class::contains_ignoring_case(model.rows()[index].name, "alpha"),
                "every kept row matches");
    }
    require(imgui_class::contains_ignoring_case("Hello", "LL"), "case is ignored");
    require(!imgui_class::contains_ignoring_case("He", "Hello"), "a longer needle never matches");
}

void test_sort() {
    const DemoModel model(600U, 2U, 5U);
    std::vector<std::size_t> order(model.rows().size());
    std::iota(order.begin(), order.end(), std::size_t{0U});
    model.sort(order, TableColumn::value, true);
    for (std::size_t index = 1U; index < order.size(); ++index) {
        require(model.rows()[order[index - 1U]].value <= model.rows()[order[index]].value, "ascending by value");
    }
    model.sort(order, TableColumn::name, false);
    for (std::size_t index = 1U; index < order.size(); ++index) {
        require(model.rows()[order[index - 1U]].name >= model.rows()[order[index]].name, "descending by name");
    }
    model.sort(order, TableColumn::enabled, true);
    std::vector<std::size_t> again = order;
    model.sort(again, TableColumn::enabled, true);
    require(order == again, "ties break by index, so sorting is repeatable");
    std::vector<std::size_t> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t index = 0U; index < sorted.size(); ++index) require(sorted[index] == index, "a sort is a permutation");
}

void test_advance() {
    DemoModel model(100U, 2U, 9U);
    const TableRow before = model.rows()[3];
    require(model.advance() == 100U, "every row advances");
    const TableRow& after = model.rows()[3];
    for (std::size_t sample = 0U; sample + 1U < imgui_class::history_length; ++sample) {
        require(after.history[sample] == before.history[sample + 1U], "the history shifts one sample left");
    }
    for (std::size_t step = 0U; step < 500U; ++step) static_cast<void>(model.advance());
    for (const TableRow& row : model.rows()) {
        for (const double sample : row.history) require(sample >= 0.0 && sample <= 1.0, "samples stay in [0, 1]");
        require(row.value == row.history.back() * 100.0, "the value follows the newest sample");
    }
    require(model.advances() == 501U, "advances are counted");
}

} // namespace

int main() {
    try {
        test_determinism();
        test_tree();
        test_filter();
        test_sort();
        test_advance();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "demo_model_tests: %s\n", failure.what());
        return 1;
    }
    std::puts("demo_model_tests: passed");
    return 0;
}
