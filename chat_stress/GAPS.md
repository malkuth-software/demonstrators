# Chat stress: where the public GUI.Forms API fell short

Written against the cp29 integration head (`claude/batch-cp29-2b-b`,
`3e2ae741d`), installed headers only. Each entry says what the app needed,
what it did instead, the GUI.Forms header concerned and the shape of the
missing API. Nothing here has been built yet; entries marked *unverified*
depend on behaviour the headers describe but no run has confirmed.

The spec (section 9) expected ten gaps. Status against what the app actually
needed:

| Spec gap | Status |
|---|---|
| 9.1 public height index | Mostly resolved: VirtualRowsView owns the index. The memory policy is still open (A2) |
| 9.2 owner-drawn variable-height list | Resolved by `VirtualRowsView`; new, smaller gaps in A3 to A5 |
| 9.3 light per-row text measuring | Confirmed (A1) |
| 9.4 scroll anchoring | Refuted for this app: VirtualRowsView anchors (`anchor_corrections`) |
| 9.5 image atlas | Confirmed (A6) |
| 9.6 allocation and frame counters | Half: frame, damage and wake counters are public (`MetricsSnapshot`); allocations are not (A7) |
| 9.7 virtual semantics at scale | Confirmed (A8) |
| 9.8 follow-tail | Resolved: `VirtualRowsView::set_follow_tail` |
| 9.9 process footprint | Confirmed (A9) |
| link-router cap (coordinator's list) | Refuted for this app (A10) |

## A. Gaps, most important first

### A1. No public "wrap this paragraph into my storage" call

- **Needed:** for each row it shows, the body text's lines (byte ranges and
  widths) and the row's height at the column width, for tens of rows per
  frame, without allocating.
- **Found:** `TextBlock` has exactly this breaker (`prepare_text`,
  `break_lines`, a reused `Workspace`), but it is private
  (`include/gui_forms/controls/text_block.hpp`). `PreparedTextLayout` is
  asynchronous and each layout reserves the full 8 MiB payload
  (`include/gui_forms/prepared_text/layout/prepared_text_layout.hpp`,
  `PreparedTextLimits::payload_bytes`), so a ring of 256 row layouts is out
  of the question.
- **Did instead:** `src/row_layout.cpp` breaks lines itself from
  `SurfaceRoot::fill_line_break_opportunities_utf8` and re-measures each
  candidate line with `SurfaceRoot::measure_text_extent_utf8`. Every
  candidate is shaped again from the line start, so a line of *w* words costs
  *w* shaping calls over a growing prefix; a 550-byte message costs on the
  order of 90 calls and several kilobytes shaped. The counters
  `text_measure_calls` and `text_measured_bytes` in every record show the
  real figure.
- **Shape:** a synchronous call on the metrics seam (the window's provider):

  ```cpp
  struct TextLineBreak { std::uint32_t begin, end; double width; };
  struct TextWrapWorkspace;   // caller-owned, reused; reserves once
  TextResolutionStatus SurfaceRoot::wrap_text_utf8(std::string_view utf8, FontSpec font,
      double width, TextWrapWorkspace& workspace, std::vector<TextLineBreak>& lines) const;
  ```

  shaping the paragraph once and reading line widths from cluster advances,
  with TextBlock's rules (white space, `overflow-wrap`, inside-word breaks
  opt-in). A cheaper first step: `measure_text_advances_utf8(text, font,
  std::span<double> prefix_widths)` so one shaping call gives every prefix
  width.

### A2. The height index's memory under a long scroll

- **Needed:** bounded memory after scrolling through ten million rows
  (spec 4.2, workload 7.4c).
- **Found:** `VirtualRowsView` keeps `RowHeightIndex` private (only forward
  declared in `include/gui_forms/controls/virtual_rows_view.hpp`) and
  measures each row once at the current width; nothing forgets measured rows
  far from the viewport. `VirtualRowsStatistics::index_runs` and
  `index_nodes` show the growth but not the bytes.
- **Did instead:** nothing (the app must not reach the index). The
  `page-down` workload records `index_runs` per tick.
- **Shape:** on VirtualRowsView, `set_measured_row_budget(std::size_t rows)`
  (rows measured beyond the budget and far from the anchor return to the
  estimate, oldest first) and `VirtualRowsStatistics::index_bytes`.

### A3. VirtualRowsView: no range or multiple selection

- **Needed:** select a range of messages by dragging (spec 4.1 "select one
  message", extended to ranges for the drag client).
- **Found:** single selection only (`selected_row`, `set_selected_row`).
- **Did instead:** the range is app state, painted by the row painter as a
  tint; the drag runs on `DragTracker` with auto-scroll, fed from the view's
  public `mouse_down`/`mouse_move`/`mouse_up` events through
  `pointer_event_of`, because the view is `final` and its own press handling
  runs first (*unverified* that the tracker's capture keeps those events
  coming).
- **Shape:** a `SelectionModel` hook on VirtualRowsView (the widgets tour's
  row 2.5 item): `set_selection_mode(single | extended)`, ranges with
  shift-click and drag, `selected_ranges()`, and the row item's `selected`
  state from it.

### A4. VirtualRowsView: no scrolled event, no row invalidation

- **Needed:** update the "N new messages" badge when the user scrolls back
  to the end, and repaint only the rows whose tint changed.
- **Found:** no `scrolled()` / `offset_changed()` event and no
  `invalidate_rows(first, count)`.
- **Did instead:** the badge is refreshed from the view's `mouse_wheel` and
  `key_down` events (a scroll-bar drag back to the end leaves the badge until
  the next append or wheel); range changes invalidate the whole viewport.
- **Shape:** `Event<double>& scrolled()` raised after each settle that moved
  the offset (with `following_tail()` current), and
  `invalidate_rows(VirtualRowIndex first, std::size_t count)`.

### A5. VirtualRowsView: the measure target has no row context

- **Needed:** a row's height depends on its predecessor (header grouping,
  day separators, spec 3.3).
- **Found:** `VirtualRowMeasure` carries the row and width only, and
  `refresh_rows` is the only way to say a row's height changed because a
  *neighbour* changed.
- **Did instead:** the painter keys its layout cache on the predecessor's
  message too, and ChatRoot refreshes the row after an erased range. This
  works; it is recorded because every grouped list will repeat it.
- **Shape:** none needed if documented; optionally
  `refresh_rows_after(VirtualRowIndex)` semantics for "the row after an
  erase".

### A6. No image atlas

- **Needed:** up to 5,000 generated avatars, about 40 visible, drawn every
  frame from a bounded store (spec 3.3).
- **Found:** images are registered whole (`SurfaceRoot::load_bgra32_premultiplied`);
  a slot update is `patch_bgra32_premultiplied`, which needs a consumer
  control and a damage rectangle and cannot run during paint
  (`include/gui_forms/surface_root/surface_root.hpp`). There is no sub-image
  identity, so every draw passes a source rectangle, and no eviction helper.
- **Did instead:** `src/avatar_atlas.cpp`: one 1280 x 1280 image, 256 slots,
  least recently used replaced, slots filled while rows are *measured* (layout)
  so paint only reads; a paint that finds no slot draws a disc and counts
  `paint_fallbacks` (*unverified* that a patch is accepted inside the view's
  settle).
- **Shape:** `ImageAtlas` in `resources/`: `ImageAtlas(SurfaceRoot&, Size slot_pixels,
  std::size_t slots)`, `AtlasSlot acquire(std::uint64_t key)` (hit, or the
  evicted slot to fill), `fill(AtlasSlot, std::span<const std::byte>)` with
  uploads deferred to the next frame, `Painter::draw_atlas_slot(slot, Rect)`,
  and statistics (hits, misses, evictions, bytes).

### A7. No public allocation counter

- **Needed:** "zero heap allocations per frame once the layout ring is
  warm" (spec 5) and allocation counts in the setup collection.
- **Found:** `MetricsSnapshot` (`include/gui_forms/metrics/types/metrics_types.hpp`)
  has frames, damage, wakes and timer ticks, but no allocation figure; the
  allocation counter the drag tests use is not installed.
- **Did instead:** records carry `"allocations": null` and a *net* heap
  figure read without hooking the allocator (macOS
  `malloc_zone_statistics`, glibc `mallinfo2`): blocks and bytes in use
  before and after, which is not a count.
- **Shape:** `gui_forms::allocation_statistics()` (allocations, frees, bytes)
  for the library's own allocations, or MetricsSnapshot fields filled when
  the library is built with its counting allocator.

### A8. Virtual semantics allocate per read

- **Found:** `Control::semantic_virtual_children()` returns
  `std::vector<SemanticNode>` by value; VirtualRowsView builds one node per
  realised row per read and calls `describe` for each.
- **Did instead:** nothing; `semantic_children` per tick is recorded.
- **Shape:** a fill-into-caller-storage form
  (`void fill_semantic_virtual_children(std::vector<SemanticNode>&) const`)
  with reused node strings, and the platform virtualised-item patterns.

### A9. Process figures are host code in the app

- `src/process_stats.cpp` reads footprint (task_vm_info, PrivateWorkingSetSize,
  smaps_rollup Pss), CPU time, process start and the machine description
  with each platform's API. **Shape:** `gui_forms::process_diagnostics()`
  returning footprint, CPU time and start time, beside `MetricsSnapshot`.

### A10. Link router: not needed here

- `include/gui_forms/controls/link_router.hpp` is the node graph's edge router;
  the chat app routes no links. If "links" means clickable URLs in message
  text: the app draws URLs as plain text, because hit-testing a span inside
  owner-drawn wrapped text needs per-cluster positions, which comes back to
  A1 (a wrap result with cluster offsets).

## B. Smaller findings

- **Clipboard:** no clipboard write reachable from a control without the
  host services API (`include/gui_forms/host/services/host_services.hpp`), so
  the context menu has no "Copy text".
- **Frame clock:** the windowed workloads step on a 16 ms `Timer`; there is
  no public per-present callback, so input-to-present is observed at tick
  granularity (each record says so).
- **Window callbacks:** `ApplicationWindowOptions::ready` and friends are
  `std::function`; the app passes a named function object
  (`ChatWindowReady`) to keep to house style.
- **Status strip:** the status line is a `Label`; `StatusStrip`'s item API was
  not needed for one line of counters.
- **Custom layout:** ChatRoot overrides `arrange` and calls each child's
  `arrange` with its rectangle after `arrange_self`, as `control.hpp`
  describes for custom layout owners (*unverified* that a child's arrange
  takes the parent's local coordinates).
