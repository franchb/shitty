/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

struct VtHost;

// The grid geometry the terminal serves to its applications: the cell
// counts, the pixel quantities the protocols report (winsize, XTWINOPS,
// pixel mouse), and nothing an embedder would not have to answer for.
// The embedder owns one and commits window changes through resize(); an
// in-band resize lets the core commit the same way.
struct VtGeometry {
    void setCellPixelSize(u16 width, u16 height);
    // Commits all fields, then tells the host - every terminal behind
    // the window must hear a geometry change, and only the embedder
    // knows them all.
    void resize(u16 pixelWidth, u16 pixelHeight, VtHost* host);

    u16 columns = 0;
    u16 rows = 0;
    // The pixel size of one cell - what the terminal reports to its
    // applications (CSI 16t, winsize, pixel mouse). The embedder derives
    // it from whatever it draws with; the core only serves it.
    u16 cellPixelWidth = 0;
    u16 cellPixelHeight = 0;
    u16 pixelWidth = 0;
    u16 pixelHeight = 0;
    // The border around the text area in physical pixels, precomputed by
    // the embedder - the content scale it folds in is presentation the
    // core never sees.
    u16 borderPixels = 0;
    // Where the grid's first cell starts, in physical pixels: the
    // border, plus half of what the pixel size leaves over beyond whole
    // cells when the embedder asks for the grid centered. A canvas the
    // embedder does not size itself - a full screen, a maximized frame,
    // a compositor's tile - is rarely a whole number of cells, and the
    // leftover reads better split than piled on the right and bottom.
    u16 originX = 0;
    u16 originY = 0;
    bool centerRemainder = false;
};
