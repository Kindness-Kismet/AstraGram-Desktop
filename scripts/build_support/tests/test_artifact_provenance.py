import json
import tempfile
import unittest
from pathlib import Path

from build_support.artifact_provenance import (
    TARGETS,
    ProvenanceError,
    verify_artifacts,
    write_artifact_manifest,
)


VERSION = "7.2.9.20"
APPVERSION = 70200920
SOURCE = {
    "source_repository": "Kindness-Kismet/AstraGram-Desktop",
    "source_ref": f"refs/tags/v{VERSION}",
    "source_sha": "a" * 40,
}
SOURCE_RUN_ID = 100
ARTIFACT_NAMES = {
    "windows-x64": ("win", "tx64upd"),
    "macos-x64": ("macos", "tmacupd"),
}


class VerifyArtifactsTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.build_runs = {}

    def _build(self, key, *, attempt, builder_run_id, run_id=SOURCE_RUN_ID):
        platform, arch = key.split("-")
        archive_platform, updater = ARTIFACT_NAMES[key]
        directory = self.root / key
        directory.mkdir()
        files = [
            directory / f"AstraGram-v{VERSION}-{archive_platform}-{arch}.zip",
            directory / f"{updater}{APPVERSION}",
        ]
        for path in files:
            path.write_bytes(key.encode())
        repository = TARGETS[key]["repository"]
        write_artifact_manifest(
            platform=platform, arch=arch, **SOURCE,
            source_run_id=run_id, source_run_attempt=attempt,
            builder_repository=repository, builder_run_id=builder_run_id, builder_run_attempt=1,
            version=VERSION, appupdateversion=APPVERSION,
            output=directory / f"provenance-{key}.json", files=files,
        )
        self.build_runs[key] = {"repository": repository, "run_id": builder_run_id, "run_attempt": 1}

    def _verify(self, current_attempt):
        verify_artifacts(
            self.root, **SOURCE,
            source_run_id=SOURCE_RUN_ID, source_run_attempt=current_attempt,
            version=VERSION, appupdateversion=APPVERSION,
            build_runs_json=json.dumps(self.build_runs),
        )

    def test_artifacts_from_earlier_attempts_pass(self):
        self._build("windows-x64", attempt=1, builder_run_id=1)
        self._build("macos-x64", attempt=2, builder_run_id=2)
        self._verify(3)

    def test_artifacts_from_later_attempt_fail(self):
        self._build("windows-x64", attempt=2, builder_run_id=1)
        with self.assertRaises(ProvenanceError):
            self._verify(1)

    def test_artifacts_from_another_run_fail(self):
        self._build("windows-x64", attempt=1, builder_run_id=1, run_id=SOURCE_RUN_ID + 1)
        with self.assertRaises(ProvenanceError):
            self._verify(1)


if __name__ == "__main__":
    unittest.main()
