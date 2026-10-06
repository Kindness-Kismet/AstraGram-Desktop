import json
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

import build_provenance
from build_support import builder, version as versions
from build_support.artifact_provenance import (
    SOURCE_REPOSITORY, TARGETS, ProvenanceError, verify_artifacts, write_artifact_manifest,
)
from release_config import release_config


class ReleaseChannelsTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.version_file = self.root / "Telegram/build/version"
        self.version_file.parent.mkdir(parents=True)
        self.tracking = self.root / ".github/upstream.json"
        self.tracking.parent.mkdir()

    def write_version(self, text, channel="stable"):
        version = versions.parse_version(text)
        self.version_file.write_text("".join(f"{k} {v}\n" for k, v in versions.version_fields(version).items()))
        self.tracking.write_text(json.dumps({"tdesktop": version.text, "tdesktop_channel": channel}))
        return version

    def test_stable_encoding_remains_compatible(self):
        version = versions.parse_version("7.2.9.23")
        self.assertEqual((version.full, version.storage_read, version.update), (7002009, 7002009, 70200923))
        self.assertEqual(version.file_version, "7.2.9.23")
        self.write_version("7.2.9.23")
        config = release_config(self.root, "main")
        self.assertEqual((config["tag"], config["prerelease"], config["make_latest"]), ("v7.2.9.23", "false", "true"))

    def test_beta_iterations_keep_numeric_platform_versions(self):
        first = versions.parse_version("7.2.10.beta")
        next_version = self.write_version("7.2.10.1.beta", "beta")
        self.assertEqual(first.update + 1, next_version.update)
        self.assertEqual(next_version.text_small, "7.2.10.1")
        self.assertEqual(next_version.file_version, "7.2.10.1")
        config = release_config(self.root, "dev")
        self.assertEqual(config["tag"], "v7.2.10.1.beta")
        self.assertEqual(config["publish"], "true")
        self.assertEqual((config["prerelease"], config["make_latest"]), ("true", "false"))

    def test_channel_and_branch_mismatches(self):
        self.write_version("7.2.9.23")
        self.assertEqual(release_config(self.root, "dev")["publish"], "false")
        with self.assertRaises(ValueError):
            release_config(self.root, "feature/test")
        self.write_version("7.2.10.1.beta", "beta")
        with self.assertRaises(ValueError):
            release_config(self.root, "main")
        self.write_version("7.2.10.2", "beta")
        with self.assertRaises(ValueError):
            release_config(self.root, "main")

    def test_inconsistent_version_file_is_rejected(self):
        self.write_version("7.2.10.1.beta", "beta")
        text = self.version_file.read_text()
        for bad in (text.replace("BetaChannel 1", "BetaChannel 0"), text + "BetaChannel 1\n", text.replace("70201001", "70201002")):
            with self.subTest(bad=bad):
                self.version_file.write_text(bad)
                with self.assertRaises(ValueError):
                    release_config(self.root, "dev")

    def test_version_bump_requires_new_code_across_channels(self):
        self.write_version("7.2.10.1.beta")
        with patch.object(versions, "ROOT", self.root), \
                patch.object(versions, "VERSION_FILE", self.version_file), \
                patch.object(versions, "_UPSTREAM_TRACKING", self.tracking):
            with self.assertRaises(ValueError):
                versions.apply_version(versions.parse_version("7.2.10.1"), check_changelog=False)
            versions.apply_version(versions.parse_version("7.2.10.2"), check_changelog=False)
            current = versions.read_version_file(self.version_file)
            self.assertEqual(current.original, "7.2.10.2")
            self.assertFalse(current.beta)
            versions.apply_version(versions.parse_version("7.2.10.3.beta"), check_changelog=False)
            self.assertTrue(versions.read_version_file(self.version_file).beta)

    def test_windows_beta_archive_passes_release_provenance(self):
        version = self.write_version("7.2.10.1.beta", "beta")
        with patch.object(builder, "BUILD_DIR", self.root), \
                patch.object(builder, "read_current_version", return_value=version.original), \
                patch.object(builder, "TARGET_SUFFIX", "x64"):
            directory = builder.output_dir("release")
            directory.mkdir()
            (directory / "AstraGram.exe").write_bytes(b"test binary")
            (directory / "Updater.exe").write_bytes(b"test updater")
            (directory / "tdata").mkdir()
            (directory / "tdata/private").write_bytes(b"must stay local")
            archive = builder.zip_output("release")
        self.assertEqual(archive.name, "AstraGram-v7.2.10.1-win-x64.zip")
        with zipfile.ZipFile(archive) as bundle:
            self.assertEqual(set(bundle.namelist()), {"AstraGram.exe", "Updater.exe"})
        update = self.root / f"tx64upd{version.update}"
        update.write_bytes(b"test update package")
        artifacts = self.root / "artifacts"
        artifacts.mkdir()
        archive = archive.rename(artifacts / archive.name)
        update = update.rename(artifacts / update.name)
        source = dict(source_repository=SOURCE_REPOSITORY, source_sha="a" * 40,
                      source_ref=f"refs/tags/v{version.original}", source_run_id=100, source_run_attempt=1)
        repository = TARGETS["windows-x64"]["repository"]
        write_artifact_manifest(
            platform="windows", arch="x64", **source,
            builder_repository=repository, builder_run_id=200, builder_run_attempt=1,
            version=version.text_small, appupdateversion=version.update,
            output=artifacts / "provenance-windows-x64.json", files=[archive, update],
        )
        verify_artifacts(
            artifacts, **source, version=version.text_small, appupdateversion=version.update,
            build_runs_json=json.dumps({"windows-x64": {"repository": repository, "run_id": 200, "run_attempt": 1}}),
        )

    def test_malformed_and_exhausted_versions(self):
        for value in ("7.2.10.0.beta", "7.2.10.100.beta", "7.2.10.beta.1", "7.02.10.1.beta", "7.2.10.1.beta-extra"):
            with self.subTest(value=value), self.assertRaises(SystemExit):
                versions.parse_version(value)

    def validate_source(self, original, branch, *, source_ref=None, source_version=None, source_sha=None):
        version = self.write_version(original)
        sha = "a" * 40
        run = {"id": 100, "run_attempt": 1, "head_sha": sha, "head_branch": branch,
               "repository": {"full_name": build_provenance.SOURCE_REPOSITORY},
               "path": build_provenance.SOURCE_WORKFLOW, "event": "push"}
        with patch.object(build_provenance, "_git_head", return_value=sha):
            build_provenance.validate_source(
                self.root, repository=build_provenance.SOURCE_REPOSITORY, sha=source_sha or sha,
                ref=source_ref or f"refs/tags/v{original}", run_id=100, run_attempt=1,
                version=source_version or version.text_small, appupdateversion=version.update,
                workflow_path=build_provenance.SOURCE_WORKFLOW, run_loader=lambda *_: run,
            )

    def test_remote_source_allows_only_matching_release_channels(self):
        self.validate_source("7.2.9.23", "main")
        self.validate_source("7.2.10.1.beta", "dev")
        for version, branch in (("7.2.10.1.beta", "main"), ("7.2.9.23", "dev"), ("7.2.10.1.beta", "feature/test")):
            with self.subTest(version=version, branch=branch), self.assertRaises(ProvenanceError):
                self.validate_source(version, branch)

    def test_remote_source_rejects_changed_tag_version_or_sha(self):
        for kwargs in ({"source_ref": "refs/tags/v7.2.10.1"}, {"source_version": "7.2.10.2"}, {"source_sha": "b" * 40}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ProvenanceError):
                self.validate_source("7.2.10.1.beta", "dev", **kwargs)


if __name__ == "__main__":
    unittest.main()
