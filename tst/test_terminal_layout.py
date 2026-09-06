# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


class TerminalLayoutTest(unittest.TestCase):
    def test_mouse_uses_text_pixels_and_preserves_dec_locator_coordinates(self):
        with Shitty(columns=10, rows=2, glyph_px=8, glyph_py=16) as terminal:
            for fullscreen in (False, True):
                terminal.window_info(
                    pixel_width=89, pixel_height=39, fullscreen=fullscreen,
                )
                self.assertEqual(terminal.winsize_full(), (10, 2, 80, 32))
                # The text starts at (4, 3), independently of window mode.
                for mode, expected in ((1006, b"2;2"), (1016, b"10;18")):
                    terminal.write(f"\x1b[?1003h\x1b[?{mode}h".encode())
                    terminal.pointer(13, 20)
                    self.assertEqual(
                        terminal.read_input(), b"\x1b[<35;" + expected + b"M",
                    )
                    terminal.write(f"\x1b[?{mode}l\x1b[?1003l".encode())
                terminal.write(b"\x1b[1;1'z")
                terminal.pointer(13, 20)
                terminal.write(b"\x1b['|")
                self.assertEqual(terminal.read_input(), b"\x1b[1;0;21;14;0&w")
                terminal.write(b"\x1b[1;2'z\x1b['|")
                self.assertEqual(terminal.read_input(), b"\x1b[1;0;2;2;0&w")

    def test_odd_remainder_is_outside_sgr_pixel_extent(self):
        with Shitty(columns=10, rows=2, glyph_px=8, glyph_py=16) as terminal:
            terminal.resize_pixels(89, 39)
            terminal.write(b"\x1b[?1003h\x1b[?1016h")
            terminal.pointer(84, 35)
            self.assertEqual(terminal.read_input(), b"\x1b[<35;80;32M")
            terminal.pointer(-100, -100)
            self.assertEqual(terminal.read_input(), b"\x1b[<35;1;1M")

    def test_stationary_pointer_rechecks_hyperlink_when_text_moves(self):
        with Shitty(columns=10, rows=2, glyph_px=8, glyph_py=16) as terminal:
            terminal.write(b"\x1b]8;;https://example.test\x1b\\A\x1b]8;;\x1b\\")
            terminal.pointer(2, 2, modifiers=2)
            self.assertNotEqual(terminal.desktop_state()["hovered_hyperlink"], 0)
            terminal.resize_pixels(89, 39)
            self.assertEqual(terminal.desktop_state()["hovered_hyperlink"], 0)
            terminal.resize_pixels(84, 36)
            self.assertNotEqual(terminal.desktop_state()["hovered_hyperlink"], 0)

    def test_layout_updates_locator_without_synthetic_mouse_motion(self):
        with Shitty(columns=10, rows=2, glyph_px=8, glyph_py=16) as terminal:
            terminal.write(b"\x1b[?1003h\x1b[?1016h\x1b[1;2'z")
            terminal.pointer(10, 18)
            terminal.read_input()
            terminal.resize_pixels(89, 39)
            self.assertEqual(terminal.read_input(), b"")
            terminal.write(b"\x1b['|")
            self.assertEqual(terminal.read_input(), b"\x1b[1;0;1;1;0&w")
            terminal.write(b"\x1b[1;1'z\x1b['|")
            self.assertEqual(terminal.read_input(), b"\x1b[1;0;19;11;0&w")

    def test_reactivated_session_uses_current_placement(self):
        with Shitty(columns=10, rows=2, glyph_px=8, glyph_py=16) as terminal:
            terminal.pointer_presence(True)
            terminal.write(b"\x1b[1;2'z")
            terminal.pointer(10, 18)
            terminal.new_session()
            terminal.resize_pixels(89, 39)
            terminal.chord_prev_tab()
            terminal.write(b"\x1b['|")
            self.assertEqual(terminal.read_input(), b"\x1b[1;0;1;1;0&w")

    def test_drag_keeps_window_pointer_when_text_origin_changes(self):
        with Shitty(columns=10, rows=2, glyph_px=8, glyph_py=16) as terminal:
            terminal.write(b"abcd")
            terminal.button(0, True, x=2, y=2, time=1)
            terminal.pointer(26, 2)
            terminal.resize_pixels(88, 36)
            # The stationary x=26 is now inside c, rather than at d.
            self.assertEqual(terminal.select_finish(), b"ab")

    def test_rendering_and_hit_testing_share_the_text_origin(self):
        with Shitty(columns=10, rows=2, glyph_px=8, glyph_py=16) as terminal:
            terminal.write(b"\x1b[?25l\x1b[41m \x1b[0m")
            terminal.resize_pixels(89, 39)
            self.assertEqual(terminal.presented_pixel(3, 3), (0, 0, 0))
            self.assertEqual(terminal.presented_pixel(4, 3), (170, 0, 0))
            self.assertEqual(terminal.presented_pixel(11, 18), (170, 0, 0))
            self.assertEqual(terminal.presented_pixel(12, 18), (0, 0, 0))


if __name__ == "__main__":
    unittest.main()
