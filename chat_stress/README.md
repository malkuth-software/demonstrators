# Chat stress

A GUI.Forms demonstrator: a chat log of up to a billion generated messages
with variable row heights, written against the installed GUI.Forms SDK's
public headers only (`find_package(GUIForms 0.6)`). It follows the chat stress
specification (`CHAT_STRESS_SPEC.md` in the projects repository). Where the
public API fell short is recorded in `GAPS.md`.

Licence: PolyForm Noncommercial 1.0.0 (repository `LICENSE`). GUI.Forms is MIT.

## What it shows

- A message log (`VirtualRowsView`): avatar, name, time, wrapped body, header
  grouping for consecutive messages from one author within five minutes, and
  day separators. Rows are measured lazily; the view anchors the first visible
  row when rows above it change height and keeps at most 16,384 rows
  measured. Bodies are laid out by GUI.Forms' paragraph layout into storage
  the app reserves once.
- Follow-tail while messages arrive, with an "N new messages" badge when the
  user has scrolled away from the end.
- The view's extended selection (click, Shift, Ctrl, and dragging, which
  auto-scrolls at the edges), a context menu (Edit, Delete, Select none) and
  a compose box (Enter sends, Escape cancels an edit).
- Avatars generated per user and kept in a GUI.Forms `ImageAtlas` of 256
  slots.
- A status line with counters: messages, rows measured, height-index runs
  and bytes, layout-ring hits, atlas use and the selection.

The corpus is generated from a seed on demand: the same seed gives the same
authors, timestamps and text on every machine, so nothing is stored per
message except edits, tombstones and erased ranges. `tools/corpus_reference.py`
is an independent mirror of the generator; the tests' expected values come
from it.

## Build

```sh
cmake -S chat_stress -B build/chat_stress -G Ninja \
      -DCMAKE_PREFIX_PATH=<GUI.Forms prefix> \
      -DCMAKE_TOOLCHAIN_FILE=<the SDK's LLVM 22 toolchain file>
cmake --build build/chat_stress
ctest --test-dir build/chat_stress --output-on-failure
```

Without `GUIForms::Application` in the SDK, only the headless tests build.

## Run

```text
chat-stress [--seed S] [--messages N] [--users U] [--scripts latin|mixed]
            [--measure WORKLOAD] [--output-dir DIR] [--font-dir DIR]
            [--width W] [--height H]
```

Defaults: ten million messages, 5,000 users, Latin text, a 1280 x 800 window.
`--scripts mixed` adds made-up Greek, Cyrillic, CJK and Arabic (right to
left) words to about 15% of messages.

## Measurement

`--measure WORKLOAD` runs one scripted workload and writes a JSON record to
`<output-dir>/dev/measurements/<host>/<workload>-<seed>.json`. Each record
holds the machine (model, processor, memory, operating system), the build
(GUI.Forms version, build type), the corpus options and the figures. Counts
are operation counts (rows measured and forgotten, text shaping calls and
bytes, atlas fills and draws, height-index runs and bytes, frames presented,
damage area, scheduler wakes, GUI.Forms' counted allocations);
times are wall-clock medians and percentiles where the spec asks for them.
The workloads and their steps are in `docs/WORKLOADS.md`.

`--measure setup` runs headless (no window) and collects:

- setup time from handing `VirtualRowsView` its rows to the first frame being
  ready, for 100, 1,000,000 and 10,000,000 rows, as the median of three runs;
- the operation counts for each of those runs: rows measured and the height
  index's runs, nodes and bytes (from `VirtualRowsStatistics`), text shaping
  calls and bytes (from the paragraph layout), and the allocations GUI.Forms
  made on its counted paths over the run (`AllocationScope`). Those count
  GUI.Forms' own counted allocations only (image resources, the atlas,
  effects, display chunks, damage lists), not the program's or the text
  engine's; each figure carries `"scope": "gui_forms_counted_paths"`;
- at 10,000,000 rows, after a scripted scroll through the log, the steady-state
  footprint and the CPU time used over ten idle seconds.

The results are written to `<output-dir>/dev/measurements/<host>/setup-<seed>.json`
and the medians are printed. They are plain data for this machine and build.

## Tests

- `corpus_tests`: generator values against the reference script, determinism,
  bounds, timestamps and calendar dates, the erase map and edits.
- `row_layout_tests`: paragraph layout with estimated metrics (fit,
  coverage, cluster breaks for long URLs, line storage growth), the layout
  ring.
- `chat_view_tests`: a headless window over a million rows: bounded startup
  measuring, follow-tail and the badge, anchoring under an edit above the
  viewport, compose and edit, erasing, drag selection in the view's extended
  mode, the measured-row budget, semantics, and equal heights for equal seeds.
- `offscreen_paint_tests`: a real raster capture: rows painted, avatars
  filled into and drawn from the atlas, no paint fallbacks, identical pixels
  on a repeat capture.
