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

- GUI.Forms' public `NodeCanvas` (cp31) over its `NodeGraph`, subclassed
  as `GraphCanvas` (`src/graph_canvas*`): pan (middle drag, or Space and
  drag), zoom about the pointer (wheel), the theme's blueprint grid in
  levels of four that fade into each other, four levels of detail with
  hysteresis, and the background variants (paper, blueprint, green, grey)
  as theme classes of the canvas, switched from the context menu.
- 21 node kinds in five families (sources, math, colour, curves, sinks) and
  a reroute, with typed sockets (float, colour, curve, signal, boolean), a
  conversion table answering the canvas's `check_link`, live previews
  (value bars, colour swatches, curves, a scrolling plot, readouts) painted
  through its `paint_body` target, and output lamps written by an evaluator
  that runs in topological order and refuses cycles; links from time-driven
  sources carry the theme's flow marks.
- Soft curved links, or routed links through GUI.Forms' `LinkRouter`; link
  drags with the canvas's `DragTracker` magnet (capture within 16 view
  units, held until 24); a release on empty canvas opens the add-node
  palette filtered to kinds that accept the link.
- Click, Shift and Ctrl selection, marquee, moves snapped to the grid (one
  undo step per drag), the knife (Ctrl + secondary drag) to cut links,
  Delete, undo and redo (Escape rolls a gesture back with no step), the
  keyboard graph, F2 and double-click editing in one reused editor per kind
  that the canvas hosts over the node, Ctrl+S to save.
- Every NodeCanvas event raiser overridden: each counts, keeps the
  document's counters and autosave in step, and calls its base.
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
| `src/graph_canvas.*`, `src/graph_canvas_body.cpp` | The NodeCanvas subclass: link check, bodies, editors, raisers, backgrounds, evaluation, flow, live sinks |
| `src/graph_root.*` | The window content: canvas, status line, palette, editors, context menu |
| `src/measurement.*`, `src/json_writer.*`, `src/workloads.*` | Records and workloads |
| `src/options.*`, `src/graph_window.*`, `src/main.cpp` | Command line, window, entry |
| `tests/` | Model, document and headless canvas tests; offscreen paint test |
