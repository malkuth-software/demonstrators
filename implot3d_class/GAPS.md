# ImPlot3D-class plots: where the public GUI.Forms API fell short

First written against `claude/cp33-plots3d` (GUI.Forms 456dd8e, stx
321e56c), installed headers only (`find_package(GUIForms 0.6)`), before
any build. Measured against ImPlot3D-class and matplotlib mplot3d
plotting (the D5 conventions). Each entry says what the app needed, what
it does instead, the header concerned and the shape of the API wanted.
Entries marked *unverified* depend on behaviour the headers describe but
no run has confirmed. None is closed yet.

| Gap | Status |
|---|---|
| P1 no user triangle meshes | Open |
| P2 plot kinds missing: wireframe, trisurf, bar3d, contour, quiver, voxels, stem | Open |
| P3 no colour bar or legend | Open |
| P4 no transparency | Open |
| P5 lighting: no specular or two-sided shading | Open |
| P6 no axis limits or aspect modes | Open |
| P7 no shared views and no look_from | Open |
| P8 tick and label placement fixed | Open |
| P9 along-axis labels are a quarter turn only | Open |
| P10 no trackball rotation or touch gestures | Open |
| P11 inline executor only | Open |
| P12 items from C++ only, not forms | Open |
| P13 the registry id is `plot3_d_view` | Open (owner decision) |
| P14 the toolbar's projection box follows camera_changed | Answered in the app; *unverified* |
| P15 no readout format choice | Open |

**P1. No user triangle meshes.** The study lists meshes (user triangles
with per-vertex colour or scalar) as a D5 need; `Plot3DScene` takes
surfaces over grids, lines and scatters only. Wanted
(`plots/plot3d_scene/plot3d_scene.hpp`): `add_mesh(const
Plot3DMeshSpec&)` with vertices, triangles as index triples, an optional
scalar per vertex (colour mapped) or colour, and normals computed when
absent; picking reports the triangle and its barycentrics.

**P2. Plot kinds missing.** matplotlib's `plot_wireframe`,
`plot_trisurf`, `bar3d`, `contour`/`contourf` on a pane, `quiver`,
`voxels` and `stem` have no counterpart. A wireframe is close at hand: the
line pass exists, so `Plot3DSurfaceSpec::style` (filled, wireframe, both)
is the likely shape. Trisurf needs P1. The demonstrator shows surfaces,
lines and scatters only.

**P3. No colour bar or legend.** The 2D package has `plot_legend` and a
colour scale; `Plot3DView` draws neither, so a viewer cannot read heights
off the colours or tell the helix from the cloud except by the status
line. Wanted: `Plot3DView` properties `show_legend` and `show_colorbar`
drawn in the overlay with the 2D package's pieces, the colour bar for the
first colour-mapped surface or a chosen item.

**P4. No transparency.** Panes are translucent, data items are opaque. A
surface through which a scatter shows needs an alpha per item (matplotlib's
`alpha`). Wanted: `std::optional<double> opacity` on the specs, with the
renderer's order-independent rule stated (or "drawn after opaque items,
back to front per item").

**P5. Lighting.** `Plot3DLighting` offers none, fixed and headlight per
surface (the app keeps fixed); the study also lists optional specular and
two-sided shading, which a surface seen from below needs. Wanted: a
specular amount and a two-sided flag on the surface spec, shadows off.

**P6. No axis limits or aspect modes.** The box fits the data
(`Plot3DScene::limits` is read-only) and the aspect is a ratio
(`set_box_aspect`). The app cannot zoom an axis to a range, keep two plots
on one scale, or ask for matplotlib's `equal`, `equalxy` and friends.
Wanted: `set_limits(Plot3DAxis, PlotRange)` with `auto` to fit, and an
aspect mode enum beside the ratio.

**P7. No shared views, no look_from.** Two 3D plots cannot share a camera
except by the app copying it on `camera_changed` (and guarding the echo),
and there is no "view along +x" call (matplotlib's `view_init` presets,
ImPlot3D's axis buttons). Wanted: a `Plot3DLinkGroup` like the 2D
`PlotLinkGroup`, and `look_from(Plot3DAxis, bool positive)`.

**P8. Tick and label placement fixed.** Ticks follow matplotlib's
far-edge rule and the tick generator's counts. The app cannot set tick
values, a format, a label pad, or which edge carries an axis. Wanted:
per-axis tick options shared with the 2D `PlotAxisItem` (values, format,
count), and an edge override.

**P9. Along-axis labels are a quarter turn.** `labels_along_axis` turns a
steep axis's title upright; titles at other angles stay screen-facing,
because the painter draws text at 0 or 90 degrees only. Wanted: rotated
text in the painter (an angle on the text draw call), then titles laid
along their projected edges.

**P10. Rotation styles and touch.** Rotation is turntable only (the owner's
choice for the default). Trackball and arcball are not offered, and
there are no touch gestures (pinch to zoom, two-finger pan). Wanted: a
`rotation_style` property (turntable, trackball, arcball) and the pinch
and pan gestures routed like the wheel and middle drag.

**P11. Inline executor only.** `stx_raster3d::Renderer::render` takes an
executor, but `Plot3DScene::render` always passes the inline one, so a
512 x 512 CONV surface renders on the UI thread alone (the `resolution`
workload records it). Wanted: the application's worker pool as the
scene's executor, with the picture swapped on completion.

**P12. Items from C++ only.** Surfaces, lines and scatters are added by
calls; a form can declare the view and its properties but not its items.
Wanted: item controls like the 2D `PlotSeriesItem` (`Plot3DSurfaceItem`
and so on) with data bound by name.

**P13. The registry id.** The registry's word rule makes `Plot3DView`'s id
`plot3_d_view`. It reads oddly in forms and the ABI; `plot3d_view` would
need a rule exception. Owner decision.

**P14. Projection box.** The P key switches the projection inside the
view; the app keeps its Orthographic box in step from `camera_changed`.
That works because writing the box raises `checked_changed`, which sets
the same camera, which `set_camera` reports as unchanged. *Unverified.*

**P15. Readout format.** The readout is `label: x=…, y=…, z=…` with `%.6g`.
The app cannot choose units, digits or per-axis formats (ImPlot's
`ImPlotFormatter`). Wanted: a formatter per axis, shared with the 2D tick
formats.
