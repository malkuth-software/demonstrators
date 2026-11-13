# ImPlot3D-class plots

A GUI.Forms demonstrator (D5 of the exhibits plan): the 3D plots of an
immediate-mode plotting library's demo, built as a retained GUI.Forms
application against the installed SDK's public headers only
(`find_package(GUIForms 0.6)`). It exercises `Plot3DView` and
`plots::Plot3DScene`, GUI.Forms' CPU 3D plotting (surfaces through
stx_conv's CONV interpolation, rasterised by stx_raster3d, both from the
MIT conv-teapot engine). Where the API fell short is recorded in
`GAPS.md`; the measured workloads are in `docs/WORKLOADS.md`. Clean-room:
written from the behaviour and appearance of such demos only, never their
code. The Utah teapot data is not used.

Licence: PolyForm Noncommercial 1.0.0 (repository `LICENSE`). GUI.Forms is MIT.

Status: written, not yet built. Needs a GUI.Forms with `Plot3DView`
(`claude/cp33-plots3d` or later).

## What it shows

One plot with a toolbar and a status line:

- **Scenes:** MATLAB-style peaks; sin(r)/r ripples; a saddle with a NaN
  disc cut out (holes); a helix line through a normal scatter cloud; and
  a mixed scene (the peaks surface, a helix above it, a cloud).
- **Interpolation:** CONV (smooth, through every sample, no overshoot),
  linear (the raw facets) or flat.
- **Stride:** 1, 2, 4, 8 or 16 (matplotlib's rstride and cstride).
- **Panes, grid, orthographic, labels along axes**, and Reset.
- **Interaction** (the owner's choices of 2026-11-13): left drag turns the
  box (turntable), right drag zooms, middle or Shift + left drag pans, the
  wheel zooms about the pointer, a double click resets; arrows turn, Shift
  + arrows pan, + and - zoom, Home resets, P switches the projection.
- **Status line:** the hovered point in matplotlib's coordinate format
  (data items have no tooltips), or the camera's angles and zoom.

`--grid N` sets the surfaces' samples per side (default 96).

## Build

    cmake -S implot3d_class -B build/implot3d_class -G Ninja \
          -DCMAKE_PREFIX_PATH=<GUI.Forms prefix> \
          -DCMAKE_TOOLCHAIN_FILE=<the SDK's LLVM 22 toolchain file>
    cmake --build build/implot3d_class
    ctest --test-dir build/implot3d_class --output-on-failure

The tests are `surface_data_tests` (the data and the command line, no
GUI.Forms), `offscreen_paint_tests` (the window painted through the
offscreen raster) and one `workload_<name>` per workload.

## Layout

| File | What |
|---|---|
| `src/surface_data.*` | the gridded surfaces, helix and cloud (deterministic) |
| `src/plot3d_root.*` | the toolbar, the `Plot3DView` and the status line |
| `src/plot3d_window.*` | the window holding the root |
| `src/workloads.*` | the headless workloads and their invariants |
| `src/measurement.*`, `src/json_writer.*` | the records |
| `src/options.*`, `src/main.cpp` | the command line |
