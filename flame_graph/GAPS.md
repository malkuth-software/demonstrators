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
| F1 PlotViewModel has no single-axis mode | Open: every gesture handed over as an x-axis gesture |
| F2 no elided or boxed text | Open: labels cut by an average-width estimate |
| F3 no batched text | Open: one `draw_text_utf8` per label |
| F4 VirtualRowsView has no columns or header | Open: `DetailsTable` paints its own header and column edges |
| F5 row ink not passed to row paint | Open: constants (as node graph N14) |
| F6 plot theme roles unreadable by custom views | Open: constants (as ImPlot-class P5) |
| F7 no categorical or hashed palette | Open: the app hashes names to flamegraph.pl's hot colours |
| F8 tick labels have no unit formatting | Open: plain numbers and a unit tag |
| F9 no tooltip for a region of a custom control | Open: the status line shows the selection instead |
| F10 custom views publish no semantic items | Not attempted |
| F11 no file dialog without host services | Open: `--trace` on the command line (as node graph N8) |
| F12 loading runs on the UI thread | Not attempted |
| D1 no per-rectangle colours | Open (as ImPlot-class G4) |
| D2 `recorder_draw_string` has no layout box | Open: trimming in the string format has nothing to trim to |
| D3 no batched strings | Open |
| D4 no recorder reset for reuse | Open, *unverified* |
| D5 clip semantics unstated | *unverified* |

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

- R1 (*unverified*): F1's x-axis gestures (pan and wheel zoom of x alone).
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
