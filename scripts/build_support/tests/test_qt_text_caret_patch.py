import tempfile
import unittest
from pathlib import Path

from build_support import qt_text_caret_patch as caret
from build_support import recipes, recipes_arm64
from build_support import qt_native_backdrop_patch as backdrop


def fixture(newline: bytes = b"\n") -> bytes:
    return (caret._ANCHOR + caret._RASTER_OP
            + "    p->fillRect(rect, p->pen().brush());\n"
            + "    p->setCompositionMode(origCompositionMode);\n"
            ).encode().replace(b"\n", newline)


class TextCaretPatchTests(unittest.TestCase):
    def test_line_endings_are_preserved_and_idempotent(self):
        for newline in (b"\n", b"\r\n"):
            with self.subTest(newline=newline):
                result = caret.patch_source(fixture(newline))
                self.assertEqual(caret.patch_source(result), result)
                self.assertNotIn(b"RasterOp_NotDestination", result)
                self.assertIn(caret._MARKER.encode().replace(b"\n", newline), result)
                self.assertIn(b"setCompositionMode(origCompositionMode)", result)
                if newline == b"\r\n":
                    self.assertNotIn(b"\n", result.replace(b"\r\n", b""))

    def test_missing_duplicate_or_drifted_anchors_fail(self):
        source = fixture()
        for broken in (
            source.replace(caret._ANCHOR.encode(), b""),
            source + caret._ANCHOR.encode(),
            source.replace(b"RasterOp_NotDestination", b"RasterOp_SourceXorDestination"),
            source + b"// RasterOp_NotDestination\n",
        ):
            with self.subTest(source=broken):
                with self.assertRaises(SystemExit):
                    caret.patch_source(broken)

    def test_partial_application_fails(self):
        source = fixture()
        patched = caret.patch_source(source)
        for broken in (
            source + caret._MARKER.encode(),
            patched + caret._MARKER.encode(),
            patched + caret._RASTER_OP.encode(),
        ):
            with self.subTest(source=broken):
                with self.assertRaises(SystemExit):
                    caret.patch_source(broken)

    def test_disk_dry_run_and_failure_do_not_write(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / caret._SOURCE
            path.parent.mkdir(parents=True)
            original = fixture(b"\r\n")
            path.write_bytes(original)
            self.assertTrue(caret.apply_patch(root, dry_run=True))
            self.assertEqual(path.read_bytes(), original)
            self.assertTrue(caret.apply_patch(root))
            self.assertFalse(caret.apply_patch(root))
            broken = original + caret._MARKER.encode()
            path.write_bytes(broken)
            with self.assertRaises(SystemExit):
                caret.apply_patch(root)
            self.assertEqual(path.read_bytes(), broken)

    def test_only_qt_recipes_apply_patch_after_backdrop_patch(self):
        qt5 = next(stage for stage in recipes.STAGES if stage.name == "qt_5.15.19")
        command = caret.patch_command()
        for stage in (qt5, recipes_arm64.QT_STAGE):
            self.assertIn(str(caret.PATCH_SCRIPT), stage.dependencies)
            self.assertEqual(stage.commands.count(command), 1)
            self.assertLess(
                stage.commands.index(backdrop.patch_command()),
                stage.commands.index(command))
            self.assertLess(stage.commands.index(command), stage.commands.index("configure -prefix"))
            self.assertNotIn("__QT_TEXT_CARET_PATCH__", stage.commands)
        for stage in recipes.STAGES:
            if not stage.name.startswith("qt_"):
                self.assertNotIn(str(caret.PATCH_SCRIPT), stage.dependencies)


if __name__ == "__main__":
    unittest.main()
