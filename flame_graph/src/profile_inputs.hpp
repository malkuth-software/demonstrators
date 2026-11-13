#pragma once

// What the three views read and share: the loaded model, the per-name
// colours, the current search and the selection. ProfilerRoot owns all of
// them and hands each view borrowed pointers (ProfileInputs); a view never
// writes them. The root clears the views' inputs before it is destroyed,
// so a view that outlives it (shared children) reads nothing.
//
// The selection is a call-tree node (picked in the flame graph or the
// timeline) and a name (the node's, or a table row's). Views outline the
// selected node's cells and, more lightly, every cell of the selected name.
// Views raise node_picked only from user input; the root's own calls
// (set_selection, zoom_to, ...) raise nothing, so selection sync cannot
// loop.

#include "trace_model.hpp"

#include <gui_forms/types/paint_types/paint_types.hpp>

#include <optional>
#include <vector>

namespace flame_graph {

struct ProfileSelection final {
    std::optional<CallNodeIndex> node{};
    std::optional<NameId> name{};
};

struct ProfileInputs final {
    const TraceModel* model{};
    const std::vector<gui_forms::Color>* colors{};
    const SearchFlags* search{};
    const ProfileSelection* selection{};

    [[nodiscard]] bool ready() const noexcept {
        return model != nullptr && colors != nullptr && search != nullptr && selection != nullptr &&
               (*model).finished();
    }
};

// A node the user picked (a click on a cell or a span).
struct NodePick final {
    CallNodeIndex node{};
    NameId name{};
};

} // namespace flame_graph
