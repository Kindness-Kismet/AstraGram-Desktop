import hashlib
import subprocess
import sys
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from build_support import dependency_cache, dependency_runner, toolchain
from build_support.recipe import Stage


class FFmpegToolchainTests(unittest.TestCase):
    def test_default_toolset_reuses_environment_without_compiler_probe(self):
        environment = {"PATH": "original", "VCToolsVersion": "14.51.36111"}
        with mock.patch.object(toolchain, "_upstream_msvc") as load:
            selected, version = toolchain.ffmpeg_arm64_environment(environment)
        self.assertIs(selected, environment)
        self.assertEqual(version, "toolset:14.51.36111")
        load.assert_not_called()

    def test_vcvars_version_key_is_case_insensitive(self):
        environment = {"VCTOOLSVERSION": "14.51.36111\\"}
        with mock.patch.object(toolchain, "_upstream_msvc") as load:
            selected, version = toolchain.ffmpeg_arm64_environment(environment)
        self.assertIs(selected, environment)
        self.assertEqual(version, "toolset:14.51.36111")
        load.assert_not_called()

    def test_affected_compiler_is_selected_only_for_ffmpeg(self):
        environment = {"PATH": "selected", "VCToolsVersion": "14.52.37000"}
        fallback = {"PATH": "fallback", "VCTOOLSVERSION": "14.51.36111"}
        msvc = mock.Mock()
        msvc.ffmpeg_environment.return_value = (fallback, 195136111)
        with mock.patch.object(toolchain, "_upstream_msvc", return_value=msvc):
            selected, version = toolchain.ffmpeg_arm64_environment(environment)
        self.assertIs(selected, fallback)
        self.assertEqual(version, "toolset:14.51.36111")
        self.assertEqual(environment["PATH"], "selected")

    def test_same_toolset_has_same_cache_key_after_fallback(self):
        default = {"VCToolsVersion": "14.51.36111"}
        fallback = {"VCTOOLSVERSION": "14.51.36111\\"}
        msvc = mock.Mock()
        msvc.ffmpeg_environment.return_value = (fallback, 195136111)
        with mock.patch.object(toolchain, "_upstream_msvc", return_value=msvc):
            _, direct_version = toolchain.ffmpeg_arm64_environment(default)
            _, fallback_version = toolchain.ffmpeg_arm64_environment({"VCToolsVersion": "14.52.37000"})
        self.assertEqual(direct_version, fallback_version)
        stage = Stage("ffmpeg", "Libraries", "build")
        with mock.patch.object(dependency_cache, "environment_keys", return_value=("libraries", "tools")):
            self.assertEqual(
                dependency_cache.compute_cache_key(stage, direct_version),
                dependency_cache.compute_cache_key(stage, fallback_version))

    def test_missing_safe_toolset_reports_the_required_version(self):
        msvc = mock.Mock()
        msvc.ffmpeg_environment.side_effect = RuntimeError("Install the 14.51 ARM64 build tools.")
        with mock.patch.object(toolchain, "_upstream_msvc", return_value=msvc):
            with self.assertRaisesRegex(SystemExit, "14.51 ARM64"):
                toolchain.ffmpeg_arm64_environment({"VCToolsVersion": "14.52.37000"})

    def test_upstream_compiler_boundaries_and_path_prefix(self):
        msvc = toolchain._upstream_msvc()
        environment = {
            "PATH": "tools;original",
            "PATH_PREFIX": "tools;",
            "VSINSTALLDIR": "selected-vs",
            "VSCMD_ARG_HOST_ARCH": "arm64",
            "COMSPEC": "cmd.exe",
        }
        for version in (195200000, 195237024):
            with self.subTest(version=version), ExitStack() as stack:
                stack.enter_context(mock.patch.object(msvc, "arm64_version", side_effect=[version, 195136111]))
                stack.enter_context(mock.patch.object(msvc.pathlib.Path, "is_file", return_value=True))
                run = stack.enter_context(mock.patch.object(msvc.subprocess, "run", return_value=subprocess.CompletedProcess(
                    [], 0, "PATH=fallback\nVCToolsVersion=14.51.36111\n", "")))
                selected, actual = msvc.ffmpeg_environment(environment)
                self.assertEqual(actual, 195136111)
                self.assertEqual(selected["PATH"], "tools;fallback")
                self.assertEqual(run.call_count, 1)
        for version in (195199999, 195237025):
            with self.subTest(version=version), ExitStack() as stack:
                stack.enter_context(mock.patch.object(msvc, "arm64_version", return_value=version))
                run = stack.enter_context(mock.patch.object(msvc.subprocess, "run"))
                selected, actual = msvc.ffmpeg_environment(environment)
                self.assertIs(selected, environment)
                self.assertEqual(actual, version)
                run.assert_not_called()


class FFmpegStageTests(unittest.TestCase):
    def test_cache_key_tracks_actual_compiler_and_preserves_other_stages(self):
        stage = Stage("ffmpeg", "Libraries", "build")
        with mock.patch.object(dependency_cache, "environment_keys", return_value=("libraries", "tools")):
            old = hashlib.sha1("libraries;Libraries;ffmpeg;0;build".encode()).hexdigest()
            self.assertEqual(dependency_cache.compute_cache_key(stage), old)
            safe = dependency_cache.compute_cache_key(stage, "195136111")
            fixed = dependency_cache.compute_cache_key(stage, "195237025")
        self.assertNotEqual(safe, old)
        self.assertNotEqual(safe, fixed)

    def test_only_arm64_ffmpeg_receives_selected_environment_and_key(self):
        stages = [Stage(name, "Libraries", "build") for name in ("openssl3", "ffmpeg", "zlib")]
        original = {"PATH": "original"}
        fallback = {"PATH": "fallback"}
        for target in ("win64", "winarm64"):
            with self.subTest(target=target), ExitStack() as stack:
                stack.enter_context(mock.patch.object(dependency_runner, "TARGET", target))
                stack.enter_context(mock.patch.object(dependency_runner, "ensure_key_directories"))
                stack.enter_context(mock.patch.object(dependency_runner, "prune_obsolete", return_value=[]))
                stack.enter_context(mock.patch.object(dependency_runner, "build_environment", return_value=original))
                select = stack.enter_context(mock.patch.object(dependency_runner, "ffmpeg_arm64_environment", return_value=(fallback, "195136111")))
                key = stack.enter_context(mock.patch.object(dependency_runner, "compute_cache_key", return_value="cache"))
                stack.enter_context(mock.patch.object(dependency_runner, "check_cache_key", return_value="Stale"))
                stack.enter_context(mock.patch.object(dependency_runner, "clear_cache_key"))
                stack.enter_context(mock.patch.object(dependency_runner, "write_cache_key"))
                stack.enter_context(mock.patch.object(dependency_runner, "stage_directory", return_value=mock.Mock()))
                execute = stack.enter_context(mock.patch.object(dependency_runner, "_execute", return_value=True))
                self.assertEqual(dependency_runner.run_stages(stages, {}, [], False), 3)
                versions = [call.args[1] for call in key.call_args_list]
                environments = [call.args[2] for call in execute.call_args_list]
                self.assertIs(environments[0], original)
                self.assertIs(environments[2], original)
                if target == "winarm64":
                    self.assertEqual(versions, [None, "195136111", None])
                    self.assertIs(environments[1], fallback)
                    select.assert_called_once_with(original)
                else:
                    self.assertEqual(versions, [None, None, None])
                    self.assertIs(environments[1], original)
                    select.assert_not_called()


if __name__ == "__main__":
    unittest.main()
