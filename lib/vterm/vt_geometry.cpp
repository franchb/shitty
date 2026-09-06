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

void VtGeometry::resize(u16 pixelWidth_, u16 pixelHeight_, VtHost* host) {
    STD_ASSERT(cellPixelWidth != 0);
    STD_ASSERT(cellPixelHeight != 0);

    const u32 borders = 2u * borderPixels;
    const u32 contentWidth = pixelWidth_ > borders ? pixelWidth_ - borders : 0;
    const u32 contentHeight = pixelHeight_ > borders ? pixelHeight_ - borders : 0;
    const u16 columns_ = (u16)(max<u32>(1, contentWidth / cellPixelWidth));
    const u16 rows_ = (u16)(max<u32>(1, contentHeight / cellPixelHeight));
    const u32 gridWidth = (u32)(columns_)*cellPixelWidth;
    const u32 gridHeight = (u32)(rows_)*cellPixelHeight;
    const u32 spareWidth = contentWidth > gridWidth ? contentWidth - gridWidth : 0;
    const u32 spareHeight = contentHeight > gridHeight ? contentHeight - gridHeight : 0;
    const u16 originX_ = (u16)(borderPixels + spareWidth / 2);
    const u16 originY_ = (u16)(borderPixels + spareHeight / 2);

    if (columns == columns_ && rows == rows_ && pixelWidth == pixelWidth_ && pixelHeight == pixelHeight_ && originX == originX_ && originY == originY_) {
        return;
    }

    columns = columns_;
    rows = rows_;
    pixelWidth = pixelWidth_;
    pixelHeight = pixelHeight_;
    originX = originX_;
    originY = originY_;

    if (host != nullptr) {
        host->resized();
    }
}
