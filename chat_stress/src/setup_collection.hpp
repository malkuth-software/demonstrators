#pragma once

// The headless setup collection (`--measure setup`): plain data, no window
// on screen. For 100, 1,000,000 and 10,000,000 rows, three runs each:
// the time from handing the chat log its rows (constructing ChatRoot, whose
// VirtualRowsView::reset_rows takes the row count) through layout to the
// first frame captured through GUI.Forms' offscreen CPU raster, with the
// operation counts of that span. At 10,000,000 rows it then runs a scroll
// and jump sequence, samples the footprint, and observes the process's CPU
// time over 10 s with nothing scheduled. The record names the machine.
//
// Durations here are the setup times the owner asked for (2026-11-12) and
// are recorded as data, with the counts beside them.

#include "options.hpp"

#include <gui_forms/application.hpp>

namespace chat_stress {

// Returns the process exit code: 0 when the record was written.
[[nodiscard]] int collect_setup(const AppOptions& options, gui_forms::Application& application);

} // namespace chat_stress
