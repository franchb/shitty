# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import os
import re
import signal
import tempfile
import time
import unittest
from pathlib import Path

from harness import ROOT, Shitty, run_startup_failure


EXAMPLE_CONFIG = ROOT / "bin" / "st" / "shitty.toml"


def config_home(directory, text):
    home = Path(directory) / "shitty"
    home.mkdir(parents=True)
    (home / "shitty.toml").write_text(text)
    return directory


def wait_for(expected, read):
    deadline = time.monotonic() + 2
    while True:
        value = read()
        if value == expected:
            return
        if time.monotonic() >= deadline:
            raise AssertionError(f"expected {expected!r}, got {value!r}")
        time.sleep(0.01)


class ConfigFileTest(unittest.TestCase):
    def test_example_config_is_accepted_by_the_application(self):
        result = run_startup_failure(
            extra_arguments=("-config", EXAMPLE_CONFIG, "-version")
        )
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, b"")

        with Shitty(pin_vga=False, extra_arguments=("-config", EXAMPLE_CONFIG)) as terminal:
            options = terminal.options()
            self.assertEqual(options["fontsize"], 15)
            self.assertEqual(options["fg"], 0xD8DEE9)
            self.assertEqual(options["bg"], 0x2E3440)
            self.assertEqual(options["cr"], 0x88C0D0)

    def test_example_config_documents_every_public_cli_option(self):
        listed = set()
        for argument in ("-help", "-listres"):
            result = run_startup_failure(extra_arguments=(argument,))
            self.assertEqual(result.returncode, 0)
            listed.update(
                match.decode()
                for match in re.findall(rb"^  -([^ ]+)", result.stdout, re.MULTILINE)
            )

        documented = {}
        for line in EXAMPLE_CONFIG.read_text().splitlines():
            if not line.startswith("# CLI: -"):
                continue
            syntax, separator, description = line.partition(" — ")
            self.assertEqual(separator, " — ", line)
            self.assertTrue(description, line)
            name = syntax.removeprefix("# CLI: -").split()[0]
            self.assertNotIn(name, documented, f"duplicate documentation for -{name}")
            documented[name] = description

        self.assertSetEqual(set(documented), listed)

    def test_import_loads_other_files_with_the_importer_on_top(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "themes").mkdir()
            (root / "themes" / "base.toml").write_text(
                'border = "7"\n'
                'fontsize = "18"\n'
            )
            (root / "themes" / "accent.toml").write_text(
                'border = "8"\n'
                'fontsize = "19"\n'
            )
            (root / "main.toml").write_text(
                'import = ["themes/base.toml", "themes/accent.toml"]\n'
                'fontsize = "23"\n'
            )
            with Shitty(
                pin_border=False,
                extra_arguments=("-config", root / "main.toml")
            ) as terminal:
                options = terminal.options()
                # A later import overrides an earlier one, and the
                # importing file overrides them all.
                self.assertEqual(options["border"], 8)
                self.assertEqual(options["fontsize"], 23)

    def test_import_nests_and_expands_home(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "inner.toml").write_text('border = "5"\n')
            (root / "middle.toml").write_text(
                'import = "~/inner.toml"\n'
                'fontsize = "21"\n'
            )
            (root / "main.toml").write_text('import = ["middle.toml"]\n')
            with Shitty(
                pin_border=False,
                extra_arguments=("-config", root / "main.toml"),
                extra_environment={"HOME": str(root)},
            ) as terminal:
                options = terminal.options()
                self.assertEqual(options["border"], 5)
                self.assertEqual(options["fontsize"], 21)

    def test_import_of_a_missing_file_fails_startup(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "main.toml"
            config.write_text('import = ["nowhere.toml"]\n')
            result = run_startup_failure(
                extra_arguments=("-config", config)
            )
            self.assertEqual(result.returncode, 255)
            self.assertIn(b"config import: cannot open", result.stdout)

    def test_import_loop_hits_the_depth_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "main.toml"
            config.write_text('import = ["main.toml"]\n')
            result = run_startup_failure(
                extra_arguments=("-config", config)
            )
            self.assertEqual(result.returncode, 255)
            self.assertIn(b"nest deeper", result.stdout)

    def test_option_comes_from_the_default_config_path(self):
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, "fontsize = 33\n")
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 33)

    def test_command_line_beats_the_config(self):
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, "fontsize = 33\n")
            environment = {"XDG_CONFIG_HOME": directory}
            arguments = ("-fontsize", "22")
            with Shitty(extra_environment=environment, extra_arguments=arguments) as terminal:
                self.assertEqual(terminal.font_state()[0], 22)

    def test_explicit_config_path_is_honored(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "own.toml"
            path.write_text("fontsize = 44\n")
            with Shitty(extra_arguments=("-config", path)) as terminal:
                self.assertEqual(terminal.font_state()[0], 44)

    def test_missing_explicit_config_fails_startup(self):
        result = run_startup_failure(extra_arguments=("-config", "/nonexistent/st.toml"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"-config", result.stdout + result.stderr)

    def test_broken_config_warns_but_the_terminal_starts(self):
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, "fontsize = 33\nboldColors = ???\n")
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 33)

    def test_hostile_config_shapes_warn_but_the_terminal_starts(self):
        # Every rejected TOML shape in one file: dotted keys, a list for
        # a scalar option, a scalar for a list option, non-string list
        # entries, inline tables, and an unterminated dollar expansion.
        text = (
            "a.b = 1\n"
            "fontsize = [1, 2]\n"
            "font = 'One Lone Face'\n"
            "font = [1, 2]\n"
            "border = {x = 1}\n"
            "title = 'tail dollar $'\n"
            "fontsize = 34\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, text)
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 34)

    def test_unknown_keys_and_tables_are_ignored(self):
        text = "nonsense = 1\n[section]\nx = 2\n"
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, "fontsize = 33\n" + text)
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 33)

    def test_typed_values_parse_alongside_the_applied_one(self):
        text = "boldColors = false\nborder = 7\ntitle = 'from config'\nfontsize = 27\n"
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, text)
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(pin_border=False, extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 27)

    def test_environment_expands_in_the_config_body(self):
        text = "fontsize = ${SHITTY_TEST_WANTED_SIZE}\ntitle = '${HOME} of ${NO_SUCH_VARIABLE}'\n"
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, text)
            environment = {
                "XDG_CONFIG_HOME": directory,
                "SHITTY_TEST_WANTED_SIZE": "31",
            }
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 31)

    def test_font_list_is_accepted(self):
        text = 'font = ["Test Font", "Fallback Font"]\nfontsize = 21\n'
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, text)
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 21)

    def test_uri_scheme_list_comes_from_the_config(self):
        text = 'uriScheme = ["gemini"]\n'
        control = 2
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, text)
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(columns=64, rows=1, extra_environment=environment) as terminal:
                uri = b"gemini://example.test"
                terminal.write(uri + b" https://example.test")
                terminal.pointer(2 + 4, 2, modifiers=control)
                self.assertEqual(terminal.desktop_state()["icon"], 1)
                terminal.pointer(2 + len(uri) + 5, 2, modifiers=control)
                self.assertEqual(terminal.desktop_state()["icon"], 0)

    def test_sigusr1_reloads_the_snapshot_and_reapplies_vterm_state(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "live.toml"
            path.write_text("fontsize = 17\nborder = 3\ntitle = 'first'\n")
            with Shitty(pin_border=False, extra_arguments=("-config", path)) as terminal:
                self.assertEqual(terminal.font_state()[0], 17)
                self.assertEqual(terminal.window_title(), "first")

                path.write_text(
                    "fontsize = 23\n"
                    "border = 9\n"
                    "title = 'second'\n"
                    "remap = ['ctrl+b=ctrl+d']\n"
                )
                os.kill(terminal.process.pid, signal.SIGUSR1)

                wait_for(23, lambda: terminal.font_state()[0])
                self.assertEqual(terminal.font_state()[-1], 9)
                wait_for("second", terminal.window_title)
                terminal.layout_key("B", "b", "b", modifiers=2)
                self.assertEqual(terminal.read_input(), b"\x04")

                terminal.write(b"\x1b]2;runtime\x07")
                self.assertEqual(terminal.window_title(), "runtime")

                # A reload is an explicit reapplication, not an old/new
                # diff. The unchanged file therefore resets runtime Vterm
                # overrides to the configured snapshot too.
                os.kill(terminal.process.pid, signal.SIGUSR1)
                wait_for("second", terminal.window_title)


    def test_import_accepts_an_absolute_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "inner.toml").write_text('border = "5"\n')
            (root / "main.toml").write_text(
                f'import = ["{root / "inner.toml"}"]\nfontsize = "21"\n'
            )
            with Shitty(
                pin_border=False,
                extra_arguments=("-config", root / "main.toml")
            ) as terminal:
                options = terminal.options()
                self.assertEqual(options["border"], 5)
                self.assertEqual(options["fontsize"], 21)


    def test_a_reload_that_cannot_reopen_the_font_still_applies_geometry(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "live.toml"
            path.write_text("border = 3\n")
            with Shitty(pin_border=False, extra_arguments=("-config", path)) as terminal:
                path.write_text("border = 9\n")
                terminal.fail_next_font_change()
                os.kill(terminal.process.pid, signal.SIGUSR1)
                wait_for(9, lambda: terminal.options()["border"])
                terminal.write(b"still alive")
                self.assertTrue(
                    terminal.snapshot().lines[0].startswith("still alive")
                )


    def test_a_reload_that_fails_to_load_keeps_the_running_config(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "live.toml"
            path.write_text("border = 3\n")
            with Shitty(pin_border=False, extra_arguments=("-config", path)) as terminal:
                path.write_text('import = ["nowhere.toml"]\nborder = 9\n')
                os.kill(terminal.process.pid, signal.SIGUSR1)
                time.sleep(0.3)
                terminal.pump()
                self.assertEqual(terminal.options()["border"], 3)
                path.write_text("border = 7\n")
                os.kill(terminal.process.pid, signal.SIGUSR1)
                wait_for(7, lambda: terminal.options()["border"])


if __name__ == "__main__":
    unittest.main()
