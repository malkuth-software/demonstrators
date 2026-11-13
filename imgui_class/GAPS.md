# ImGui-class widgets: where the public GUI.Forms API fell short

First written against the cp29 checkpoint (`origin/checkpoint/2026-11-12-cp29`),
installed headers only (`find_package(GUIForms 0.6)`), before any build.
Each entry says what the app needed, what it does instead, the header
concerned and the shape of the API wanted. Entries marked *unverified*
depend on behaviour the headers describe but no run has confirmed. The
gd_* drawing ABI is not used here; its gaps are in `implot_class/GAPS.md`.

| Gap | Status |
|---|---|
| I1 docking | Not attempted: SplitContainer and ExpandableSections stand in |
| I2 no colour picker | Open: `ColorField` in the app |
| I3 no vector editor | Open: `VectorField` composes three ValueBars |
| I4 owner draw is per grid, not per column | Open: every cell is raised; most ask for the default |
| I5 plots in cells | Open: the app strokes its own polyline per cell |
| I6 run-time styling is text only | Open: the app writes a theme sheet |
| I7 compiling a theme needs the window | Open: refused before attachment |
| I8 tree expand and collapse all | Open: one `set_expanded` per node |
| I9 labelled rows | Awkward: `FormStack` in the app |
| I10 ValueBar scrub capture | *unverified* |
| I11 ColorValueEditor alpha | *unverified* |

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
