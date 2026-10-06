import argparse
import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import upstream
from build_support.upstream_releases import Release, parse_releases, pick, select_target


RELEASES = [
    Release("7.2.9", "a" * 40, "2026-09-17"),
    Release("7.2.10", "b" * 40, "2026-09-27", "beta"),
    Release("7.3.0", "c" * 40, "2026-10-10"),
]


class UpstreamReleasesTests(unittest.TestCase):
    def test_tag_subjects_and_numeric_order(self):
        raw = "\n".join([
            "v7.2.10\tb\tBeta version 7.2.10.\t2026-09-27",
            "v7.2.9\ta\tVersion 7.2.9.\t2026-09-17",
            "v7.2.11\tc\tUnrelated commit\t2026-09-28",
            "v7.2.12\td\tVersion 7.2.11.\t2026-09-29",
            "v7.2.13-extra\te\tVersion 7.2.13.\t2026-09-30",
        ])
        items = parse_releases(raw)
        self.assertEqual([(x.version, x.channel) for x in items], [("7.2.9", "stable"), ("7.2.10", "beta")])

    def test_stable_default_and_explicit_beta(self):
        items = RELEASES[:2]
        self.assertEqual(select_target(items, None, "stable"), RELEASES[0])
        self.assertEqual(select_target(items, None, "beta"), RELEASES[1])
        with self.assertRaises(SystemExit):
            select_target(items, "7.2.10", "stable")
        self.assertEqual(select_target(items, "7.2.10", "beta"), RELEASES[1])

    def test_beta_baseline_can_advance_to_stable(self):
        self.assertEqual(pick(RELEASES, "7.2.10"), RELEASES[1])
        self.assertEqual(select_target(RELEASES, None, "stable"), RELEASES[2])
        self.assertEqual(select_target(RELEASES, None, "beta"), RELEASES[2])

    def test_stable_check_from_newer_beta_does_not_downgrade(self):
        with patch.object(upstream, "load", return_value={"tdesktop": "7.2.10"}), \
                patch.object(upstream, "fetch_releases", return_value=RELEASES[:2]), \
                patch.object(upstream, "git") as git, \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(upstream.check(argparse.Namespace(channel="stable")), 0)
        git.assert_not_called()
        self.assertIn("baseline stays at 7.2.10", output.getvalue())

    def test_done_records_channel_and_preserves_rules(self):
        with tempfile.TemporaryDirectory() as directory:
            tracking = Path(directory) / "upstream.json"
            tracking.write_text(json.dumps({"tdesktop": "7.2.9", "skip": {"docs/": "local"}}))
            with patch.object(upstream, "TRACKING", tracking), \
                    patch.object(upstream, "fetch_releases", return_value=RELEASES), \
                    patch.object(upstream, "git", return_value="dev\n"), \
                    patch.object(upstream, "lagging", return_value=[]), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(upstream.done(argparse.Namespace(version="7.2.10", channel="beta")), 0)
                self.assertEqual(json.loads(tracking.read_text())["tdesktop_channel"], "beta")
                self.assertEqual(upstream.done(argparse.Namespace(version="7.3.0", channel="stable")), 0)
            result = json.loads(tracking.read_text())
            self.assertEqual(result["tdesktop_channel"], "stable")
            self.assertEqual(result["skip"], {"docs/": "local"})

    def test_done_rejects_beta_on_main_and_lagging_submodules(self):
        with patch.object(upstream, "load", return_value={"tdesktop": "7.2.9"}), \
                patch.object(upstream, "fetch_releases", return_value=RELEASES), \
                patch.object(upstream, "git", return_value="main\n"), \
                patch.object(upstream, "lagging") as lagging:
            with self.assertRaises(SystemExit):
                upstream.done(argparse.Namespace(version="7.2.10", channel="beta"))
            lagging.assert_not_called()
        with patch.object(upstream, "load", return_value={"tdesktop": "7.2.9"}), \
                patch.object(upstream, "fetch_releases", return_value=RELEASES), \
                patch.object(upstream, "lagging", return_value=["lib_ui is behind"]), \
                patch.object(upstream, "TRACKING") as tracking, \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(upstream.done(argparse.Namespace(version="7.3.0", channel="stable")), 1)
            tracking.write_text.assert_not_called()


if __name__ == "__main__":
    unittest.main()
