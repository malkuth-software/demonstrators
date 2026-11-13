# ImGui-class widgets: where the public GUI.Forms API fell short

First written against the cp29 checkpoint (`origin/checkpoint/2026-11-12-cp29`),
installed headers only (`find_package(GUIForms 0.6)`), before any build.
Each entry says what the app needed, what it does instead, the header
concerned and the shape of the API wanted. Entries marked *unverified*
depend on behaviour the headers describe but no run has confirmed. The
gd_* drawing ABI is not used here; its gaps are in `implot_class/GAPS.md`.

| Gap | Status |
|---|---|
| I1 docking | Partly a misreading: docked-only works without windows; note below |
| I2 no colour picker | Open: needs a design (owner decision); note below |
| I3 no vector editor | Open: note below |
| I4 owner draw is per grid, not per column | Closed: `DataGridViewColumn::owner_draw`, eb0fd4205 |
| I5 plots in cells | Closed: `paint_sparkline`, d0e9a3c73; no sparkline column kind |
| I6 run-time styling is text only | Open: needs a design (owner decision) |
| I7 compiling a theme needs the window | Open: needs a design (owner decision) |
| I8 tree expand and collapse all | Closed: `expand_all`, `collapse_all`, `expand_to`, d0e9a3c73 |
| I9 labelled rows | Awkward: `FormStack` in the app |
| I10 ValueBar scrub capture | Answered: the press captures, documented in bf76ee6fa |
| I11 ColorValueEditor alpha | Answered: alpha is kept, documented and tested in bf76ee6fa |

**I1. Docking.** `DockWorkspace`, `DockSite` and their transactions are a
large surface (layout model, sites, histories), and floating panels need
`DockWorkspace::attach_windows(ApplicationWindowHandle, ...)`, which a
headless offscreen run has no handle for. The demo does not attempt
docking; the Panels page is a `SplitContainer` with `ExpandableSections`.
Wanted (`controls/dock_workspace.hpp`): a minimal docked-only path, such as
`DockWorkspace::make_simple(std::span<const DockPanelSpec>)` that builds a
main-window layout of tool panels without floating windows, usable before
a host exists.

**I2. No colour picker.** `ColorValueEditor` is a hex field and a swatch;
`RibbonColorPicker` is a palette. An editor's picker (saturation-value
square, hue bar, alpha bar, numeric channels) is the app's `ColorField`
(`src/color_field.*`), drawn with `fill_linear_gradient` and
`fill_checker`. Wanted (`controls/`): `ColorPicker` with a
`PropertyCell<Color> value`, an `alpha_enabled` property, HSV and RGB
channel fields, `value_changed` (previews) and `value_committed`, and the
same anatomy parts for theming.

**I3. No vector editor.** A position or scale is three scrubbed numbers
edited as one value. `VectorField` (`src/vector_field.*`) lays out three
ValueBars and forwards their events, guarding against its own writes.
Wanted: `VectorValueEditor(StableId, std::size_t components)` with labels,
a range per component, `std::span<const double> values()`,
`set_values(std::span<const double>)`, and `values_changed` /
`values_committed`; ideally a linked-scrub mode that moves every
component together.

**I4. Owner draw is per grid.** `DataGridView::draw_mode = owner_draw`
raises `draw_item_content` for every cell, so the app must set
`draw_default` for the three text columns of five, cell by cell. Wanted
(`controls/data_grid_view.hpp`): `DataGridViewColumn::owner_draw` (bool),
so only that column's cells are raised. `ItemContentPaint::index` is the
row position; the app maps it through its own order, which works only
because the grid's sorting is turned off (`not_sortable`) and the app
sorts.

**I5. Plots in cells.** A 32-sample history per row is drawn with
`Painter::stroke_polyline` after the app scales the samples itself; the
`Sparkline` control's drawing is not reachable for a cell, and
`SeriesMarkPainter` needs a `PlotTransform` built from axes per cell.
Wanted: `plots::paint_sparkline(Painter&, Rect, std::span<const double>,
const SparklineStyle&)`, the function `Sparkline` paints with, and a
`DataGridViewColumnKind::sparkline` whose cell value is a series.

**I6. Run-time styling is text only.** Changing an accent colour or a
corner radius at run time means writing a theme sheet as text
(`style_sheet` in `src/demo_root.cpp`), compiling it and setting the
result as a theme override. There is no typed path, so the app must know
role and part names, and a typo is found only by the compiler's
diagnostics. Wanted (`theme/`): a typed `ThemeOverrides` (role colours,
part fills, radii, density) that compiles without text, and the theme
tuner control the study ranked.

**I7. Compiling a theme needs the window.** `compile_theme_package` is on
`ApplicationResources` (or takes an `ImageRegistry&`), which a control
reaches only through `attached_surface_root()`, so a control cannot
prepare its theme before it is attached; the app refuses and counts it
(`style_unattached`). Wanted: `Control::set_theme_sheet(std::string_view)`
compiled when the control is attached (refusal reported then), as
`set_inline_style` already does for one control's own style.

**I8. Expand and collapse all.** `TreeView` has `set_expanded` per id;
the app walks its own node list. Wanted: `TreeView::expand_all()`,
`collapse_all()` and `expand_to(std::string_view id)`.

**I9. Labelled rows.** A caption column beside controls, rows of their
own height, is the commonest editor layout. `GridPanel` can hold it, but
each caption is a separate `Label` placed by row and column. The app's
`FormStack` (`src/form_stack.*`) does it in a few lines. Wanted: a
`FormLayout` panel with `add_row(caption, control, height)`, caption
alignment and a shared caption width.

**I10. ValueBar scrub capture.** The scrub workload drags across the bar
with dispatched pointer events. *Unverified* whether a scrub that leaves
the bar keeps tracking (pointer capture) and whether the host locks or
wraps the pointer for long scrubs (node graph gap 12).

**I11. ColorValueEditor alpha.** The Colour workload sets a half-
transparent colour and reads it back from `ColorValueEditor::value`.
*Unverified* that the editor keeps alpha; the header does not say.

## Closure on GUI.Forms

Closed on GUI.Forms branch `claude/cp32-demonstrator-gaps` (base
`origin/checkpoint/2026-11-12-cp29` at b8e702fa6), not yet integrated or
built. The shas are that branch's commits.

**I4 (eb0fd4205).** `DataGridViewColumn::owner_draw`: that column's cells
raise `draw_item_content` whatever the grid's `draw_mode`. Index mapping
through the app's own order is unchanged.

**I5 (d0e9a3c73).** `paint_sparkline(Painter&, Rect plot,
plots::SeriesSpans<double> values, std::size_t slots, double scale, const
SparklineStyle&, SparklineWorkspace&)` in `controls/sparkline.hpp` is the
function Sparkline paints with. The workspace holds the reusable mark
storage, one per column. A `DataGridViewColumnKind::sparkline` was not
added: a cell's value is text, and a series value is a model change; an
owner-drawn column calling `paint_sparkline` covers the demo.

**I8 (d0e9a3c73).** `TreeView::expand_all`, `collapse_all` and
`expand_to(id)`: one visible rebuild, then one `expansion_changed` per
changed item in model order.

**I10, I11 (bf76ee6fa).** Existing behaviour, now stated in the headers:
ValueBar's press captures the pointer, and the host's pointer lock decides
whether a long scrub is unbounded or stops at the window's edge;
ColorValueEditor keeps alpha (`#RRGGBBAA`, or `#RRGGBB` as opaque).

**I2, I6, I7.** Not built; each needs an owner decision first. I2: a new
stock `ColorPicker` (saturation-value square, hue and alpha bars, channel
fields) needs its parts and look in the design language, or
ColorValueEditor could open one as a drop-down instead. I6: a typed
`ThemeOverrides` is a new public theme surface ahead of the ABI seal.
I7: `set_theme_sheet` compiled at attachment needs a rule for where a
refusal is reported when no caller is waiting.

**I1, docking, against stage DK.** A docked-only workspace already works
without host windows: a `DockSite` bound to a `DockWorkspace` and set as a
`Window`'s root realizes the main site headlessly
(`tests/dock_site_tests.cpp` does this). `attach_windows` is needed only
for floating sites (DK phase D4 over the W0 window layer). What is missing
is convenience, not capability: a one-call layout of tool panels around a
document area from a list of panel specs (register, then show, each with
a default edge), and a header paragraph saying that the docked-only path
needs no `attach_windows`. Neither changes the DK plan.

**I3, vector editor, against the drawing primitives.** Nothing is missing
from the Painter: a vector editor is layout and event forwarding over
existing ValueBars. The gaps are in the controls: no multi-component value
editor with `values()` and `set_values(std::span<const double>)`, one
`values_changed` and `values_committed` for the whole vector, a shared
label column, and a linked scrub that moves every component by the same
delta (ScrubGesture drives one target; a linked mode needs one gesture
fanned to several targets with a single commit).

