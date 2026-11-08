"""覆盖测试版的版本顺序、构建来源和分平台更新索引。"""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from build_support import version as versions
from artifact_provenance import ProvenanceError
from beta_update_feed import merge_release_map
from build_provenance import SOURCE_ENTRY_WORKFLOWS, SOURCE_REPOSITORY, SOURCE_WORKFLOW, validate_source
from release_metadata import choose_version, publication_state, release_versions
from release_update_map import generate_update_map


SHA = "a" * 40
OTHER_SHA = "b" * 40
ROOT = Path(__file__).resolve().parents[2]


def release(name: str, *, draft=False) -> dict:
    return {"tag_name": "v" + name, "draft": draft, "prerelease": name.endswith("-beta")}


class VersionTests(unittest.TestCase):
    def test_beta_uses_next_revision_after_stable_and_beta(self):
        tags = {"v7.4.1": OTHER_SHA, "v7.4.1.1-beta": OTHER_SHA, "v7.4.1.2": OTHER_SHA}
        actual = choose_version(versions.parse_version("7.4.1"), "beta", tags, [], SHA)
        self.assertEqual(actual.text_small, "7.4.1.3-beta")
        self.assertEqual(actual.update, 70_400_103)
        self.assertEqual(actual.full, 7_004_001)
        self.assertEqual(actual.storage_read, 7_004_001)

    def test_rerun_reuses_published_tag(self):
        tags = {"v7.4.1.1-beta": SHA, "v7.4.1.2-beta": OTHER_SHA}
        actual = choose_version(versions.parse_version("7.4.1"), "beta", tags, [], SHA)
        self.assertEqual(actual.text_small, "7.4.1.1-beta")

    def test_stable_must_follow_beta(self):
        published = release_versions([release("7.4.1.2-beta")])
        with self.assertRaisesRegex(ValueError, "exceed"):
            choose_version(versions.parse_version("7.4.1.2"), "main", {}, published, SHA)
        actual = choose_version(versions.parse_version("7.4.1.3"), "main", {}, published, SHA)
        self.assertGreater(actual.update, published[0][0].update)

    def test_stale_official_baseline_is_rejected(self):
        published = release_versions([release("7.4.2")])
        with self.assertRaisesRegex(ValueError, "baseline"):
            choose_version(versions.parse_version("7.4.1"), "beta", {}, published, SHA)

    def test_revision_exhaustion_cannot_change_official_baseline(self):
        with self.assertRaisesRegex(ValueError, "99 revision"):
            choose_version(versions.parse_version("7.4.1"), "beta", {"v7.4.1.99-beta": OTHER_SHA}, [], SHA)

    def test_drafts_and_feed_do_not_become_published_versions(self):
        data = [release("7.4.1"), release("7.4.1.1-beta", draft=True),
                {"tag_name": "beta-updates", "draft": False, "prerelease": True}]
        self.assertEqual(len(release_versions(data)), 1)

    def test_retry_keeps_public_beta_visible_and_latest_stable_unchanged(self):
        published = release_versions([release("7.4.1.1-beta"), release("7.4.1.2")])
        self.assertEqual(publication_state(versions.parse_version("7.4.1.1-beta"), published),
                         {"published": "true", "latest": "false"})
        self.assertEqual(publication_state(versions.parse_version("7.4.1"), published)["latest"], "false")
        self.assertEqual(publication_state(versions.parse_version("7.4.1.3"), published)["latest"], "true")

    def test_version_file_roundtrip_keeps_storage_and_numeric_file_version(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version_file = root / "version"
            version_file.write_text((ROOT / "Telegram/build/version").read_text(encoding="utf-8"), encoding="utf-8")
            with patch.object(versions, "ROOT", root), patch.object(versions, "VERSION_FILE", version_file), \
                    patch.object(versions, "read_upstream_version", return_value="7.4.1"):
                versions.apply_version(versions.parse_version("7.4.1.3-beta"))
                values = dict(line.split() for line in version_file.read_text().splitlines())
                self.assertEqual(values["AppVersionStrSmall"], "7.4.1.3-beta")
                self.assertEqual(values["AppVersionStrFile"], "7.4.1.3")
                self.assertEqual(values["AppStorageReadVersion"], "7004001")
                self.assertEqual(values["BetaChannel"], "1")
                versions.apply_version(versions.parse_version("7.4.1.4"))
                self.assertNotIn("-beta", version_file.read_text())


class SourceTests(unittest.TestCase):
    def run_request(self, *, branch="beta", version="7.4.1.1-beta", update=70400101, event="push", sha=SHA, workflow=None):
        run = {"id": 123, "run_attempt": 2, "repository": {"full_name": SOURCE_REPOSITORY},
               "head_sha": sha, "head_branch": branch,
               "path": workflow or SOURCE_ENTRY_WORKFLOWS[branch], "event": event}
        fields = {"AppVersionOriginal": "7.4.1", "AppVersionStrSmall": "7.4.1",
                  "AppVersionStr": "7.4.1", "AppUpdateVersion": "70400100", "BetaChannel": "0"}
        with patch("build_provenance._git_head", return_value=SHA), \
                patch("build_provenance._read_version_fields", return_value=fields):
            validate_source(ROOT, repository=SOURCE_REPOSITORY, sha=SHA, ref=f"refs/tags/v{version}",
                            run_id=123, run_attempt=2, version=version, appupdateversion=update,
                            workflow_path=SOURCE_WORKFLOW, run_loader=lambda *args: run)

    def test_beta_and_stable_are_accepted_on_their_branches(self):
        self.run_request()
        self.run_request(branch="main", version="7.4.1", update=70400100)

    def test_branch_event_sha_and_update_code_cannot_be_substituted(self):
        for changes in ({"branch": "main"}, {"event": "pull_request"},
                        {"sha": OTHER_SHA}, {"update": 70400102},
                        {"version": "7.4.2.1-beta", "update": 70400201}):
            with self.subTest(changes=changes), self.assertRaises(ProvenanceError):
                self.run_request(**changes)

    def test_beta_rejects_stable_or_unrelated_workflow_entries(self):
        for workflow in (SOURCE_WORKFLOW, ".github/workflows/untrusted.yml"):
            with self.subTest(workflow=workflow), self.assertRaisesRegex(ProvenanceError, "workflow path"):
                self.run_request(workflow=workflow)

    def test_stable_rejects_beta_workflow_entry(self):
        with self.assertRaisesRegex(ProvenanceError, "workflow path"):
            self.run_request(branch="main", version="7.4.1", update=70400100,
                             workflow=SOURCE_ENTRY_WORKFLOWS["beta"])


class UpdateFeedTests(unittest.TestCase):
    def test_beta_map_contains_only_present_architectures_and_beta_channel(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "tx64upd70400101").write_bytes(b"signed update")
            actual = generate_update_map(root, 70400101, "beta")
            self.assertEqual(set(actual), {"win64"})
            self.assertEqual(set(actual["win64"]), {"beta"})
            self.assertEqual(actual["win64"]["beta"]["released"], 70400101)

    def test_channels_and_architectures_keep_newest_successful_packages(self):
        result = {}
        for name, platform, prefix in (("7.4.1.3-beta", "win64", "tx64upd"),
                                       ("7.4.1.2", "win64", "tx64upd"),
                                       ("7.4.1.1-beta", "winarm", "tarm64upd"),
                                       ("7.4.1.1-beta", "win64", "tx64upd")):
            version = versions.parse_version(name)
            filename = prefix + str(version.update)
            data = {**release(name), "assets": [{"name": filename, "browser_download_url": f"https://example.org/v{name}/{filename}"}]}
            update_map = {platform: {version.channel: {"released": version.update, "testing": version.update, "link": f"/{prefix}{{version}}"}}}
            merge_release_map(result, data, update_map, version)
        self.assertEqual(result["win64"]["beta"]["released"], 70400103)
        self.assertEqual(result["win64"]["stable"]["released"], 70400102)
        self.assertEqual(result["winarm"]["beta"]["released"], 70400101)
        self.assertTrue(result["win64"]["beta"]["link"].startswith("https://"))

    def test_missing_asset_is_not_advertised(self):
        version = versions.parse_version("7.4.1.1-beta")
        update_map = {"win64": {"beta": {"released": version.update, "testing": version.update, "link": "/missing"}}}
        with self.assertRaisesRegex(ValueError, "Missing update asset"):
            merge_release_map({}, {**release(version.text_small), "assets": []}, update_map, version)


if __name__ == "__main__":
    unittest.main()
