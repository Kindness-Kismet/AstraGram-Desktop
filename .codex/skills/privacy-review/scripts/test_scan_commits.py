"""Exercise main-branch selection, history coverage and path redaction."""

import argparse
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile


SCRIPT = Path(__file__).with_name("scan_commits.py")
WORKSPACE = None


class HistoryAuditTest(unittest.TestCase):
    def setUp(self):
        # 保留隔离仓库供复核，不触碰主仓库和用户资料。
        self.directory = Path(tempfile.mkdtemp(prefix="audit-", dir=WORKSPACE))
        self.repo = self.directory / "repo"
        self.repo.mkdir()
        self.git("init", "--quiet", "--initial-branch=main")
        self.git("config", "user.name", "Privacy Audit Test")
        self.git("config", "user.email", "privacy-audit@example.invalid")
        self.put("README.md", "Initial fixture\n")
        self.base = self.commit()

    def git(self, *args, data=None):
        return subprocess.check_output(
            ["git", "-C", str(self.repo), *args], input=data, stderr=subprocess.PIPE
        )

    def put(self, path, content):
        target = self.repo / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content.encode() if isinstance(content, str) else content)
        self.git("add", "--", path)

    def commit(self, parents=None, message="Create audit fixture"):
        tree = self.git("write-tree").decode().strip()
        if parents is None:
            current = subprocess.run(
                ["git", "-C", str(self.repo), "rev-parse", "--verify", "HEAD"],
                capture_output=True,
            )
            parents = [current.stdout.decode().strip()] if current.returncode == 0 else []
        args = ["commit-tree", tree]
        for parent in parents:
            args.extend(["-p", parent])
        commit = self.git(*args, data=(message + "\n").encode()).decode().strip()
        self.git("update-ref", "HEAD", commit)
        return commit

    def scan(self, base=None, head=None, expected=0):
        before = self.git("status", "--porcelain=v1", "-z")
        refs = self.git("show-ref")
        output = self.directory / "report.json"
        command = [sys.executable, str(SCRIPT), "--repo", str(self.repo),
                   "--base", base or self.base, "--output", str(output)]
        if head:
            command.extend(["--head", head])
        process = subprocess.run(
            command,
            capture_output=True, text=True, encoding="utf-8",
        )
        self.assertEqual(process.returncode, expected, process.stderr)
        self.assertEqual(before, self.git("status", "--porcelain=v1", "-z"))
        self.assertEqual(refs, self.git("show-ref"))
        raw = output.read_text(encoding="utf-8")
        return json.loads(raw), raw + process.stdout + process.stderr

    def test_deleted_paths_and_archive_are_reported_without_values(self):
        path = "Q" + ":/" + "Users/Example/private.txt"
        self.put("private/settings.txt", path + "\n")
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, "w") as zipped:
            zipped.writestr("notes.txt", path)
            zipped.writestr(path, "fixture")
        self.put("sample.tdesktop-theme", archive.getvalue())
        added = self.commit()
        self.git("update-index", "--force-remove", "--", "private/settings.txt", "sample.tdesktop-theme")
        self.commit()
        report, visible = self.scan(expected=1)
        self.assertEqual(report["summary"]["commits"], 2)
        self.assertTrue(any(h["commit"] == added for h in report["content_candidates"]))
        self.assertTrue(any(h["kind"] == "absolute_path" for h in report["content_candidates"]))
        self.assertTrue(any(h["member"] == "notes.txt" for h in report["content_candidates"]))
        self.assertNotIn(path, visible)

    def test_default_uses_main_even_when_a_feature_branch_is_checked_out(self):
        self.put("main.txt", "Ordinary main change\n")
        main = self.commit()
        self.git("symbolic-ref", "HEAD", "refs/heads/feature/audit-fixture")
        self.put("feature.txt", "Q" + ":/" + "Users/Example/private.txt\n")
        feature = self.commit(parents=[main])
        report, _ = self.scan()
        self.assertEqual(report["head"], main)
        self.assertEqual(report["summary"]["commits"], 1)
        self.assertNotIn(feature, {row["commit"] for row in report["commits"]})

    def test_existing_values_are_distinguished_from_new_exposure(self):
        self.put("setting.txt", "Q" + ":/" + "Users/Example/settings\n")
        base = self.commit()
        self.put("setting.txt", "Q" + ":/" + "Users/Example/settings\nNew ordinary value\n")
        self.commit()
        report, _ = self.scan(base=base)
        self.assertTrue(report["content_candidates"])
        self.assertFalse(any(h["introduced"] for h in report["content_candidates"]))

    def test_merge_and_submodule_changes_are_in_the_manifest(self):
        self.put("分组/文件 名.txt", "Side branch fixture\n")
        side = self.commit(parents=[self.base])
        self.git("read-tree", self.base)
        self.put("main.txt", "Main branch fixture\n")
        main = self.commit(parents=[self.base])
        self.git("update-index", "--add", "--cacheinfo", "160000," + self.base + ",dependency")
        self.put("分组/文件 名.txt", "Side branch fixture\n")
        merge = self.commit(parents=[main, side])
        report, _ = self.scan()
        self.assertEqual({c["commit"] for c in report["commits"]}, {main, side, merge})
        self.assertEqual(report["submodules"][0]["new"], self.base)
        self.assertTrue(any(c["path"] == "分组/文件 名.txt" for row in report["commits"] for c in row["files"]))

    def test_oversize_blob_is_a_coverage_gap(self):
        self.put("large.txt", b"x" * (32 * 1024 * 1024 + 1))
        self.commit()
        report, _ = self.scan(expected=2)
        self.assertEqual(report["coverage_gaps"][0]["reason"], "blob_size_limit")

    def test_commit_message_and_utf16_paths_are_redacted(self):
        path = "Q" + ":/" + "Users/Example/settings"
        self.put("settings.txt", path.encode("utf-16"))
        self.commit(message="Fixture " + path)
        report, visible = self.scan(expected=1)
        self.assertTrue(any(h["path"] == "<commit-message>" for h in report["content_candidates"]))
        self.assertTrue(any(h["kind"] == "absolute_path" for h in report["content_candidates"]))
        self.assertNotIn(path, visible)

    def test_unix_and_network_share_paths_are_candidates(self):
        paths = ["/home/" + "example/project", "/mnt/" + "q/project", "/q/" + "project",
                 "/Users/" + "Example/project", "\\\\" + "example-pc" + "\\project\\file"]
        self.put("paths.txt", "\n".join(paths))
        self.commit()
        report, _ = self.scan(expected=1)
        self.assertEqual({hit["line"] for hit in report["content_candidates"]}, {1, 2, 3, 4, 5})

    def test_invalid_ref_does_not_produce_a_success_report(self):
        output = self.directory / "invalid.json"
        process = subprocess.run(
            [sys.executable, str(SCRIPT), "--repo", str(self.repo),
             "--base", "missing-ref", "--output", str(output)], capture_output=True,
        )
        self.assertEqual(process.returncode, 2)
        self.assertFalse(output.exists())

    def test_command_reports_results_without_an_output_file(self):
        self.put("main.txt", "Ordinary change\n")
        head = self.commit()
        process = subprocess.run(
            [sys.executable, str(SCRIPT), "--repo", str(self.repo), "--base", self.base],
            capture_output=True, text=True, encoding="utf-8",
        )
        self.assertEqual(process.returncode, 0, process.stderr)
        report = json.loads(process.stdout)
        self.assertEqual(report["head"], head)
        self.assertEqual(report["summary"]["commits"], 1)
        self.assertEqual(sorted(p.name for p in self.directory.iterdir()), ["repo"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    WORKSPACE = args.workspace.resolve()
    WORKSPACE.mkdir(parents=True, exist_ok=True)
    unittest.main(argv=[sys.argv[0], *remaining])
