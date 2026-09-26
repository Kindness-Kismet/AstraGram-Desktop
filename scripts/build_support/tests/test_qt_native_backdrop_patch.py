import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from build_support import qt_native_backdrop_patch as backdrop
from build_support.dependency_cache import compute_cache_key
from build_support.recipe import Stage
from build_support import recipes, recipes_arm64


def fixture(alpha: str, newline: bytes = b"\n") -> bytes:
    return (backdrop._INCLUDE + backdrop._CONSTRUCTOR
            + backdrop._PREFIX + alpha + backdrop._SUFFIX + "\n").encode().replace(b"\n", newline)


class NativeBackdropPatchTests(unittest.TestCase):
    def test_both_shapes_and_line_endings_are_idempotent(self):
        for alpha in backdrop._ALPHA:
            for newline in (b"\n", b"\r\n"):
                with self.subTest(alpha=alpha, newline=newline):
                    source = fixture(alpha, newline)
                    result = backdrop.patch_source(source)
                    self.assertEqual(backdrop.patch_source(result), result)
                    self.assertIn(b'GetPropW(hwnd, L"AyuGramNativeBackdrop")', result)
                    self.assertIn(b"Qt::WindowTransparentForInput", result)
                    self.assertIn(b"|| opacity < 1.0;", result)
                    self.assertIn(backdrop._CAPABILITY.encode().replace(b"\n", newline), result)
                    if newline == b"\r\n":
                        self.assertNotIn(b"\n", result.replace(b"\r\n", b""))

    def test_existing_qvariant_include_is_preserved(self):
        source = fixture("hasAlpha") + (backdrop._VARIANT + "\n").encode()
        result = backdrop.patch_source(source)
        self.assertEqual(result.count(backdrop._VARIANT.encode()), 1)
        self.assertEqual(backdrop.patch_source(result), result)

    def test_missing_duplicate_or_drifted_anchors_fail(self):
        source = fixture("hasAlpha")
        for broken in (
            source.replace(backdrop._CONSTRUCTOR.encode(), b""),
            source + backdrop._CONSTRUCTOR.encode(),
            source + (backdrop._PREFIX + "hasAlpha" + backdrop._SUFFIX).encode(),
            source.replace(b"opacity < 1.0", b"opacity <= 1.0"),
            source.replace(b"hasAlpha)", b"hasAlpha && unexpected)"),
            source.replace(backdrop._INCLUDE.encode(), b""),
        ):
            with self.subTest(source=broken):
                with self.assertRaises(SystemExit):
                    backdrop.patch_source(broken)

    def test_partial_application_fails(self):
        source = fixture("hasAlpha")
        patched = backdrop.patch_source(source)
        for broken in (
            source + backdrop._CAPABILITY.encode(),
            source.replace(b"hasAlpha)", (backdrop._OPT_IN + "hasAlpha)").encode()),
            source + backdrop._ADDED_VARIANT.encode(),
            patched.replace(backdrop._CAPABILITY.encode(), b""),
            patched.replace(backdrop._ADDED_VARIANT.encode(), b""),
            patched + backdrop._CAPABILITY.encode(),
        ):
            with self.subTest(source=broken):
                with self.assertRaises(SystemExit):
                    backdrop.patch_source(broken)

    def test_disk_dry_run_and_failure_do_not_write(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / backdrop._SOURCE
            path.parent.mkdir(parents=True)
            original = fixture("hasAlpha", b"\r\n")
            path.write_bytes(original)
            self.assertTrue(backdrop.apply_patch(root, dry_run=True))
            self.assertEqual(path.read_bytes(), original)
            self.assertTrue(backdrop.apply_patch(root))
            self.assertFalse(backdrop.apply_patch(root))
            broken = original + backdrop._CAPABILITY.encode()
            path.write_bytes(broken)
            with self.assertRaises(SystemExit):
                backdrop.apply_patch(root)
            self.assertEqual(path.read_bytes(), broken)

    def test_only_qt_recipes_include_patch_after_official_patches(self):
        qt5 = next(stage for stage in recipes.STAGES if stage.name == "qt_5.15.19")
        for stage in (qt5, recipes_arm64.QT_STAGE):
            self.assertIn(str(backdrop.PATCH_SCRIPT), stage.dependencies)
            command = backdrop.patch_command()
            self.assertEqual(stage.commands.count(command), 1)
            self.assertLess(stage.commands.index("git apply %%i -v"), stage.commands.index(command))
            self.assertLess(stage.commands.index(command), stage.commands.index("configure -prefix"))
            self.assertTrue(command.startswith('"'))
        for stage in recipes.STAGES:
            if not stage.name.startswith("qt_"):
                self.assertNotIn(str(backdrop.PATCH_SCRIPT), stage.dependencies)

    def test_script_content_invalidates_only_dependent_cache(self):
        with tempfile.TemporaryDirectory() as temporary:
            script = Path(temporary) / "patch script.py"
            script.write_text("first")
            qt = Stage("qt_test", "Libraries", "command", dependencies=[str(script)])
            other = Stage("other", "Libraries", "command")
            with patch("build_support.dependency_cache.environment_keys", return_value=("libs", "tools")):
                qt_key, other_key = compute_cache_key(qt), compute_cache_key(other)
                script.write_text("second")
                self.assertNotEqual(compute_cache_key(qt), qt_key)
                self.assertEqual(compute_cache_key(other), other_key)


if __name__ == "__main__":
    unittest.main()
