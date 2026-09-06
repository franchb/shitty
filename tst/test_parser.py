# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


def observable(terminal):
    snapshot = terminal.snapshot()
    cells = tuple(
        (
            cell.char,
            cell.double_width,
            cell.double_width_continuation,
            cell.bold,
            cell.italic,
            cell.underline,
            cell.inverse,
            cell.wrapped,
            cell.foreground,
            cell.background,
            cell.hyperlink,
        )
        for cell in snapshot.cells
    )
    return (
        snapshot.columns,
        snapshot.rows,
        snapshot.cursor_x,
        snapshot.cursor_y,
        snapshot.cursor_style,
        snapshot.view_offset,
        cells,
        terminal.read_input(),
    )


class ParserStreamingTest(unittest.TestCase):
    def assert_wide_stream_matches_bytewise(self, columns, setup, text):
        # Bytewise UTF-8 takes the scalar placement path. Compare full
        # cells (including graphemes), cursor and retained history, so a
        # batched write cannot hide a lost joiner or a shifted wrap.
        snapshots = []
        for fragmented in (False, True):
            with Shitty(columns=columns, rows=4, save_lines=8) as terminal:
                terminal.write(setup)
                if fragmented:
                    terminal.write_chunks(*(bytes([byte]) for byte in text))
                else:
                    terminal.write(text)
                views = []
                for _ in range(5):
                    snapshot = terminal.snapshot()
                    snapshot.refresh_count = 0
                    views.append(snapshot)
                    terminal.page_up()
                snapshots.append(views)
        self.assertEqual(snapshots[0], snapshots[1])

    def test_wide_runs_match_bytewise_at_row_boundaries(self):
        text = ("a日本語한글👩\u200d💻👍🏽❤️🇬🇧e\u0301一\ufe0eZ" * 4).encode()
        for columns in (1, 2, 3, 7, 8, 80):
            for setup in (b"", b"\x1b[?7l", b"\x1b[4h", b"\x1b[?1049h"):
                with self.subTest(columns=columns, setup=setup):
                    self.assert_wide_stream_matches_bytewise(columns, setup, text)

    def test_wide_runs_match_bytewise_across_line_renditions_and_margins(self):
        text = ("日本語一\u0301👩\u200d💻abcd" * 5).encode()
        for columns in (2, 3, 8, 17):
            setups = (
                b"\x1b#6",
                b"\x1b[2;1H\x1b#6\x1b[H",
                b"\x1b[2;4r\x1b[?6h",
                b"\x1b[?69h\x1b[2;3s\x1b[?6h",
            )
            for setup in setups:
                with self.subTest(columns=columns, setup=setup):
                    self.assert_wide_stream_matches_bytewise(columns, setup, text)

    def test_wide_runs_match_bytewise_across_batch_boundaries(self):
        text = ("一" + "a" * 70 + "二" * 40 + "e\u0301👩\u200d💻" + "b" * 65 + "三\u0301").encode()
        for columns in (3, 7, 80, 132):
            for setup in (b"", b"\x1b[?7l"):
                with self.subTest(columns=columns, setup=setup):
                    self.assert_wide_stream_matches_bytewise(columns, setup, text)

    def test_deferred_clusters_match_bytewise_with_malformed_suffixes(self):
        for cluster in ("一\u0301", "e\u0301", "👩\u200d💻", "❤\ufe0f\ufe0e"):
            for suffix in (b"\xff" + b"a" * 140, b"\xe0\x80\x80" * 70, b"\xf0\x9f"):
                text = cluster.encode() + suffix + "二\u0301Z".encode()
                for columns in (3, 7, 80):
                    with self.subTest(cluster=cluster, suffix=suffix, columns=columns):
                        self.assert_wide_stream_matches_bytewise(columns, b"", text)

    def test_deferred_cluster_width_changes_match_bytewise_over_existing_cells(self):
        text = ("❤\ufe0f\ufe0e一\u0301☀\ufe0e\ufe0f\ufe0e👩\u200d💻Z" * 8).encode()
        for columns in (2, 3, 4, 7, 80):
            for offset in (0, 1):
                setup = ("日本語" * 100).encode() + b"\x1b[H" + b" " * offset
                with self.subTest(columns=columns, offset=offset):
                    self.assert_wide_stream_matches_bytewise(columns, setup, text)

    def test_grapheme_spans_match_bytewise_with_attributes_and_long_clusters(self):
        text = ("日本語 e\u0301 👩\u200d💻 👍🏽 ❤️ 🇬🇧 क्\u200dष " + "一" + "\u0301" * 30 + "Z").encode()
        setups = (
            b"\x1b[1;3;4;31m\x1b]8;;https://example.com\x1b\\",
            b"\x1b[?2027l",
            ("日本語" * 100).encode() + b"\x1b[H",
        )
        for columns in (3, 7, 80, 132):
            for setup in setups:
                with self.subTest(columns=columns, setup=setup):
                    self.assert_wide_stream_matches_bytewise(columns, setup, text * 4)

    def test_grapheme_spans_expose_and_extend_the_last_cluster_at_every_byte_split(self):
        text = "日本語 e\u0301 👩\u200d💻 👍🏽 ❤️ 🇬🇧 一\ufe0e\ufe0fZ".encode()
        for columns in (7, 80):
            for split in range(1, len(text)):
                with self.subTest(columns=columns, split=split):
                    with Shitty(columns=columns, rows=4) as batched, Shitty(columns=columns, rows=4) as scalar:
                        for part in (text[:split], text[split:], "\u0301X".encode()):
                            batched.write(part)
                            scalar.write_chunks(*(bytes([byte]) for byte in part))
                            actual = batched.snapshot()
                            expected = scalar.snapshot()
                            actual.refresh_count = expected.refresh_count = 0
                            self.assertEqual(actual, expected)

    def test_normalized_text_blocks_keep_control_and_error_boundaries(self):
        passive = bytes(byte for byte in range(32) if byte not in range(7, 16) and byte != 27)
        text = b"\xff" + b"".join(
            "一e\u0301👩\u200d💻".encode() + bytes([control]) + "\u0301Z".encode()
            for control in passive
        )
        text += b"\xf0\x9f\x00\x91\xa9\x7f\xe2\x7f\x82\xac"
        text += b"\xff" + b"a" * 61 + b"\xe0\x80\x80\x01" + "\u0301Z".encode()
        for columns in (1, 2, 7, 80):
            for setup in (b"", b"\x1b[4h", b"\x1b[?7l"):
                with self.subTest(columns=columns, setup=setup):
                    self.assert_wide_stream_matches_bytewise(columns, setup, text)

    def test_normalized_text_trace_matches_bytewise_with_passive_controls(self):
        text = b"\xffA\x00B\x01\x80\xc2\x80\xe0\x80\x01\x80\xe2\x7f\x82\xac\x1aZ"
        traces = []
        for fragmented in (False, True):
            with Shitty(columns=80, rows=4) as terminal:
                terminal.parser_trace_on()
                if fragmented:
                    terminal.write_chunks(*(bytes([byte]) for byte in text))
                else:
                    terminal.write(text)
                traces.append(terminal.parser_trace())
        self.assertEqual(traces[0], traces[1])

    def test_backspace_does_not_reuse_the_previous_csi_parameter(self):
        with Shitty(columns=20, rows=2) as terminal:
            terminal.write(b"\x1b[10CX\bY")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(1, 0).char, " ")
            self.assertEqual(snapshot.cell(10, 0).char, "Y")

    def test_control_exposes_normalized_parser_events(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.parser_trace_on()
            terminal.write(
                b"A\x03"
                b"\x1b[?15;2z"
                b"\x1b]27;Something\x1b\\"
            )
            self.assertEqual(
                terminal.parser_trace(),
                [
                    ("text", b"A"),
                    ("control", b"\x03"),
                    ("csi", b"?15;2z"),
                    ("osc", b"27;Something"),
                ],
            )

    def test_escape_inside_osc_aborts_the_string(self):
        # DEC STD 070, xterm, vte, ghostty: an ESC that does not form ST
        # aborts the control string and starts a fresh escape sequence.
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write(b"\x1b]0;A\x1b[31mX\x07")
            terminal.write(b"\x1b[2;1H\x1b[0;31mR")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 0).char, "X")
            self.assertEqual(
                snapshot.cell(0, 0).foreground,
                snapshot.cell(0, 1).foreground,
            )

    def test_escape_inside_dcs_aborts_the_string(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write(b"\x1bP1;2z\x1b[32mY")
            terminal.write(b"\x1b[2;1H\x1b[0;32mG")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 0).char, "Y")
            self.assertEqual(
                snapshot.cell(0, 0).foreground,
                snapshot.cell(0, 1).foreground,
            )

    def test_embedded_line_feed_honours_new_line_mode(self):
        # A C0 LF executed in the middle of a CSI sequence follows the
        # same LNM rules as one in the ground state.
        with Shitty(columns=10, rows=3) as terminal:
            terminal.write(b"\x1b[20habc\x1b[\n1C\x1b[6n")
            self.assertEqual(terminal.read_input(), b"\x1b[2;2R")

    def test_parameter_intermediate_and_final_bytes_are_independent(self):
        with Shitty(columns=6, rows=2) as terminal:
            terminal.write_chunks(b"\x1b[1;", b"2", b"\"", b"q", b"X")
            self.assertTrue(terminal.snapshot().cell(0, 0).protected)

            terminal.write(b"\x1b[1!2pY")
            self.assertEqual(terminal.snapshot().cell(1, 0).char, "Y")

    def test_unknown_multi_intermediate_csi_is_ignored_atomically(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"a\x1b[12 !~b")
            self.assertEqual(terminal.snapshot().lines[0][:2], "ab")

    def test_utf8_graphic_aborts_malformed_csi_and_is_discarded(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b[\xc4\x80a")
            self.assertEqual(terminal.snapshot().lines[0][0], "a")
            self.assertEqual(terminal.parser_trace(), [("text", b"a")])

    def test_cancel_discards_oversized_csi_but_remains_observable(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b[" + b"1;" * 32 + b"1m\x18X")
            self.assertEqual(
                terminal.parser_trace(),
                [("control", b"\x18"), ("text", b"X")],
            )

    def test_invalid_utf8_in_dcs_header_discards_the_control_string(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1bP1 \xc4\x80a\x1b\\X")
            self.assertEqual(terminal.snapshot().lines[0][0], "X")
            self.assertEqual(terminal.parser_trace(), [("text", b"X")])

    def test_st_encodings_remain_observable_outside_control_strings(self):
        cases = (
            (b"\x1b\\", [("escape", b"\\")]),
            (b"\x9c", [("control", b"\x9c")]),
            (b"\x1b\x1b\\", [("escape", b"\\")]),
            (b"\x1b\x9c", [("control", b"\x9c")]),
        )
        for sequence, expected in cases:
            with self.subTest(sequence=sequence):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.parser_trace_on()
                    terminal.write(sequence)
                    self.assertEqual(terminal.parser_trace(), expected)

    def test_seven_bit_c1_form_remains_an_escape_event(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1bD\x84")
            self.assertEqual(
                terminal.parser_trace(),
                [("escape", b"D"), ("control", b"\x84")],
            )

    def test_special_first_intermediate_does_not_end_escape_sequence(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b 0\x1b#!A\x1b%/B")
            self.assertEqual(
                terminal.parser_trace(),
                [
                    ("escape", b" 0"),
                    ("escape", b"#!A"),
                    ("escape", b"%/B"),
                ],
            )

    def test_osc_accepts_mixed_width_terminators_and_legacy_bel_in_single_byte_mode(
        self,
    ):
        cases = (
            b"\x1b]TEST\x1b\\",
            b"\x1b]TEST\x9c",
            b"\x1b]TEST\x07",
            b"\x9dTEST\x1b\\",
            b"\x9dTEST\x9c",
            b"\x9dTEST\x07",
        )
        for sequence in cases:
            with self.subTest(sequence=sequence):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"\x1b%@")
                    terminal.parser_trace_on()
                    terminal.write(sequence)
                    self.assertEqual(terminal.parser_trace(), [("osc", b"TEST")])

    def test_private_prefix_after_numeric_parameters_is_rejected(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"a\x1b[1?25hb")
            self.assertEqual(terminal.snapshot().lines[0][:2], "ab")

    def assert_all_splits_match(self, sequence, suffix=b"X"):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(sequence + suffix)
            expected = observable(terminal)

        chunkings = [
            (sequence[:split], sequence[split:], suffix)
            for split in range(1, len(sequence))
        ]
        chunkings.append(tuple(bytes([byte]) for byte in sequence + suffix))
        for chunks in chunkings:
            with self.subTest(chunks=chunks):
                with Shitty(columns=8, rows=3) as terminal:
                    terminal.write_chunks(*chunks)
                    self.assertEqual(observable(terminal), expected)

    def test_csi_private_prefix_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1b[?1049h\x1b[2;3H")

    def test_csi_greater_than_prefix_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1b[>7u\x1b[2;3H")

    def test_sgr_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1b[1;3;4;38;2;1;2;3m")

    def test_device_attributes_query_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1b[c", suffix=b"")

    def test_osc_bel_terminator_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1b]4;1;?\a", suffix=b"")

    def test_osc_st_terminator_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1b]10;?\x1b\\", suffix=b"")

    def test_dcs_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1bP$qm\x1b\\", suffix=b"")

    def test_escape_restarts_an_incomplete_csi(self):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(b"\x1b[999;\x1b[2;3HX")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(2, 1).char, "X")
            self.assertEqual(snapshot.lines[0], "        ")

    def test_can_and_sub_cancel_incomplete_sequences(self):
        for cancel in (b"\x18", b"\x1a"):
            with self.subTest(cancel=cancel):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"\x1b[31" + cancel + b"X")
                    cell = terminal.snapshot().cell(0, 0)
                    self.assertEqual(cell.char, "X")
                    self.assertEqual(cell.foreground, (255, 255, 255))

    def test_can_cancels_osc_without_emitting_action(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]2;ignored\x18X")
            self.assertEqual(terminal.read_actions(), [])
            self.assertEqual(terminal.snapshot().cell(0, 0).char, "X")

    def test_unknown_csi_intermediates_are_ignored_as_a_unit(self):
        sequences = (
            b"\x1b[?1$p",
            b"\x1b[1;2;3;4$x",
            b"\x1b[1;2&z",
        )
        for sequence in sequences:
            with self.subTest(sequence=sequence):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"A" + sequence + b"B")
                    self.assertEqual(terminal.snapshot().lines[0], "AB      ")

    def test_unknown_string_protocols_are_ignored_through_st(self):
        sequences = (
            b"\x1b_Gi=31;QUJDRA==\x1b\\",
            b"\x1b^private message\x1b\\",
            b"\x1bXstart of string\x1b\\",
        )
        for sequence in sequences:
            with self.subTest(sequence=sequence):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"A" + sequence + b"B")
                    self.assertEqual(terminal.snapshot().lines[0], "AB      ")

    def test_bulk_string_payloads_match_fragmented_input(self):
        payload = b"printable payload " * 256
        sequences = (
            b"\x1b]777;" + payload + b"\x1b\\",
            b"\x1bPz" + payload + b"\x1b\\",
            b"\x1b_" + payload + b"\x1b\\",
        )
        for sequence in sequences:
            with self.subTest(introducer=sequence[:3]):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.parser_trace_on()
                    terminal.write(sequence + b"X")
                    expected_trace = terminal.parser_trace()
                    expected = observable(terminal)

                chunks = []
                offset = 0
                sizes = (1, 7, 31, 127)
                while offset < len(sequence):
                    size = sizes[len(chunks) % len(sizes)]
                    chunks.append(sequence[offset:offset + size])
                    offset += size
                chunks.append(b"X")

                with Shitty(columns=8, rows=2) as terminal:
                    terminal.parser_trace_on()
                    terminal.write_chunks(*chunks)
                    self.assertEqual(terminal.parser_trace(), expected_trace)
                    self.assertEqual(observable(terminal), expected)

    def test_oversized_osc_and_dcs_are_discarded_through_st(self):
        sequences = (
            b"\x1b]2;" + b"x" * (1024 * 1024 + 1) + b"\x1b\\",
            b"\x1bP$q" + b"x" * 5000 + b"\x1b\\",
        )
        for sequence in sequences:
            with self.subTest(sequence=sequence[:3]):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"A" + sequence + b"B")
                    self.assertEqual(terminal.snapshot().lines[0], "AB      ")
                    self.assertEqual(terminal.read_actions(), [])
                    self.assertEqual(terminal.read_input(), b"")

    def test_overflowing_csi_parameters_do_not_leak_as_text(self):
        sequences = (
            b"\x1b[999999999999999999999A",
            b"\x1b[" + b"1;" * 32 + b"1m",
        )
        for sequence in sequences:
            with self.subTest(sequence=sequence[:16]):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"A" + sequence + b"B")
                    self.assertEqual(terminal.snapshot().lines[0], "AB      ")

    def test_eight_bit_c1_sequences_work_in_single_byte_mode(self):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(
                b"\x1b%@"
                b"\x9b2;3HX"
                b"\x9d2;eight bit title\x9c"
                b"\x90$q\"p\x9c"
            )
            snapshot = terminal.snapshot()

            self.assertEqual(snapshot.cell(2, 1).char, "X")
            self.assertEqual(
                terminal.read_actions(),
                ["OSC 2 656967687420626974207469746c65"],
            )
            self.assertEqual(
                terminal.read_input(), b"\x1bP1$r64;1\"p\x1b\\"
            )

    def test_eight_bit_string_protocols_are_ignored_through_st_in_single_byte_mode(self):
        for sequence in (b"\x9fignored\x9c", b"\x9eignored\x9c", b"\x98ignored\x9c"):
            with self.subTest(sequence=sequence):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"\x1b%@A" + sequence + b"B")
                    self.assertEqual(terminal.snapshot().lines[0], "AB      ")


if __name__ == "__main__":
    unittest.main()
