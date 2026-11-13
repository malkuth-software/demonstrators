# Node graph: where the public GUI.Forms API fell short

First written against the cp29 integration tree `a5081f7d6`; moved onto
cp31 (`b8e702fa6`), whose public `NodeCanvas` replaced the app's own canvas.
Installed headers only (`find_package(GUIForms 0.6)`), before any build.
Numbers 1 to 19 are NODE_GRAPH_SPEC section 9; N-numbered entries are what
the app found besides. Each open entry says what the app needed, what it
does instead, the header concerned and the shape of the missing API.
Entries marked *unverified* depend on behaviour the headers describe but no
run has confirmed.

| Gap | Status |
|---|---|
| 1 multi-level grid | Closed: `select_grid_level` (`types/paint_marks`), drawn by `paint_canvas_background` |
| 2 texture multiply under the grid | Closed in the theme: the canvas look's texture layers (`theme/canvas_look`), multiplied or screened at a clamped contrast |
| 3 link strokes, halo, dashed flow | Closed: `paint_canvas_link` (halo, dashes), flow marks on the motion clock (`set_link_flowing`); the app marks links from time-driven sources flowing |
| 4 per-layer cached pictures | Open: whether NodeCanvas keeps a recorded picture per layer is not public; a consumer control still cannot hold and replay one |
| 5 node shadows | Not attempted (no budget measured) |
| 6 text cached per node under zoom | Moved into GUI.Forms: NodeCanvas draws captions and port labels; whether it shapes them per frame is its business (*unverified*) |
| 7 public spatial index | Closed: `SpatialIndex` (`scene/spatial_index`); NodeCanvas keeps two |
| 8 item-level hover, cursor, tooltip | Partly: NodeCanvas has hover states per item (`:hover`); per-item cursors and tooltips are still open |
| 9 drag engine internal | Closed: `DragTracker`; NodeCanvas is its client |
| 10 bounded candidate providers | Closed: NodeCanvas collects the magnet's candidates (visible area plus `candidate_margin`, nearest first, capped) |
| 11 autoscroll while dragging | Closed in NodeCanvas (*unverified*) |
| 12 pointer lock for scrubbing | Open: the app's value bars are painted, not scrubbed |
| 13 semantic relations | Open: NodeCanvas names each input's source in its port text |
| 14 records as semantic items | Closed in NodeCanvas: visible and focused nodes as items, ports as children, the keyboard graph (*unverified*) |
| 15 announcements from a custom control | Partly: NodeCanvas's live region (`<id>.announcements`); the app's own edits (palette insertions) are not announced |
| 16 NodeGraph undo, serialization, conversions, frames | Partly: undo, rollback and serialization closed; conversions partly (N6), frames open (N7) |
| 17 document parser and serializer | Closed: `NodeGraph::save` / `load`, `gui-forms-graph 1` |
| 18 router scale | Open, *unverified* at 20,000 nodes: the routed style routes the whole graph on each revision |
| 19 custom clipboard formats | Open: no copy and paste of graph fragments |

## Closed since the first write

- **N1 No NodeCanvas control.** Closed by cp31's `NodeCanvas`
  (`controls/node_canvas.hpp`). The app's 1,700-line canvas and its
  geometry (view transform, levels of detail, link curves, spatial index)
  are gone; `GraphCanvas` is a subclass that supplies `check_link`,
  `paint_body` and `edit`, and overrides the six raisers.
- **N2 No theme roles for a consumer canvas.** Closed for the canvas's own
  parts: `node_canvas_anatomy` and `resolve_canvas_look`. The background
  variants are classes (`paper`, `blue`, `green`, `grey`) of the
  gui-forms theme's node canvas, switched with `add_class`/`remove_class`.
  What the app draws inside a body is N10.
- **N3 Lamp writes advance the shared revision.** Closed: `lamp_revision`.
- **N4 No per-record text cache.** No longer the app's: NodeCanvas draws the
  text (gap 6).
- **N5 No rollback of an open undo group.** Closed:
  `NodeGraph::rollback_undo_group`; Escape during a gesture and a cancelled
  edit leave no step, and `GraphDocument::cancel_gesture` uses it.
- **N9 Edit host placement.** Closed: `NodeEditRequest::editor`; the canvas
  hosts the kind's editor as its own child over the node and keeps it
  there through pans and zooms.

## Open, found by the app

**N6. No conversion table in NodeGraph.** Socket types are numbers; which
pairs connect, and how lossily, is the app's 5x5 table (`src/socket_types.*`).
NodeCanvas asks the app through `NodeCanvasCallbacks::check_link` for every
magnet candidate and before every connect, which is enough for the canvas;
the graph itself still accepts any pair. Shape: a per-graph conversion
predicate or table that `connect` consults, so every client agrees.

**N7. No frames or groups in NodeGraph.** The generator skips the spec's
frames around every 64 nodes. Shape: frame records with member lists, saved
in the document, drawn and hit-tested by NodeCanvas.

**N8. No file dialog without host services.** Save (Ctrl+S, now raised by
`GraphCanvas`) writes to the path the document came from; with none it is
counted as a failure. Open is from the command line.

**N10. No theme roles for what a consumer draws in a node body.**
`NodeBodyPaint` hands the painter, the body and the node's states, but no
ink for the body's content. The app borrows the canvas look's port-label
ink for numbers, the header fill for bar backgrounds, the selection stroke
for bar fills and the node outline for preview frames (`BodyInk`, copied
before each paint), and keeps data-type colours (`socket_colour`) for
preview strokes. Shape: body parts in the canvas anatomy (value bar track
and fill, preview frame, body text) resolved into `CanvasLook`, or the
item look passed in `NodeBodyPaint`.

**N11. The canvas's drawn link paths are not public.** `links_crossing`
answers from the canvas's own flattening (its curve segment count, the
router's paths); `soft_link_cubic` and `flatten_scene_cubic` are public but
the segment count and the port edges NodeCanvas uses are not, so a test
cannot build an independent brute-force oracle for the knife that agrees
exactly. The workload checks that the cut equals the canvas's query and
that nothing else was cut. Shape: `NodeCanvas::link_view_path(LinkId,
std::span<Point>)` returning the points it draws.

**N12. One ambient enlistment per control, shared with the base.**
`MotionClock::enlist_control_ambient` keeps one record per control.
NodeCanvas enlists it for its flow marks and withdraws it when the flow
stops (`update_flow_ambient`); `GraphCanvas` enlists the same record for
its time-driven sinks and overrides `on_ambient_frame` to serve both. A
withdrawal by the base can stop the sinks until their next paint enlists
again. Shape: a protected NodeCanvas hook (`virtual bool
wants_ambient_frames() const`) that its own enlist and withdraw consult,
or a per-reason count in the clock.

**N13. Socket item classes are numbers.** NodeCanvas gives each port the
item class `socket-<type in decimal>`, so a theme colours the app's float
sockets as `.socket-0`, colours as `.socket-1`. The app cannot name its
types for theme authors. Shape: a socket-type name table on the graph or the
canvas (`set_socket_type_name(0, "float")`) used for the class.

**N14. The palette's rows paint with literal colours.** `VirtualRowsView`'s
row paint target gets no ink, so the kind palette's title and family ink are
constants in `GraphRoot::paint_palette_row`. Shape: the row's resolved
foreground (normal, selected, hot) in `VirtualRowPaint`.

## Drawing ABI (`gd_*`)

The app draws only through the C++ `Painter` (`fill_rect`, `fill_checker`,
`stroke_rect`, `stroke_polyline`, `draw_text_utf8`) inside the body
callback; everything else is NodeCanvas's. No `gd_*` call is needed by the
app itself. For the seal: a C or C# client of `NodeCanvas` would need the
body callback's painter as a drawing context (`gd_context` borrowed for the
call) and a checker fill, which the C++ body uses for the swatch's
transparency backdrop (*unverified* that `gd_*` has one; see the flame graph
demonstrator's GAPS.md for the drawing ABI survey).

## Risks to confirm in the first build and run

- R1 (*unverified*): `GraphCanvas`'s constructor calls `set_graph` and
  `set_callbacks`; a raiser NodeCanvas raises from `set_graph` reaches the
  subclass's override (the vtable is the subclass's in its constructor body).
- R2 (*unverified*): `canvas_look()` called from `on_paint` before the base
  paint resolves the look the base then uses (no double resolution).
- R3 (*unverified*): enlisting the motion clock from `on_paint` for
  time-driven sinks, and N12's shared record.
- R4 (*unverified*): an editor that is never another control's child can be
  handed to the canvas, and its `focus_observed` event reports the loss when
  `end_edit` removes it.
- R5 (*unverified*): the magnet release-radius workload and test assume no
  other compatible input lies within 24 view units of the pulled point.
- R6: interval timing for the idle and frame budgets needs a windowed run;
  the headless workloads record capture intervals only.
