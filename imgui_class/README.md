# ImGui-class widgets

A GUI.Forms demonstrator (D3 of the exhibits plan): the breadth of an
immediate-mode toolkit's demo window, built as a retained GUI.Forms
application against the installed SDK's public headers only
(`find_package(GUIForms 0.6)`). There is no immediate-mode layer and no
perpetual redraw: every control is made once, changed by input or by the
app, and repainted where it changed. Where the public API fell short is
recorded in `GAPS.md`; the measured workloads are in `docs/WORKLOADS.md`.
Clean-room: written from the behaviour and appearance of such demos only,
never their code.

Licence: PolyForm Noncommercial 1.0.0 (repository `LICENSE`). GUI.Forms is MIT.

Status: written, not yet built.

## What it shows

Eight tabs:

- **Widgets:** button with a click count, check box, a radio group,
  toggle switch, combo, list box, single and multiline text, numeric
  up-down, a slider driving three progress bars, and the progress bars'
  effect programs (stripes, pulse, laser, woven stripes, bubbles).
- **Trees:** a seeded tree of branches, folders and leaves, expand and
  collapse all, the selection read back.
- **Tables:** a 2,000-row `DataGridView` filtered and sorted by the app,
  advanced one step at a time, with a value bar and a 32-sample plot drawn
  in cells through owner draw.
- **Values:** `ValueBar`s with scrub, soft ranges and units (plain, angle,
  percent, length as a field), a three-component vector editor composed of
  bars, and a `NumericUpDown` with scrub.
- **Colour:** a colour picker (saturation-value square, hue bar, alpha
  over a checkerboard, all Painter gradients) kept in step with GUI.Forms'
  `ColorValueEditor`.
- **Plots:** `Sparkline`s fed by the table's histories and a `PlotView` of
  the visible rows' mean.
- **Panels:** a `SplitContainer` with `ExpandableSections` beside a tree,
  standing in for docking (GAPS.md I1).
- **Style:** a theme sheet generated from an accent hue, a corner radius
  and light or dark (or edited by hand), compiled through the window's
  `ApplicationResources` and set as a preview's theme override; refusals
  and warnings are shown.

## Build

    cmake -S imgui_class -B build/imgui_class -G Ninja \
          -DCMAKE_PREFIX_PATH=<GUI.Forms prefix> \
          -DCMAKE_TOOLCHAIN_FILE=<the SDK's LLVM 22 toolchain file>
    cmake --build build/imgui_class
    ctest --test-dir build/imgui_class --output-on-failure

## Layout

| Path | Contents |
|---|---|
| `src/color_math.*` | HSV, RGB, 8-bit colours and hexadecimal text |
| `src/color_field.*` | The colour picker control |
| `src/vector_field.*` | Three ValueBars as one vector value |
| `src/demo_model.*` | Seeded rows and tree; filter, sort, advance |
| `src/form_stack.*`, `src/grid_page.*` | Labelled rows; the page grid with a tool strip |
| `src/demo_root.*` | The window content: tabs, pages, cell painting, the style sheet |
| `src/measurement.*`, `src/json_writer.*`, `src/workloads.*` | Records and workloads |
| `src/options.*`, `src/demo_window.*`, `src/main.cpp` | Command line, window, entry |
| `tests/` | Colour and model tests; offscreen paint test |
