/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vt_geometry.h"

#include "vt_host.h"

#include <std/alg/minmax.h>
#include <std/dbg/assert.h>

using namespace stl;

void VtGeometry::setCellPixelSize(u16 width, u16 height) {
    STD_ASSERT(width != 0);
    STD_ASSERT(height != 0);
    if (cellPixelWidth == width && cellPixelHeight == height) {
        return;
    }
    cellPixelWidth = width;
    cellPixelHeight = height;
}

bool VtGeometry::resizeCells(u16 columns_, u16 rows_, VtHost* host) {
    STD_ASSERT(cellPixelWidth != 0);
    STD_ASSERT(cellPixelHeight != 0);
    columns_ = max<u16>(1, columns_);
    rows_ = max<u16>(1, rows_);
    const u32 pixelWidth_ = (u32)(columns_)*cellPixelWidth;
    const u32 pixelHeight_ = (u32)(rows_)*cellPixelHeight;
    if (columns == columns_ && rows == rows_ && pixelWidth == pixelWidth_ && pixelHeight == pixelHeight_) {
        return false;
    }
    columns = columns_;
    rows = rows_;
    pixelWidth = pixelWidth_;
    pixelHeight = pixelHeight_;
    if (host != nullptr) {
        host->resized();
    }
    return true;
}
