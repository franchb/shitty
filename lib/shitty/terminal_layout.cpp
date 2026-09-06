/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "terminal_layout.h"

#include <std/alg/minmax.h>
#include <std/dbg/assert.h>

#include <math.h>
#include <limits.h>

using namespace stl;

VtGridSize TerminalLayout::gridSize(u32 width, u32 height, u16 cellWidth, u16 cellHeight) const {
    STD_ASSERT(cellWidth != 0 && cellHeight != 0);
    const u32 borders = 2u * borderPixels;
    return {
        max(1u, (width > borders ? width - borders : 0u) / cellWidth),
        max(1u, (height > borders ? height - borders : 0u) / cellHeight),
    };
}

bool TerminalLayout::resize(u16 width, u16 height, u32 textWidth, u32 textHeight) {
    const u32 borders = 2u * borderPixels;
    const u32 contentWidth = width > borders ? width - borders : 0;
    const u32 contentHeight = height > borders ? height - borders : 0;
    // Center even when the remainder is zero. An odd pixel stays at the
    // trailing edge; neither mouse bounds nor text sizes are derived from it.
    const u16 x = borderPixels + (contentWidth > textWidth ? (contentWidth - textWidth) / 2 : 0);
    const u16 y = borderPixels + (contentHeight > textHeight ? (contentHeight - textHeight) / 2 : 0);
    const bool changed = pixelWidth != width || pixelHeight != height || originX != x || originY != y;
    pixelWidth = width;
    pixelHeight = height;
    originX = x;
    originY = y;
    return changed;
}

VtPointerPosition TerminalLayout::pointerPosition(int x, int y) const {
    return {
        (int)(max<i64>(INT_MIN, (i64)(x)-originX)),
        (int)(max<i64>(INT_MIN, (i64)(y)-originY)),
        x,
        y,
    };
}

int mouseFramebufferCoordinate(double logical, double scale) {
    if (!isfinite(logical) || !isfinite(scale)) {
        return 0;
    }
    const double pixel = logical * max(1.0, scale);
    return (int)(min(max(round(pixel), (double)(INT_MIN)), (double)(INT_MAX)));
}
