# ImPlot-class workloads

`implot-class --measure WORKLOAD [--large-points N] [--output-dir DIR]
[--font-dir DIR] [--width W] [--height H]` runs one workload headless: a
window holding the app's root, attached to GUI.Forms' offscreen raster with
the bundled fonts, driven by public calls and dispatched pointer events.
The live feed's thread is not started; the Live workload writes blocks
itself and calls the live plot's `on_ambient_frame` at fixed 60 Hz times,
so records repeat (GAPS.md P9). The record goes to
`DIR/dev/measurements/implot_class/<host>/<workload>.json`, and the program
exits 1 when an invariant failed.

Each record holds the inputs, every counter (the root's, the three custom
plots' frame statistics, the feed, the channel, the live plot's frames and
column copies, the subplots' propagations and echoes, the histogram's
rebins), the window's `MetricsSnapshot` and GUI.Forms' counted allocations,
then per frame: the capture interval and the deltas of paint passes,
controls painted, damage rectangles, partial and full-window paints,
requested and painted damage area and counted allocations.

| Workload | Procedure | Invariants |
|---|---|---|
| `tour` | Each page in turn, two frames each | every page shown; heatmap, histogram and drag plot painted; leaving Live withdrew its sources |
| `large-pan` | Large page; a tenth-wide x range panned across the 1e6-point line in 120 frames | every range taken; the last is the series' end; pan frames repaint less than the window |
| `heatmap` | Four colormaps, then bin counts 10 to 200; hover the heatmap's centre | every bin count rebinned; the hover names a cell; one frame draws at most one rectangle per cell (plus the scale) |
| `drag` | Drag the second point 30, 40 units; drag the vertical line 50 units | the point and the line moved; the moves were reported |
| `live` | Live page; 120 frames, one block (32 samples) each, ambient frame at 60 Hz times; then leave the page | the plot consumed the feed with no overrun; ambient frames ran; leaving paused the feed |
| `link` | Subplots page; 30 x ranges set on the plots in turn; then link y and set one y range | every plot shows the linked x range; peers were set and echoes ignored; y follows |
| `idle` | Basics page, two frames, then 60 unchanged frames | no damage requested |

Frame-interval budgets, presentation intervals and the live plot's wakes
on a hidden tab need a windowed run on a host; the headless intervals are
capture costs.
