# Flame graph

A GUI.Forms demonstrator (D2 of the exhibits plan, roadmap row 4.5): a
profiler viewer that loads a recorded trace and shows it as a zoomable
flame graph or icicle chart, a timeline with one lane per thread, and a
sortable table of functions, with search across all three. Built against
the installed SDK's public headers only (`find_package(GUIForms 0.6)`), on
the plots package (view model, scales, ticks), the Painter's batched
primitives and VirtualRowsView. Where the public API fell short is
recorded in `GAPS.md`, with the gd_* drawing ABI gaps that bear on the
seal; the measured workloads are in `docs/WORKLOADS.md`. Clean-room:
written from the behaviour of such viewers only, never their code.

Licence: PolyForm Noncommercial 1.0.0 (repository `LICENSE`). GUI.Forms is MIT.

Status: written, not yet built.

## Traces

- **Chrome trace JSON** (the Trace Event Format), as written by Chrome's
  tracing, Perfetto's JSON export and many profilers: complete (`X`) and
  begin/end (`B`/`E`) events, with `thread_name` metadata naming lanes. A
  bounded pull reader reads it without building a document.
- **Folded stacks** (`a;b;c 12` per line, the input of flamegraph.pl), laid
  on one lane in file order, one unit per sample.
- **Generated:** without `--trace`, a seeded synthetic trace (`--threads`,
  `--spans`, `--names`, `--seed`).

## What it shows

- **Flame graph / icicle** (toggle in the toolbar): the merged call tree,
  one cell per node, width by total time, children in name order. Click
  selects, double click zooms to a cell (the root resets), drag pans, the
  wheel zooms about the pointer, Shift+wheel scrolls deep stacks, Home
  resets. Narrow subtrees are skipped whole, so a frame costs about the
  visible rows times the width.
- **Timeline:** per thread, a lane of rows by call depth (capped at 32),
  spans as bars over a millisecond ruler. Sub-pixel spans merge, and the
  row walk jumps past them by binary search, so a frame costs about one
  visit per pixel per row however many spans the range holds.
- **Details table:** per function, total (counted once per stack, so
  recursion does not inflate it), self, calls and share; click a header to
  sort, select a row to outline the function's cells, double click to
  search for it.
- **Search:** a case-insensitive substring over names; matches are drawn
  in magenta, everything else dimmed, the table filtered, and the status
  line gives the matched share (each matched subtree counted once).

## Layout

```
src/string_table.*     interned names (one arena, open addressing)
src/json_reader.*      bounded pull JSON reader
src/trace_model.*      lanes, depths, the merged call tree, function rows, search
src/trace_loader.*     Chrome JSON and folded-stack loaders
src/trace_generator.*  the seeded synthetic trace (model or JSON text)
src/profile_ink.*      colours, number formats, the label cut
src/flame_view.*       flame graph / icicle
src/timeline_view.*    timeline lanes and ruler
src/details_table.*    function table over VirtualRowsView
src/profiler_root.*    toolbar, views, status, selection sync
src/workloads.*        headless measured workloads
tests/                 model, loader, view (headless) and offscreen paint tests
```

## Build

```
cmake -S flame_graph -B build/flame_graph -G Ninja \
      -DCMAKE_PREFIX_PATH=<GUI.Forms prefix> \
      -DCMAKE_TOOLCHAIN_FILE=<the SDK's LLVM 22 toolchain file>
cmake --build build/flame_graph
ctest --test-dir build/flame_graph --output-on-failure
build/flame_graph/flame-graph --trace trace.json
```
