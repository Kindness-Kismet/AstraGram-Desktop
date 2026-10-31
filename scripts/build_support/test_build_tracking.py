import tempfile
import unittest
from pathlib import Path

from build_tracking import recover_failed_compilations


class BuildTrackingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "out"
        self.tracking = self.output / "app.dir/Debug/app.tlog"
        self.tracking.mkdir(parents=True)
        (self.tracking / "unsuccessfulbuild").touch()

    def write(self, name, text):
        path = self.tracking / name
        path.write_text(text, encoding="utf-16")
        return path

    def recover(self):
        return recover_failed_compilations(self.output, "Debug", self.root / "backups")

    def test_preserves_successful_sources_and_precompiled_header(self):
        command = self.write("CL.command.1.tlog",
                             "^pch.cpp\n/Yc\n^good.cpp\n/Yu\n^failed.cpp\n/Yu\n")
        reads = self.write("CL.read.1.tlog", "^PCH.CPP\npch.h\n^good.cpp\ngood.h\n")
        writes = self.write("CL.write.1.tlog", "^pch.cpp|good.cpp|failed.cpp\noutputs.obj\n")
        before = {path: path.read_bytes() for path in (command, reads, writes)}
        self.assertEqual(len(self.recover()), 1)
        self.assertEqual(command.read_text(encoding="utf-16"), "^pch.cpp\n/Yc\n^good.cpp\n/Yu\n")
        self.assertEqual(reads.read_bytes(), before[reads])
        self.assertEqual(writes.read_bytes(), before[writes])
        backup = next((self.root / "backups").rglob(command.name))
        self.assertEqual(backup.read_bytes(), before[command])
        self.assertEqual(self.recover(), [])

    def test_group_with_incomplete_source_is_retried(self):
        command = self.write("CL.command.1.tlog", "^good.cpp|failed.cpp\n/c\n")
        self.write("CL.read.1.tlog", "^good.cpp\ngood.h\n")
        self.write("CL.write.1.tlog", "^good.cpp|failed.cpp\noutput.obj\n")
        self.recover()
        self.assertEqual(command.read_text(encoding="utf-16"), "")

    def test_missing_output_tracking_is_retried(self):
        command = self.write("CL.command.1.tlog", "^failed.cpp\n/c\n")
        self.write("CL.read.1.tlog", "^failed.cpp\nfailed.h\n")
        self.recover()
        self.assertEqual(command.read_text(encoding="utf-16"), "")

    def test_other_configuration_is_untouched(self):
        command = self.write("CL.command.1.tlog", "^failed.cpp\n/c\n")
        before = command.read_bytes()
        self.assertEqual(recover_failed_compilations(self.output, "Release", self.root / "backups"), [])
        self.assertEqual(command.read_bytes(), before)

    def test_successful_project_is_untouched(self):
        (self.tracking / "unsuccessfulbuild").rename(self.tracking / "completed")
        command = self.write("CL.command.1.tlog", "^source.cpp\n/c\n")
        before = command.read_bytes()
        self.assertEqual(self.recover(), [])
        self.assertEqual(command.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
