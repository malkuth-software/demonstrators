# Flame graph: where the public GUI.Forms API fell short

First written against the cp31 integration tree (`b8e702fa6`), installed
headers only (`find_package(GUIForms 0.6)`), before any build.
F-numbered entries concern controls, the Painter and the plots package;
D-numbered entries the gd_* drawing ABI (`gui_forms/drawing_c_api.h`, ABI
0.3), which is to be sealed. Each entry says what the app needed, what it
does instead, the header concerned and the shape of the API wanted.
Entries marked *unverified* depend on behaviour the headers describe but no
run has confirmed.

| Gap | Status |
|---|---|
| F1 PlotViewModel has no single-axis mode | Closed: `22d9fbaa7` (axis-zone rules stated and tested; `set_gesture_axes`) |
| F2 no elided or boxed text | Closed: `06cc3e6a7` (`fit_text_utf8`, `draw_text_utf8_fitted`) |
| F3 no batched text | Closed: `06cc3e6a7` (`draw_text_runs`) |
| F4 VirtualRowsView has no columns or header | Open: design note below; not in this batch |
| F5 row ink not passed to row paint | Closed: `a6fbe5a12` (`VirtualRowPaint::foreground`) |
| F6 plot theme roles unreadable by custom views | Closed: `20da664c0` (`resolve_plot_palette`, `plot_series_color`) |
| F7 no categorical or hashed palette | Closed: `2d78c1bbe` (`categorical_color`, `plot_category_color`) |
| F8 tick labels have no unit formatting | Closed: `2d78c1bbe` (`TickLabelUnit::duration`) |
| F9 no tooltip for a region of a custom control | Owner decision: the hook exists; data items carry no tooltips |
| F10 custom views publish no semantic items | Not a library gap: the realized-item virtuals |
| F11 no file dialog without host services | Open: host services (as node graph N8) |
| F12 loading runs on the UI thread | Not a library gap: BackgroundWorker; see the note |
| D1 no per-rectangle colours | Closed: `c6d025801` (ABI 0.4) |
| D2 `recorder_draw_string` has no layout box | Closed: `991c00fbc` (ABI 0.5) |
| D3 no batched strings | Closed: `991c00fbc` (ABI 0.5) |
| D4 no recorder reset for reuse | Closed: `991c00fbc` (ABI 0.5) |
| D5 clip semantics unstated | Closed: `991c00fbc` (stated and tested) |

## Controls, Painter and plots

**F1. PlotViewModel has no single-axis mode.** The flame graph and the
timeline need a horizontal range only: drag pans it, the wheel zooms about
the pointer. `plots::PlotViewModel` moves every axis for a gesture in the
plot zone, and the header says an axis with a locked end refuses pan. The
views keep an unused y axis and hand every press and wheel turn over with
`PlotZone{PlotZoneKind::x_axis}`, so only x moves (*unverified*: that an
x-axis press starts a pan of x alone and an x-axis wheel zooms x about the
pointer). Header: `plots/plot_view_model/plot_view_model.hpp`. Shape: a
model option naming the axes gestures move (`set_gesture_axes(x_only)`),
or a pan that leaves locked axes still instead of refusing.

**F2. No elided or boxed text.** A flame cell or a timeline bar shows its
name when it is wide enough and a cut name with an ellipsis otherwise; a
table cell the same. `Painter::draw_text_utf8` takes an origin and
`measure_text_utf8` a whole string; neither takes a width or reports how
much fits. The app measures each name once (cached per name), spreads the
width evenly over its scalars and cuts at the estimated count less half a
glyph (`cut_label`); proportional text can still overrun by part of a
glyph, which the plot clip hides at the view's edge but not between cells.
Header: `types/painter/painter.hpp`. Shape:
`draw_text_utf8_fitted(origin, text, font, color, max_width, TextTrim)`
or `measure_text_prefix(text, font, max_width) -> bytes`.

**F3. No batched text.** A full flame view draws a few hundred labels per
frame, each a separate `draw_text_utf8` with the same font and colour.
Header: `types/painter/painter.hpp`. Shape: `draw_text_runs(font,
std::span<const TextRun>)` with `TextRun{origin, text, color}`.

**F4. VirtualRowsView has no columns or header.** The details table is a
sortable grid of five columns over thousands of functions. VirtualRowsView
gives virtualised owner-drawn rows; `DetailsTable` paints the header strip
above it, hit-tests its own column edges for sorting, and repeats the same
edges in each row's paint callback. No column resizing, no header keyboard
access, no sort indicator part for themes. DataGridView has columns but no
virtual mode over row callbacks (ImPlot-class and ImGui-class GAPS).
Header: `controls/virtual_rows_view.hpp`. Shape: a column model on
VirtualRowsView (widths, alignment, a header with a `header_clicked`
event and a sort indicator part, a per-cell paint target), or a virtual
mode for DataGridView.

**F5. Row ink is not passed to the row paint target.** As node graph N14:
`VirtualRowPaint` carries the row's states and the view for `paint_part`,
but no resolved foreground, so the table's text uses constants. Header:
`controls/virtual_rows_view.hpp`. Shape: the row's resolved foreground in
`VirtualRowPaint`.

**F6. Plot theme roles unreadable by custom views.** As ImPlot-class P5:
the ruler, lane headers, grid and labels are custom paint; the plots
package's roles (axis, tick, label, grid) are resolved only inside
PlotView, so `profile_ink.hpp` holds constants. Shape: the resolved plot
look for any control (`plots::resolve_plot_look(const Control&)`).

**F7. No categorical or hashed palette.** A flame graph colours a
function the same everywhere it appears. The plots package has colormaps
(`sample_colormap`) for continuous values but nothing that maps an
identity to a stable, theme-aware categorical colour; the app hashes
names into flamegraph.pl's hot range. Header: `plots/colormap/`. Shape:
`plots::categorical_color(std::uint64_t key, CategoricalPalette)`.

**F8. Tick labels have no unit formatting.** The time ruler's ticks come
from `generate_ticks` over milliseconds; the labels are plain numbers, so
a zoom to microseconds shows "0.0012" and a "ms" tag. Header:
`plots/tick_generator/tick_generator.hpp`. Shape: a label formatter in
`TickRequest` (duration units with automatic prefix), or a duration scale.

**F9. No tooltip for a region of a custom control.** Hovering a cell or a
span should show its full name, total, self and share. Tooltips attach to
controls; a custom view cannot answer per point. The status line shows the
selection's figures instead. Shape: a Control hook `tooltip_at(Point,
std::string&) -> bool` used by the one presenter.

**F10. Custom views publish no semantic items.** Cells and spans are not
accessibility items. Control has realized-item virtuals (VirtualRowsView
overrides `realized_item_count` and its siblings), which a view could
implement for its visible cells; not attempted.

**F11. No file dialog without host services.** As node graph N8: the
trace comes from `--trace`.

**F12. Loading runs on the UI thread.** A 512 MiB trace blocks the
window while it parses. GUI.Forms has a background worker
(`background_worker/`); the model would need a hand-off of a finished
model to the UI thread. Not attempted.

## Closing notes (branch `claude/cp32-demonstrator-gaps`)

The shas above are on that branch, not yet folded; the app still uses its
workarounds until the fold.

- **F1.** `PlotViewModel` states which axes each gesture moves: one in an
  axis zone (`PlotZone{x_axis, i}`) moves axis i alone (drag pans, wheel
  zooms about the pointer), one in the plot zone every admitted axis; and
  `set_gesture_axes(PlotGestureAxes::x_only)` confines every input change
  (gestures in any zone, box zoom, keys and fit) to x. Test:
  `test_view_model_single_axis` (plot primitives). This answers R1.
- **F2, F3.** `Painter::fit_text_utf8` (the longest prefix in whole code
  points within a width, by binary search), `draw_text_utf8_fitted` with
  `TextTrim::cut` or `ellipsis` (two draws at most, no allocation) and the
  virtual `draw_text_runs` (`TextRun`: origin, text, colour). Test:
  `gui_forms_painter_text_fitting_tests`.
- **F4.** Not in this batch. The shape stands: a column model on
  VirtualRowsView (widths, alignment, a header part with a sort indicator,
  `header_clicked`, a per-cell paint target), which needs a design note
  and the header's keyboard and accessibility rules first.
- **F5.** `VirtualRowPaint::foreground` is `item_content_text` for the
  selection family in the row's states. Test: `million_rows_bounded_work`.
- **F6.** `resolve_plot_palette(const Control&)` (grid, axis, text,
  selection) and `plot_series_color`, in `controls/plot_view_palette.hpp`.
- **F7.** `plots::category_key` (FNV-1a 64 over a name),
  `category_position` and `categorical_color(key, map)`; for the theme's
  accents, `plot_category_color(control, key)`. A warm colormap of the
  app's own reproduces flamegraph.pl's palette through `categorical_color`.
- **F8.** `TickRequest::label_unit = TickLabelUnit::duration` with
  `unit_seconds` (1e-3 for a millisecond axis): one unit per tick set (s,
  ms, µs, ns; the largest in which the step is at least 0.1), "0.5 ms".
- **F9.** A control can already answer per region: set
  `set_publishes_item_tool_tips(true)` in its constructor and override
  `tool_tip_items()` with the visible regions' nodes (bounds and
  `tool_tip`). The owner's 2026-11-02 rule is that data items carry no
  tooltips (DataGridView's error glyphs are the one exception), and flame
  cells are data, so this is the owner's call, not a missing API.
- **F10.** Control's realized-item virtuals (`realized_item_count` and its
  siblings, as VirtualRowsView overrides them) publish visible cells; an
  app task, not a library gap.
- **F11.** A file dialog is a host service; the headless and command-line
  runs have none. Same as node graph N8.
- **F12.** `BackgroundWorker` runs the parse; the app keeps the finished
  model in its own shared slot written by the work and read in
  `run_worker_completed` on the UI thread, since `BindingValue` carries no
  arbitrary object. No library gap; whether a typed result hand-off is
  wanted is open.
- **D1-D5.** ABI 0.4 added `recorder_fill_rectangles_each` (a colour per
  rectangle, uint64 count; as ImPlot-class G4).
  ABI 0.5 adds `recorder_draw_string_in_rect` (alignment, line alignment,
  trimming, clipped to the box unless 0x4000), `recorder_draw_strings`
  with `gd_text_item`, `recorder_reset` (keeps storage, reopens a closed
  recorder), `measure_string_fitted` (bytes fitted and their width), and
  states the clip: `recorder_set_clip` replaces, in target coordinates;
  `recorder_save`/`restore` carry it; after close every recording call
  fails. Tests: `check_text_and_reuse` (C11) and
  `laid_out_text_aligns_trims_and_clips` (Skia).

## Drawing ABI (`gd_*`)

The C++ views draw through `Painter::fill_rects_each`, `stroke_rect`,
`stroke_segments`, `draw_text_utf8` and `measure_text_utf8`. A C or C#
client drawing the same views through gd_* (a recorder per frame) meets
these gaps; they bear on the seal.

**D1. No per-rectangle colours.** A flame frame is thousands of cells in
hundreds of colours, drawn by the C++ view in one `fill_rects_each` call.
`recorder_fill_rectangles(recorder, brush, rects, count)` takes one brush,
so a gd_* client sorts its cells by colour and makes one brush and one
call per colour, or calls `recorder_fill_rectangle` per cell. Same as
ImPlot-class G4. Shape: `recorder_fill_rectangles_colored(recorder, const
gd_rect* rects, const gd_color* colors, uint64_t count)`.

**D2. `recorder_draw_string` has no layout box.** It takes an origin
(`gd_point`) and a string format. The format carries trimming
(`GD_TRIM_ELLIPSIS_CHARACTER` and the rest, via `string_format_set`), but
with no layout rectangle there is no width to trim to, so a cell label
cannot be elided by the ABI; `measure_string` takes a layout width but
returns a size, not how much fitted. Shape: `recorder_draw_string_in_rect
(recorder, text, font, brush, gd_rect layout, format)` honouring
trimming and alignment, and `measure_string` reporting the bytes fitted
(GDI+'s codepoints-fitted).

**D3. No batched strings.** Hundreds of labels per frame are hundreds of
calls, each passing the font, brush and format handles. Shape:
`recorder_draw_strings(recorder, font, format, const gd_text_item* items,
uint64_t count)` with `gd_text_item{gd_string_view text; gd_point origin;
gd_color color;}`.

**D4. No recorder reset for reuse.** A view repainting at interaction rate
makes a recorder per frame (`recorder_create`, then `release`);
`recorder_clear` records a clear of the target, it does not empty the
command list. Shape: `recorder_reset(recorder)` emptying the commands and
keeping their storage. *Unverified* that `recorder_close` followed by new
commands is refused.

**D5. Clip semantics are unstated.** The views clip their plots and then
draw headers outside; the C++ Painter has `save`/`clip_rect`/`restore`
with intersecting clips. `recorder_set_clip(recorder, gd_rect)` and
`recorder_reset_clip` do not say whether a clip intersects the current
one or replaces it, nor how it interacts with `recorder_save` tokens.
*Unverified*; shape: say so in the header (intersect, restored by
`recorder_restore`).

Present and useful: `recorder_draw_string_over_fill` (text in one colour,
and another where it lies over a fill) and `contrasting_text_color` would
give labels over hot cells automatic contrast; the C++ Painter has no
equivalent, so the app uses one dark ink. ImPlot-class G7 ("no text in
gd_*") should be re-checked: `recorder_draw_string` is in ABI 0.3.

## Risks to confirm in the first build and run

- R1: F1's x-axis gestures (pan and wheel zoom of x alone): stated and
  tested by `22d9fbaa7`; `set_gesture_axes(x_only)` makes it explicit.
- R2 (*unverified*): `VirtualRowsView::set_selected_row` and `reset_rows`
  raise `selection_changed`; the table guards both, so either way no pick
  is raised for the program's own selection.
- R3 (*unverified*): whether setting a TextBox's text raises
  `text_changed`; the root runs the search itself when a table row is
  activated, and the search workload calls `set_search` after setting the
  text.
- R4 (*unverified*): a second press with `click_count` 2 reaches
  `on_mouse_double_click` after `on_mouse_down`/`on_mouse_up`.
- R5: `std::from_chars` for `double` (JSON numbers) needs the SDK's libc++
  (LLVM 22) and, for the fold check, libstdc++ 11 or later.
- R6: the idle workload expects no repaint without input; nothing in the
  app enlists the motion clock.
- R7: frame-interval budgets need a windowed run; the headless intervals
  are capture costs.
