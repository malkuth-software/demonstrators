# ImGui-class workloads

`imgui-class --measure WORKLOAD [--rows N] [--seed S] [--output-dir DIR]
[--font-dir DIR] [--width W] [--height H]` runs one workload headless: a
window holding the app's root, attached to GUI.Forms' offscreen raster with
the bundled fonts, driven by public calls and dispatched pointer events.
Nothing runs on a clock, so a record repeats for a seed. The record goes
to `DIR/dev/measurements/imgui_class/<host>/<workload>.json`, and the
program exits 1 when an invariant failed.

Each record holds the inputs, every counter (the root's thirty, the colour
field's, the model's advances, the vector field's changes, the visible
rows), the window's `MetricsSnapshot` and GUI.Forms' counted allocations,
then per frame: the capture interval and the deltas of paint passes,
controls painted, damage rectangles, partial and full-window paints,
requested and painted damage area and counted allocations.

| Workload | Procedure | Invariants |
|---|---|---|
| `tour` | Each page in turn, two frames each | every page shown; cell plots and value bars drawn; the colour field painted; the tree holds the model's nodes |
| `widgets` | Five clicks on the counter button; each progress effect in turn | five clicks counted; every effect set |
| `table` | Tables page; filter "ka"; sort by value descending; 30 advances with a frame each; clear the filter | the filter kept a subset; 30 advances; advancing repainted the cell plots, 32 points each; clearing shows every row |
| `scrub` | Values page; drag the plain bar 80 units; drag the vector's x bar 40 units back | the value changed and the release committed; the vector changed and reported it |
| `colour` | Colour page; drag the square from top left to bottom right; drag the hue bar a third down; set a half-transparent red | full saturation, near black; hue near one third; the hex editor follows the field and set_color |
| `style` | Style page; six generated sheets (hue, radius, light or dark); then a broken hand-written sheet | every choice compiled, none refused, the preview has the override; the broken sheet is refused and the override stays |
| `idle` | Trees page, two frames, then 60 unchanged frames | no damage requested |

Frame-interval budgets and the progress effects' animation need a windowed
run on a host; the headless intervals are capture costs.
