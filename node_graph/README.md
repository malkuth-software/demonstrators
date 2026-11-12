# Node graph

A GUI.Forms demonstrator: a node graph editor on a blueprint grid, written
against the installed GUI.Forms SDK's public headers only
(`find_package(GUIForms 0.6)`). It follows the node graph specification
(`NODE_GRAPH_SPEC.md` in the projects repository). Where the public API fell
short is recorded in `GAPS.md`; the measured workloads are in
`docs/WORKLOADS.md`.

Licence: PolyForm Noncommercial 1.0.0 (repository `LICENSE`). GUI.Forms is MIT.

Status: written, not yet built.

## What it shows

- A canvas (`src/node_canvas_view.*`) over GUI.Forms' `NodeGraph`: pan
  (middle drag, or Space and drag), zoom about the pointer (wheel), a world
  grid in levels of four that fade into each other, and four levels of
  detail with hysteresis (overview, silhouette, compact, full).
- 21 node kinds in five families (sources, math, colour, curves, sinks) and
  a reroute, with typed sockets (float, colour, curve, signal, boolean), a
  conversion table, live previews (value bars, colour swatches, curves, a
  scrolling plot, readouts) and output lamps from an evaluator that runs in
  topological order and refuses cycles.
- Soft curved links, or routed links through GUI.Forms' `LinkRouter`; link
  drags with a magnet (capture within 16 view units, release beyond 24)
  over GUI.Forms' `DragTracker`; a release on empty canvas opens the add-node
  palette filtered to kinds that accept the link.
- Click, Shift and Ctrl selection, marquee, moves snapped to the grid (one
  undo step per drag), the knife (Ctrl + secondary drag) to cut links,
  Delete, undo and redo, keyboard moves, F2 and double-click editing in one
  edit host per kind.
- Load and save in GUI.Forms' `gui-forms-graph 1` format, tolerant on load
  (comments, CRLF, spacing) and canonical on save; unknown kinds and
  properties are kept and written back.
- A seeded stress generator and headless workloads that write plain JSON
  records of every counter.

## Build

    cmake -S node_graph -B build/node_graph -G Ninja \
          -DCMAKE_PREFIX_PATH=<GUI.Forms prefix> \
          -DCMAKE_TOOLCHAIN_FILE=<the SDK's LLVM 22 toolchain file>
    cmake --build build/node_graph
    ctest --test-dir build/node_graph --output-on-failure

## Layout

| Path | Contents |
|---|---|
| `src/socket_types.*`, `src/kinds.*` | Socket types and conversions; the kind table |
| `src/graph_document.*` | The document over `NodeGraph`: counted edits, gestures as undo groups, type and cycle checks, files, autosave |
| `src/evaluator.*` | Evaluation, lamps, plot histories, the bounded expression parser |
| `src/generator.*` | Seeded stress graphs and seeded edits |
| `src/canvas_geometry.*` | View transform, levels of detail, link curves, crossings, the spatial index |
| `src/node_canvas_view.*`, `src/node_canvas_paint.cpp` | The canvas control: input, gestures, hit testing, semantics, painting |
| `src/graph_root.*` | The window content: canvas, status line, palette, edit hosts, context menu |
| `src/measurement.*`, `src/json_writer.*`, `src/workloads.*` | Records and workloads |
| `src/options.*`, `src/graph_window.*`, `src/main.cpp` | Command line, window, entry |
| `tests/` | Model, document, geometry and headless canvas tests; offscreen paint test |
