# Node graph: where the public GUI.Forms API fell short

First written against the cp29 integration tree `a5081f7d6`, installed
headers only (`find_package(GUIForms 0.6)`), before any build. Numbers 1 to
19 are NODE_GRAPH_SPEC section 9; N-numbered entries are what the app found
besides. Each open entry says what the app needed, what it does instead,
the header concerned and the shape of the missing API. Entries marked
*unverified* depend on behaviour the headers describe but no run has
confirmed.

| Gap | Status |
|---|---|
| 1 multi-level grid | Closed: `select_grid_level`, `grid_pattern_at_level` (`types/paint_marks`) |
| 2 texture multiply under the grid | Open: not in the theme language; the app paints flat colours |
| 3 link strokes, halo, dashed flow | Partly: `stroke_polyline` with `StrokeStyle` dashes; halo is a second wider stroke; flow phase not used |
| 4 per-layer cached pictures | Open: the canvas repaints every layer each frame |
| 5 node shadows | Not attempted (no budget measured) |
| 6 text cached per node under zoom | Open: the app caches line records per node (N4) |
| 7 public spatial index | Open: the app's `SpatialIndex` (`src/canvas_geometry.hpp`) |
| 8 item-level hover, cursor, tooltip | Open |
| 9 drag engine internal | Closed: `DragTracker` (`drag/drag_tracker/drag_tracker.hpp`) |
| 10 bounded candidate providers | Partly: the app collects candidates (visible area + 200 units, nearest first, capped at `maximum_drag_targets`) |
| 11 autoscroll while dragging | Closed: `DragPlan::auto_scroll`, the canvas pans in `scroll_drag` (*unverified*) |
| 12 pointer lock for scrubbing | Open: value-bar scrubbing not built |
| 13 semantic relations | Open: links are named in port children's text only |
| 14 records as semantic items | Partly: nodes as virtual list items with port children; focus on a virtual item and spatial keyboard navigation are canvas work |
| 15 announcements from a custom control | Partly: drag announcements through `DragTracker::announce`; edits are not announced |
| 16 NodeGraph undo, serialization, conversions, frames | Partly: undo and serialization closed; conversions (N6) and frames (N7) open |
| 17 document parser and serializer | Closed: `NodeGraph::save` / `load`, `gui-forms-graph 1` |
| 18 router scale | Open, *unverified* at 20,000 nodes: the routed style routes the whole graph on each revision |
| 19 custom clipboard formats | Open: no copy and paste of graph fragments |

## Open, found by the app

**N1. No NodeCanvas control.** The app's `NodeCanvasView` (`src/node_canvas_view.*`,
`src/node_canvas_paint.cpp`) is a plain `Control` with its own view
transform, level of detail, hit testing, gestures over `DragTracker`, port
batching and semantics. Every graph editor would write the same. Shape: a
public canvas over `NodeGraph` with paint and hit-test hooks per record.

**N2. No theme roles for a consumer canvas.** Background variants, family
band colours, socket colours and lamp colours are app constants
(`palette_of`, `socket_colour`). Shape: theme parts for a canvas surface,
its grid, node bodies and captions, and a socket-type colour table.

**N3. Lamp writes advance the shared revision.** `set_port_lamp` (evaluation
state, not undone, not saved) moves `NodeGraph::revision()`, so caches keyed
on the revision (the spatial index, the evaluator's topology, the router's
`routed_revision`) rebuild after every evaluation that changed a lamp.
Shape: a separate geometry or structure revision, or lamps outside the
revision. The app writes lamps only when they differ.

**N4. No per-record text cache.** Titles are laid out with
`lay_out_paragraph` into `ParagraphBuffers` and their line records kept per
node slot, keyed by generation, title hash, font size and width
(`TitleLayout`). Shape: a cache keyed by a record identity that a control
can hold and drop.

**N5. No rollback of an open undo group.** Cancelling a gesture (Escape
during a drag) ends the group and undoes it, which leaves a redo step.
`GraphDocument::cancel_gesture` documents this. Shape:
`NodeGraph::abandon_undo_group()` restoring the state at `begin_undo_group`
without a redo step.

**N6. No conversion table in NodeGraph.** Socket types are numbers; which
pairs connect, and how lossily, is the app's 5x5 table (`src/socket_types.*`)
checked before `connect`. Shape: a per-graph conversion predicate.

**N7. No frames or groups in NodeGraph.** The generator skips the spec's
frames around every 64 nodes. Shape: frame records with member lists, saved
in the document.

**N8. No file dialog without host services.** Save (Ctrl+S) writes to the
path the document came from; with none, it is counted as a failure. Open is
from the command line.

**N9. Edit host placement.** The edit hosts are `TextBox` children of the
window's root, placed over the canvas by the root's `set_child_layout`. That
works, but a control cannot host an editing child over one of its own
records without its parent's cooperation.

## Risks to confirm in the first build and run

- R1 (*unverified*): `DragTracker` capture and the canvas's own
  `on_mouse_*` overrides together: the canvas calls `press` from
  `on_mouse_down` and forwards moves to `pointer`.
- R2 (*unverified*): `suppress_click` set in `on_mouse_up` after a planned
  drag suppresses the click event as the raiser contract says.
- R3 (*unverified*): enlisting the motion clock from `on_paint`
  (`enlist_live_sinks`) for time-driven visible sinks.
- R4 (*unverified*): `Painter` used as the `TextMetricsProvider` for
  `lay_out_paragraph` during paint.
- R5: interval timing for the idle and frame budgets needs a windowed run;
  the headless workloads record capture intervals only.
