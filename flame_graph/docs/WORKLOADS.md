# Flame graph workloads

`flame-graph --measure WORKLOAD [--threads N] [--spans N] [--names N]
[--seed S] [--output-dir DIR] [--font-dir DIR] [--width W] [--height H]`
runs one workload headless: a window holding the viewer's root over a
generated trace (or `--trace FILE`), attached to GUI.Forms' offscreen
raster with the bundled fonts, driven by public calls and dispatched
pointer and key events. Nothing runs on a clock, so a record repeats for a
seed. The record goes to `DIR/dev/measurements/flame_graph/<host>/<workload>.json`,
and the program exits 1 when an invariant failed.

Each record holds the inputs; every counter (the root's, the model's and
its name table's, the last load report, the search summary, the flame
view's, the timeline's, the table's and VirtualRowsView's); the window's
`MetricsSnapshot` and GUI.Forms' counted allocations; the counted
allocations of each phase (`phase_allocations`); then per frame: the
capture interval, the deltas of paint passes, controls painted, damage
rectangles, partial and full-window paints, requested and painted damage
area and counted allocations, and the views' own per-frame counts (flame
cells and visits, timeline bars, visits and rows).

Defaults: 4 threads of 20,000 spans over 400 names, seed 1, 1280 by 860.

| Workload | Procedure | Invariants |
|---|---|---|
| `load` | Write the generated trace as Chrome JSON; load it through the root; compare with the trace generated straight into a model | loaded as Chrome JSON; no malformed or other events; every X event a span; same spans, nodes, total, functions, depth; threads named; both views painted |
| `flame-zoom` | Click the heaviest node four levels down; double click it; 20 wheel notches at the plot's centre; Home | the click selects it and the table selects its function; the double click zooms to its extent; the wheel narrows the range; every frame's cells within visible rows times width; Home shows everything |
| `flame-pan` | 12 wheel notches; drag 240 right, then 120 left; 4 Shift+wheel notches | the range moved, kept its extent and stayed inside the trace; view changes raised; rows scrolled |
| `timeline-zoom` | 30 wheel notches at the plot's centre, a frame each; Home | the range narrowed; every frame's visits within rows times twice the width; spans merged; Home restores the range |
| `timeline-pan` | 16 wheel notches; drag 300 left; 6 Shift+wheel notches; click the first lane's top row | the range moved; lanes scrolled (or all fit); a click on a span selects its node |
| `search` | "render", "STEP_1", "gc::step_2", "no such frame", "" through the search box | each summary agrees with an independent count (lower-cased copies, parent chains); matched weight within the total; the table shows exactly the matching functions; the empty query clears |
| `table-scroll` | Scroll the table top to bottom in 20 steps; click the Calls header | rows start sorted by total; each frame paints only the visible rows; the click sorts by calls, descending |
| `mode-switch` | Six clicks on the flame/icicle button | six switches, back to the icicle; both orientations drew cells |
| `idle` | Two frames, then 60 unchanged frames | no damage requested; the flame view did not repaint |

Frame-interval budgets need a windowed run on a host; the headless
intervals are capture costs.
