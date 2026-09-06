# Terminal placement and VT geometry

`lib/shitty/terminal_layout.*` owns the framebuffer dimensions, scaled border,
text origin and centering policy. Renderers and session input routing use the
same `Composer::layout`. Changing alignment or adding space around a terminal
belongs here, not in `lib/vterm`.

`VtGeometry` describes the terminal itself: columns, rows, cell pixel sizes and
the exact text pixel extent. `resizeCells()` takes **cell counts**, commits the
extent and notifies the host only when terminal dimensions change. It has no
window origin, padding, alignment or display scale.

## Input coordinates

Platform pointer events contain framebuffer coordinates. `SessionSet` converts
them through `TerminalLayout::pointerPosition()` before calling `Vterm`.
`VtPointerPosition` carries two explicitly separate, zero-based positions:

- `pixelX/Y`: relative to the first text cell. Negative and out-of-range
  positions are preserved for selection dragging; protocol encoders apply
  their own bounds.
- `locatorPixelX/Y`: the presenting client's DEC locator coordinate convention.
  The GUI supplies window pixels, matching xterm. The C embedding API uses its
  existing cell coordinates with a virtual 1x1 cell.

SGR-Pixels uses text-local pixels; DEC locator pixel mode uses the independent
locator position. Do not normalize the two into one coordinate pair.

`pointerRepositioned()` updates a retained pointer after layout changes without
sending a synthetic motion report. The UI also calls it when reactivating a
session that retained a pointer position. Selection, hyperlink lookup and
mouse reporting inside the core only use text-local coordinates.

## Resize and redraw

`Composer::resizeWindow()` computes and commits layout before publishing the
terminal dimensions. Window-only changes go through `layoutChangedListeners`:
they redraw and reproject the pointer, without resizing the PTY, emitting an
in-band resize report or ending synchronized output. A change of cell metrics
still updates the terminal's pixel extent even when cell counts stay the same.

XTWINOPS requests go through `VtHost`. The GUI adapter converts cell requests
to window dimensions, including padding, and commits the resulting geometry.
The headless and C adapters implement their own sizing convention. Window-size
queries use host information; text-size queries use `VtGeometry`.

Integration coverage is in `tst/test_terminal_layout.py`; the resize notification
boundary is also covered by `Composer::SeparatesTerminalResizeFromWindowLayout`.
