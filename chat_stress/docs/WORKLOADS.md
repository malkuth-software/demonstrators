# Chat stress workloads

`chat-stress --measure <workload>` runs one script and closes the window when
it ends. The script is `src/workload_driver.cpp`: a component stepped by a
16 ms `Timer` (one tick a frame at 60 Hz), which drives the window through
its public input dispatch (`dispatch_pointer`, key events, `request_focus`)
and the view's public scrolling calls. Every tick records a sample: phase,
tick interval, the change in the counters since the last tick, and the
allocations GUI.Forms' counted paths made in that tick. Seeded
choices (jump targets, edited rows) come from the corpus seed, so two runs
with one seed make the same choices.

Times are wall clock. Ticks are 16 ms apart, so an input's effect is seen at
tick granularity: the public API has no per-present callback (`GAPS.md` B).

| Workload | Spec | Steps |
|---|---|---|
| `startup` | 7.1 | Ticks until every visible row is measured and one more frame is presented; records the first present and ready-to-use times from process start. Gives up after 600 ticks. |
| `memory` | 7.2 | `wheel`, then `trackpad`, then `jump`, then 10 s settled; footprint at each phase end. |
| `idle` | 7.3 | Middle of the log, pointer left the window, view focused; 10 s to settle, then the driver's timer stops and a single 60 s one-shot ends the observation. Records presents, wakes and CPU time over the minute (the one-shot's own wake is subtracted). |
| `idle-focused` | 7.3 | As `idle`, with the compose box focused (caret blink is the expected work). |
| `wheel` | 7.4a | Middle of the log; one line up each tick: 2 s warm-up, 10 s measured. |
| `trackpad` | 7.4b | A fixed fling: 40 pixels, 5% less each tick, restarted every 60 ticks; 2 s warm-up, 10 s measured. |
| `page-down` | 7.4c | Page Down every second tick (about 30 a second) for 120 s; height-index runs and bytes recorded per tick (the measured-row budget bounds them). |
| `thumb` | 7.4d | Offset moved from the top to the end over 5 s, as a thumb drag. |
| `jump` | 7.5 | 1,000 jumps to seeded messages, Ctrl+End, Ctrl+Home, then 100 thumb clicks at seeded shares of the extent. |
| `append` | 7.6 | Six 30 s segments at the end with follow-tail: 1 a second, 100 a second, a 10,000-message burst every 2 s; then the same three scrolled away from the end (badge counts). |
| `edit` | 7.7 | 1,000 edits of visible rows (texts of other seeded messages, so heights change); 1,000 edits of measured rows above the viewport, each checking that the first visible row did not move (maximum drift recorded); 1,000 range erases of 1 to 10,000 rows. |
| `setup` | — | Headless; see below. |

## Setup collection (`--measure setup`)

`src/setup_collection.cpp`, run without a window or host: the chat content
is attached to an `OffscreenAttachment` and painted by `OffscreenRaster`
with the real CPU raster at scale 2.

For each of 100, 1,000,000 and 10,000,000 rows, three runs:

1. a new corpus of that size;
2. the clock starts; the chat content is created, which hands the rows to
   `VirtualRowsView` (`reset_rows`);
3. layout, then one captured frame; the clock stops when the frame is ready;
4. the run's counts: rows measured, height-index runs, nodes and bytes,
   text shaping calls and bytes, and GUI.Forms' counted allocations on this
   thread over steps 2 and 3 (`AllocationScope`).

The record holds each run and the median of three. After the last
10,000,000-row run, the collection scrolls (600 wheel steps and the `jump`
sequence), reads the footprint, the height index's bytes and measured rows
(under the 16,384-row budget) and the process-wide counted allocations,
then measures the CPU time the process uses over ten idle seconds.

Limits:

- Allocation counts are GUI.Forms' counted paths only (image resources, the
  atlas, effects, display chunks, damage lists): not the program's, the
  standard library's elsewhere, or the text engine's shaping (`GAPS.md` A14).
- Headless idle observes only background work (no host, no presents). The
  windowed `idle` workload is the one that counts presents and wakes.
- Footprint is the platform's own figure (macOS physical footprint, Windows
  private working set, Linux proportional set size) and is not comparable
  across platforms.
