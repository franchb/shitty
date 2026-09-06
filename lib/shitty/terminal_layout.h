/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <lib/vterm/vt_pointer.h>
#include <lib/vterm/vt_geometry.h>

// The terminal's placement within the window's framebuffer. Renderers
// and the input adapter share this transform; the VT core never sees it.
struct TerminalLayout {
    VtGridSize gridSize(u32 width, u32 height, u16 cellWidth, u16 cellHeight) const;
    bool resize(u16 width, u16 height, u32 textWidth, u32 textHeight);
    VtPointerPosition pointerPosition(int x, int y) const;

    u16 pixelWidth = 0;
    u16 pixelHeight = 0;
    u16 borderPixels = 0;
    u16 originX = 0;
    u16 originY = 0;
};

// Logical window coordinates to framebuffer pixels, before placement.
int mouseFramebufferCoordinate(double logical, double scale);
