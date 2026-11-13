# ImPlot3D-class workloads

`implot3d-class --measure WORKLOAD [--grid N] [--output-dir DIR]
[--font-dir DIR] [--width W] [--height H]` runs one workload headless: a
window holding the app's root, attached to GUI.Forms' offscreen raster with
the bundled fonts, driven by public calls and dispatched pointer and key
events. The record goes to
`DIR/dev/measurements/implot3d_class/<host>/<workload>.json`, and the
program exits 1 when an invariant failed. CTest runs each workload as
`workload_<name>`.

Each record holds the inputs, the root's counters (scene switches,
rebuilds, refused items, hovers, picks, camera changes, status updates),
the plot's scene figures (pictures rendered, meshes built, triangles
drawn, items, labels, camera), the window's `MetricsSnapshot` and
GUI.Forms' counted allocations; then named steps (the scene figures after
a stride, a grid size or a scene, with that frame's capture interval);
then per frame: the capture interval and the deltas of paint passes,
controls painted, damage rectangles, partial and full-window paints,
requested and painted damage area, pictures rendered and counted
allocations.

| Workload | Procedure | Invariants |
|---|---|---|
| `tour` | Each scene in turn, two frames each | every scene rendered a picture; surface scenes drew triangles; no item refused; tick labels laid out |
| `orbit` | Peaks; left drag 240 px across in 60 moves, 120 px up in 32, a middle drag; then two frames at rest | across turns the azimuth only; up changes the elevation; middle moves the target only; camera_changed raised; at most one picture per frame; none at rest |
| `zoom` | Five wheel notches in and five out at the plot's centre; a right drag; focus, P, Home | the wheel zooms in and back exactly; the right drag zooms; P is handled and switches to orthographic; Home resets the zoom and keeps the projection |
| `pick` | Hover the plot's centre; click without moving; move off the plot | a surface hit; hovered raised; the readout fills the status line; the picked height is within 5% of the z range of the peaks function; the click picks once; leaving clears the hover |
| `strides` | Linear peaks at strides 1, 2, 4, 8, 16; then CONV at 16 down to 1 (recorded only) | a coarser linear stride never draws more triangles; 16 draws fewer than 1; each change rebuilds the mesh; no item refused |
| `interpolation` | Linear, flat, CONV | each rebuilds the mesh and draws triangles |
| `resolution` | Grid sides 32, 64, 128, 256, 512 at stride 1 (CONV) | each side is built and drawn; no item refused |
| `idle` | Two frames, then 60 unchanged frames | no damage requested; no picture rendered |

The headless intervals are capture costs: each includes the window's
paint, the picture's render when it was stale (one sample per pixel while
a drag moves it, 2 x 2 samples at rest) and the copy into the capture.
Frame-interval budgets and presentation intervals need a windowed run on
a host. The inline executor is the only one the view uses (GAPS.md P11),
so these figures are single-threaded.
