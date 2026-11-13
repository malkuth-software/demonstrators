# ImPlot-class plots: where the public GUI.Forms API fell short

First written against the cp29 checkpoint (`origin/checkpoint/2026-11-12-cp29`),
installed headers only (`find_package(GUIForms 0.6)`), before any build.
P-numbered entries concern the plots package and controls; G-numbered
entries the gd_* drawing ABI (`gui_forms/drawing_c_api.h`, ABI 0.3), which
is to be sealed. Each entry says what the app needed, what it does
instead, the header concerned and the shape of the API wanted. Entries
marked *unverified* depend on behaviour the headers describe but no run
has confirmed.

| Gap | Status |
|---|---|
| P1 custom plot kinds rebuild the PlotView frame | Closed: `PlotCustomSeries`, 20da664c0 |
| P2 no heatmap or histogram series kind | Closed: `PlotHeatmapSeries`, `PlotHistogramSeries`, 20da664c0 |
| P3 no draggable handles | Open: needs a design (owner decision) |
| P4 no device scale for a control or painter | Answered: `attached_surface_root().scale()`, documented in 20da664c0 |
| P5 theme plot roles unreadable by custom kinds | Closed: `resolve_plot_palette`, `plot_series_color`, 20da664c0 |
| P6 no per-region cursor | Answered: write `cursor` from `on_mouse_move`, documented in 20da664c0 |
| P7 PlotView takes no AxisLink | Closed: `PlotLinkGroup`, 20da664c0 |
| P8 hidden-tab plots keep the motion clock | Answered: an unpainted plot leaves the clock; test in 20da664c0 |
| P9 no deterministic motion-clock drive offscreen | Answered: `Window::poll_frame_schedule`, documented in 20da664c0 |
| P10 `bin_values` adds, does not clear | Closed: `bin_values_into`, 20da664c0 |
| P11 implicit pointer capture | Answered: a primary press captures; test in 20da664c0 |
| G1 gd_* library not installed | Closed: `GUIForms::DrawingCApi`, `GUIForms::DrawingRaster`, c6d025801 |
| G2 no band fill | Closed: `recorder_fill_band` (ABI 0.4), c6d025801 |
| G3 polylines have no gaps | Closed: `recorder_draw_polyline_runs` (ABI 0.4), c6d025801 |
| G4 no per-rectangle colours | Closed: `recorder_fill_rectangles_each` (ABI 0.4), c6d025801 |
| G5 no header for `gdr_initialize` | Closed: `drawing_raster_c_api.h`, c6d025801 |
| G6 no bridge from a recorder to a Painter | Closed in C++: `replay_drawing_commands`, c6d025801; C ABI open (owner decision) |
| G7 no text in gd_* | Retracted: `font_create`, `recorder_draw_string`, `measure_string` exist |

## Plots package

**P1. Custom plot kinds rebuild the frame.** `PlotView` has no paint hook
for a kind it lacks, so the heatmap, histogram and drag plots are a
`Control` (`src/plot_frame.*`) that redoes what `PlotView` already does:
margins, a `PlotViewModel` for pan, zoom and box zoom, `TickGenerator`
ticks with label metrics, `PlotTransform`, grid, axes, readout. Wanted
(`controls/plot_view.hpp`): a custom series item,

    class PlotCustomSeries {  // owned by a PlotSeriesItem of kind custom
    public:
        virtual plots::PlotDataBounds data_bounds() const = 0;
        virtual plots::MarkStatistics paint(Painter&, const plots::PlotTransform&,
                                            plots::SeriesMarkPainter&) = 0;
        virtual std::optional<plots::PickResult> pick(Point device, const plots::PlotTransform&) const;
    };

so a new kind gets the frame, legend, picking and theming for free.

**P2. No heatmap or histogram kinds.** The primitives have
`SeriesMarkPainter::paint_heatmap`, `paint_histogram`, `Colormap` and
`bin_values`, but `PlotSeriesItem::kind` stops at line, stairs, scatter,
shaded, bars and stems. Wanted: `kind="heatmap"` with
`set_grid(std::span<const double>, rows, columns, PlotRange x, PlotRange y)`,
`colormap` and `domain` properties, and a colour-scale item; and
`kind="histogram"` with `set_samples` and `bins` (count, or `auto`).

**P3. No draggable handles.** Drag points and lines are a pure model
(`src/drag_model.*`) and a custom plot. Wanted: `PlotAnnotationItem` kinds
`point_handle`, `vertical_handle`, `horizontal_handle` and `rect_handle`
with a `draggable` property and an event
`handle_moved(const PlotHandleMove&)` carrying the item, the new data
position and the gesture phase (begin, move, commit, cancel), and
`set_handle_order(XOrder)` to keep points in x order.

**P4. No device scale.** A custom control's paint never learns the
device scale, so `PlotTransform::make(..., 1.0)` decimates at one column
per logical unit, coarser than the screen on a 2x display. Wanted
(`painter.hpp`): `double Painter::device_scale() const noexcept`, or the
scale on `Control::attached_surface_root()`.

**P5. Theme plot roles.** `PlotView` reads plot colours (series cycle,
grid, axis, crosshair) from the theme, but a custom kind cannot: the app
hard-codes them, so `set_theme_override` changes the stock plots and not
the custom ones. Wanted (`theme.hpp`): `plots::PlotPalette
resolve_plot_palette(const Control&)` returning the series cycle, grid,
axis, tick label, crosshair and selection colours.

**P6. Per-region cursor.** A drag handle, an axis (pan one axis) and the
plot area want different cursors. `Control` has one cursor. Wanted:
`virtual CursorKind cursor_at(Point local) const` on `Control`, consulted
on pointer move.

**P7. PlotView takes no AxisLink.** `plots::AxisLink` exists, but
`PlotView` neither accepts nor publishes one, so `LinkedPlotGrid`
(`src/linked_axes.*`) listens to each plot's `view_changed`, sets the
peers' ranges and suppresses its own echoes with a guard. Wanted:
`void PlotView::link_x(std::size_t axis, std::shared_ptr<plots::AxisLink>)`
and `link_y`, with the link applying ranges without re-raising
`view_changed` to the setter.

**P8. Hidden tabs keep the motion clock.** A `PlotView` with a live
channel or a signal program asks for ambient frames while attached,
whether or not its tab shows. The app withdraws the sources (empty
values, empty source) on leaving the Live page and restores them on
return, which also loses the history. *Unverified* that withdrawing even
leaves the clock. Wanted: ambient frames stop for controls whose
`effective_visible()` is false, or `PlotView::suspend_sources(bool)` that
keeps the columns.

**P9. Deterministic drive offscreen.** The offscreen raster has no motion
clock, so headless runs call `PlotView::on_ambient_frame(FrameTime)`
directly with synthetic times. That works only because it is public.
Wanted (`capture/offscreen_raster`): `OffscreenAttachment::advance_frames(FrameTime now)`
that runs every subscribed control's ambient frame, as a host would.

**P10. `bin_values` adds to `counts`.** It does not clear them, which the
header says but the name does not. The histogram clears before each
rebin. Wanted: either `bin_values_into` (clears) or a `BinMode::accumulate`
argument.

**P11. Implicit pointer capture.** The custom plots call
`set_pointer_capture` on press. *Unverified* whether a press already
captures, as for stock controls; the header does not say.

## gd_* drawing ABI 0.3

**G1. The library is not installed.** `drawing_c_api.h` is installed but
the `gui_drawing_c_api` library is not, so a consumer of the SDK cannot
link the ABI at all. The export path is off by default and takes a build
tree's library by path (`IMPLOT_CLASS_GD_LIBRARY`). Wanted: an installed,
exported `GUIForms::DrawingCApi` target.

**G2. No band fill.** A shaded plot fills between two curves. gd_* has
`fill_polygon` only, so the export builds one polygon per band (upper
curve forward, lower back), which a NaN split multiplies. Wanted (matching
`Painter::fill_band`):

    gd_result (*recorder_fill_band)(gd_handle recorder, const double* xs,
                                    const double* upper, const double* lower,
                                    uint32_t count, gd_color color);

with NaN in any column breaking the band.

**G3. Polylines have no gaps.** `draw_lines` joins every point; a NaN
point is undefined. The export splits the line at NaN and issues one call
per run. Wanted: a `GD_LINES_BREAK_ON_NAN` flag on `draw_lines`, or the
rule stated that a non-finite point ends a run.

**G4. No per-rectangle colours.** `fill_rectangles` takes one colour, so
bars of several colours take one call each and a heatmap cannot be drawn
as rectangles at all; the export writes the heatmap into a bitmap
(`bitmap_lock`) and draws that. Wanted (matching
`Painter::fill_rects_each`):

    gd_result (*recorder_fill_rectangles_each)(gd_handle recorder, const gd_rect* rects,
                                               const gd_color* colors, uint32_t count);

**G5. No header for `gdr_initialize`.** The raster module's entry point
must be called before the recorder executes into a bitmap, but no
installed header declares it; the export declares
`extern "C" gd_result gdr_initialize(void);` by hand, against the house
rule that foreign entry points take their type from a declaration.
Wanted: `gui_forms/drawing_raster_c_api.h`, or registration through
`gd_get_api_v0`.

**G6. No bridge to a Painter.** A recorder can be executed into a gd
bitmap only. A consumer that draws a plot through gd_* (a C or C# client)
cannot put it into a GUI.Forms control without encoding a PNG and
decoding it as an image. Wanted: `void Painter::execute_recording(gd_handle recorder)`
in C++, and in the C ABI a control paint callback that receives a
recorder.

**G7. No text.** gd_* has no text call, so the export has no tick labels,
title or legend. Wanted: `recorder_draw_text_utf8(recorder, gd_point,
const char*, uint32_t bytes, const gd_font_spec*, gd_color)` and a matching
measure.

## Closure on GUI.Forms

Closed on GUI.Forms branch `claude/cp32-demonstrator-gaps` (base
`origin/checkpoint/2026-11-12-cp29` at b8e702fa6), not yet integrated or
built. The shas are that branch's commits.

**G1 to G5, ABI 0.4 (c6d025801).** `gui_drawing_c_api` installs as
`GUIForms::DrawingCApi` and the raster module as `GUIForms::DrawingRaster`
(components `DrawingCApi` and `DrawingRaster` of `find_package(GUIForms)`),
with the raster fonts in `lib/fonts` (`bin/fonts` on Windows). ABI 0.4
appends three entries to `gd_api_v0`, so a 0.3 caller is unchanged:
`recorder_fill_band(recorder, brush, const gd_point* upper, const gd_point*
lower, uint64_t count)`, `recorder_draw_polyline_runs(recorder, pen, const
gd_point*, uint64_t)` (a NaN coordinate breaks the line) and
`recorder_fill_rectangles_each(recorder, const gd_rect*, const gd_color*,
uint64_t)`. They take a brush handle and a 64-bit count, like the other
batch calls, not the colour and 32-bit count proposed above. Each records
existing command kinds, so replay and the trace are unchanged.
`gui_forms/drawing_raster_c_api.h` declares `gdr_initialize`.

**G6 (c6d025801).** `replay_drawing_commands(commands, Painter&)` in
`controls/drawing_surface_replay.hpp` draws a recording into a Painter and
reports what it could not: text (until the font identity work lands),
ellipses, paths, images, clears, non-solid brushes and transforms other
than translations. A C-ABI paint callback that receives a recorder needs
handles shared between the gd and gf libraries: an owner decision.

**G7.** A misreading: gd_* has text (`font_create`, `recorder_draw_string`,
`measure_string`). The export can carry labels.

**P1, P2, P5, P7, P10 (20da664c0).** `PlotSeriesItem::set_custom` takes a
`PlotCustomSeries` (extent, mark capacity, paint, pick) that PlotView
frames, pans, zooms and picks like its own kinds; `PlotHeatmapSeries` and
`PlotHistogramSeries` are two of them. `resolve_plot_palette` and
`plot_series_color` give custom kinds the theme's plot colours.
`PlotLinkGroup` links the x, y or both axes of several PlotViews.
`plots::bin_values_into` clears before it counts.

**P4, P6, P8, P9, P11 (20da664c0).** Existing behaviour, now stated in the
headers, with tests for P8 and P11.

**P3.** Not built. Draggable handles need a design: whether handles are a
series kind, an overlay item or a PlotView gesture, and how a drag
commits. An owner decision.

