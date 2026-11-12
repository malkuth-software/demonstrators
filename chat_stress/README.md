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
  row when rows above it change height.
- Follow-tail while messages arrive, with an "N new messages" badge when the
  user has scrolled away from the end.
- Single selection by click, range selection by dragging (auto-scrolls at the
  edges), a context menu (Edit, Delete, Select none) and a compose box
  (Enter sends, Escape cancels an edit).
- Avatars from a bounded 256-slot atlas, generated per user.
- A status line with the counters: rows, realised rows, rows measured, text
  measuring calls, atlas uploads and frames.

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
are operation counts (rows measured, text measuring calls and bytes, atlas
uploads, height-index runs, frames presented, damage area, scheduler wakes);
times are wall-clock medians and percentiles where the spec asks for them.
The workloads and their steps are in `docs/WORKLOADS.md`.

`--measure setup` runs headless (no window) and collects:

- setup time from handing `VirtualRowsView` its rows to the first frame being
  ready, for 100, 1,000,000 and 10,000,000 rows, as the median of three runs;
- the operation counts for each of those runs: rows measured and height-index
  operations (from `VirtualRowsStatistics`), text measuring calls, and the net
  heap change (an allocation *count* is not available through the public API,
  so `allocations` is written as `null`; see `GAPS.md` A7);
- at 10,000,000 rows, after a scripted scroll through the log, the steady-state
  footprint and the CPU time used over ten idle seconds.

The results are written to `<output-dir>/dev/measurements/<host>/setup-<seed>.json`
and the medians are printed. They are plain data for this machine and build.

## Tests

- `corpus_tests`: generator values against the reference script, determinism,
  bounds, timestamps and calendar dates, the erase map and edits.
- `row_layout_tests`: line wrapping with estimated metrics, in-word breaks for
  long URLs, the layout ring.
- `chat_view_tests`: a headless window over a million rows: bounded startup
  measuring, follow-tail and the badge, anchoring under an edit above the
  viewport, compose and edit, erasing, drag selection, semantics, and equal
  heights for equal seeds.
- `offscreen_paint_tests`: a real raster capture: rows painted, avatars
  uploaded, no paint fallbacks, identical pixels on a repeat capture.
