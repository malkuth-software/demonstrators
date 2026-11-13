# Node graph workloads

`node-graph --measure WORKLOAD [--nodes N] [--links-per-node L] [--seed S]
[--output-dir DIR] [--font-dir DIR] [--width W] [--height H]` runs one
workload headless: a window holding the app's root, attached to GUI.Forms'
offscreen raster with the bundled fonts, driven by dispatched pointer and
key events. The graph is generated from the seed first (the generator is
part of the record). The record goes to
`DIR/dev/measurements/<host>/<workload>-<seed as 16 hex digits>.json`, and
the program exits 1 when an invariant failed.

Each record holds: the inputs, the generator's report, every counter
(NodeCanvas's statistics, its DragTracker's and its two spatial indexes',
the GraphCanvas subclass's, the document, evaluator and root), the graph's
sizes and undo log, the window's public metrics, GUI.Forms' counted
allocations (process-wide, for the workload's `AllocationScope`, per frame
and per named phase; never the process total), per-frame counts (nodes and
links painted, paint passes, controls painted, damage rectangles, partial
and full-window paints, requested and painted damage area, the capture
interval) and the invariants with their outcome.

| Workload | Procedure | Invariants |
|---|---|---|
| `load` | Frame all, two frames | every generated node is in the graph; nodes painted |
| `pan` | Zoom 4x, then 120 frames panning 8,3 view units each | no spatial-index rebuild while panning; the phase's allocations recorded |
| `zoom` | 90 frames zooming in by 1.06, 180 out | levels of detail change |
| `drag` | Zoom 4x; drag up to ten visible nodes by their captions | one undo step per drag; one committed move per drag |
| `magnet` | Zoom 4x; up to ten link drags from a visible output, released 10 units off a compatible free input; then one capture pulled out to 20 units and one to about 57 | every near release connected; the capture held inside the 24-unit release radius connects; the one pulled past it connects nothing and asks for the palette |
| `marquee-knife` | Zoom 2x; marquee from the first empty point to 70 % of the window; knife (Ctrl + secondary) across the middle | the marquee selected nodes; the knife cut every link the canvas's `links_crossing` listed and nothing else (no independent oracle: GAPS.md N11) |
| `edit-host` | Open and cancel the edit host on 40 nodes, then commit changed titles on 10 | cancels leave the document byte-identical; at most one host per kind; one undo step per commit |
| `roundtrip` | Save; 1,000 seeded edits; undo all; save | the two saves are byte-identical |
| `idle` | Two frames, then 60 unchanged frames | no index rebuild; no item look resolved again; no evaluation; no damage requested; the phase's allocations recorded |

The spec's sizes (N of 1,000, 5,000 and 20,000) are set with `--nodes`.
Frame-interval budgets, idle wakes and the time-driven Plot case need a
windowed run on a host; the headless intervals are capture costs, not
presentation intervals.

To write a stress graph for opening in the window:

    node-graph --generate 5000 --links-per-node 2 --seed 1 --output stress-5000.graph
    node-graph stress-5000.graph
