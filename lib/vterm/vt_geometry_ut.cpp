/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vt_geometry.h"

#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(VtGeometry) {
    STD_TEST(ResizeTakesCellsAndCommitsTheirPixelExtent) {
        VtGeometry geometry;
        geometry.setCellPixelSize(8, 16);
        STD_INSIST(geometry.resizeCells(80, 24, nullptr));
        STD_INSIST(geometry.columns == 80 && geometry.rows == 24);
        STD_INSIST(geometry.pixelWidth == 640 && geometry.pixelHeight == 384);
        STD_INSIST(!geometry.resizeCells(80, 24, nullptr));
        geometry.setCellPixelSize(9, 18);
        STD_INSIST(geometry.resizeCells(80, 24, nullptr));
        STD_INSIST(geometry.pixelWidth == 720 && geometry.pixelHeight == 432);
    }
}
