/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "terminal_layout.h"

#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(TerminalLayout) {
    STD_TEST(ConvertsLogicalCoordinatesToFramebufferPixels) {
        STD_INSIST(mouseFramebufferCoordinate(10.25, 2.0) == 21);
        STD_INSIST(mouseFramebufferCoordinate(-10.25, 2.0) == -21);
        STD_INSIST(mouseFramebufferCoordinate(10.0, 0.5) == 10);
        STD_INSIST(mouseFramebufferCoordinate(__builtin_inf(), 2.0) == 0);
        STD_INSIST(mouseFramebufferCoordinate(10.0, __builtin_nan("")) == 0);
    }

    STD_TEST(TranslationsKeepTextAndLocatorCoordinatesDistinct) {
        TerminalLayout layout;
        layout.borderPixels = 2;
        layout.resize(89, 39, 80, 32);
        const auto first = layout.pointerPosition(4, 3);
        STD_INSIST(first.pixelX == 0 && first.pixelY == 0);
        STD_INSIST(first.locatorPixelX == 4 && first.locatorPixelY == 3);
        const auto outside = layout.pointerPosition(1, 1);
        STD_INSIST(outside.pixelX == -3 && outside.pixelY == -2);
    }
}
