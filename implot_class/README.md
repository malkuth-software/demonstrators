# ImPlot-class plots

A GUI.Forms demonstrator (D4 of the exhibits plan): the breadth of an
immediate-mode plotting library's demo, built as a retained GUI.Forms
application against the installed SDK's public headers only
(`find_package(GUIForms 0.6)`). It exists to stress the plots package, the
drawing primitives and the gd_* drawing ABI before that ABI is sealed;
where they fell short is recorded in `GAPS.md`, and the measured workloads
are in `docs/WORKLOADS.md`. Clean-room: written from the behaviour and
appearance of such demos only, never their code.

Licence: PolyForm Noncommercial 1.0.0 (repository `LICENSE`). GUI.Forms is MIT.

Status: written, not yet built.

## What it shows

Six tabs:

- **Basics:** `PlotView` kinds through declared items: a line with an
  annotation and a crosshair, a scatter, bars with stems, a shaded band
  under a line, stairs, and a log10 y axis.
- **Large:** a 1,000,000-point random walk copied in (the plot's envelope
  pyramid decimates it), beside a 1,000,000-point sine read in place under
  a `SeriesLease`, and a 1,000,000-point scatter. Pan with a drag, zoom
  with the wheel or a box.
- **Heatmap:** a custom `HeatmapPlot` (200 x 200 cells through
  `SeriesMarkPainter::paint_heatmap`, a colour scale, hover readout, a
  colormap chooser) and a custom `HistogramPlot` (100,000 samples binned
  with `bin_values`, bin count on a track bar).
- **Drag:** a custom `DragPlot`: drag points along a curve (kept in x
  order), a vertical and a horizontal line, with a readout.
- **Live:** a streaming `PlotView` fed by a producer thread through a
  `SeriesRingChannel`, following the last ten seconds, beside two signal
  program series; pause, and the sub-surface switch.
- **Subplots:** four `PlotView`s with linked x axes (and y on request),
  linked by the app through `view_changed` (GAPS.md P7).

`--export-png FILE` draws a line, a band, markers, bars and a heatmap
through the gd_* C ABI into a bitmap and writes it as PNG. It needs
`-DIMPLOT_CLASS_GD_LIBRARY=...`, because the SDK does not install the gd_*
library (GAPS.md G1).

The custom plots share `PlotFrame` (`src/plot_frame.*`): a `Control` over
`PlotViewModel`, `PlotTransform`, `TickGenerator` and `SeriesMarkPainter`,
which is what any plot kind the package lacks has to rebuild (GAPS.md P1).

## Build

    cmake -S implot_class -B build/implot_class -G Ninja \
          -DCMAKE_PREFIX_PATH=<GUI.Forms prefix> \
          -DCMAKE_TOOLCHAIN_FILE=<the SDK's LLVM 22 toolchain file>
    cmake --build build/implot_class
    ctest --test-dir build/implot_class --output-on-failure

Optional: `-DIMPLOT_CLASS_GD_LIBRARY=<build tree>/libgui_drawing_c_api.<ext>`
and, when the raster module is separate,
`-DIMPLOT_CLASS_GD_RASTER_LIBRARY=<its library>`.

## Layout

| Path | Contents |
|---|---|
| `src/series_data.*` | Seeded generators: random walk, sine, normal cloud, heatmap field, mixture |
| `src/plot_frame.*` | The custom plots' base: axes, ticks, pan and zoom, readout, statistics |
| `src/heatmap_plot.*`, `src/histogram_plot.*`, `src/drag_plot.*`, `src/drag_model.*` | Plot kinds the package lacks |
| `src/linked_axes.*`, `src/grid_page.*` | Linked subplots; the page grid with a tool strip |
| `src/live_feed.*` | The producer thread and its headless block writer |
| `src/plot_root.*` | The window content: tabs, pages, status line |
| `src/gd_export.*` | The gd_* export path |
| `src/measurement.*`, `src/json_writer.*`, `src/workloads.*` | Records and workloads |
| `src/options.*`, `src/plot_window.*`, `src/main.cpp` | Command line, window, entry |
| `tests/` | Data, drag model and live feed tests; offscreen paint test |
