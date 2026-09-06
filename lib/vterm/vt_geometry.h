/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

struct VtHost;

struct VtGridSize {
    u32 columns;
    u32 rows;
};

// Terminal dimensions only. The presenting client determines the grid;
// window bounds, padding and placement do not enter this contract.
struct VtGeometry {
    void setCellPixelSize(u16 width, u16 height);
    // Commit the grid and its exact pixel extent before notifying the host.
    // Returns false when neither the grid nor its pixel extent changed.
    bool resizeCells(u16 columns, u16 rows, VtHost* host);

    u16 columns = 0;
    u16 rows = 0;
    u16 cellPixelWidth = 0;
    u16 cellPixelHeight = 0;
    u32 pixelWidth = 0;
    u32 pixelHeight = 0;
};
