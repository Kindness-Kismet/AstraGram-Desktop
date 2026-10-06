import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
FFMPEG_LIBRARIES = (
    "avcodec", "avutil", "swresample", "openh264", "opus", "vpx", "va", "vdpau",
)


@unittest.skipUnless(shutil.which("cmake") and shutil.which("cc") and shutil.which("make"),
                     "CMake, a C compiler, and make are required")
class LinuxImageLinksTests(unittest.TestCase):
    def link_command(self, *, linux, packaged, plugins=True):
        source = (ROOT / "CMakeLists.txt").read_text()
        block = source.split("add_subdirectory(cmake)\n", 1)[1].split(
            "add_subdirectory(Telegram)", 1
        )[0]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            deps = root / "deps"
            deps.mkdir()
            (deps / "plugin.c").write_text("void image_plugin(void) {}\n")
            (root / "main.c").write_text("int main(void) { return 0; }\n")
            (deps / "CMakeLists.txt").write_text("\n".join([
                "add_library(external_ffmpeg INTERFACE IMPORTED GLOBAL)",
                "add_library(desktop-app::external_ffmpeg ALIAS external_ffmpeg)",
                "target_link_libraries(external_ffmpeg INTERFACE "
                + " ".join(FFMPEG_LIBRARIES) + ")",
                "if (PLUGINS)",
                "  add_library(external_qt_static_plugins_kimageformats STATIC plugin.c)",
                "endif()",
            ]))
            (root / "CMakeLists.txt").write_text("\n".join([
                "cmake_minimum_required(VERSION 3.25)",
                "project(ImagePluginLinkCheck LANGUAGES C)",
                f"set(LINUX {'ON' if linux else 'OFF'})",
                f"set(DESKTOP_APP_USE_PACKAGED {'ON' if packaged else 'OFF'})",
                f"set(PLUGINS {'ON' if plugins else 'OFF'})",
                "add_subdirectory(deps)",
                block,
                "add_executable(probe main.c)",
                "if (PLUGINS)",
                "  target_link_libraries(probe PRIVATE external_qt_static_plugins_kimageformats)",
                "endif()",
            ]))
            result = subprocess.run(
                ["cmake", "-S", str(root), "-B", str(root / "out"),
                 "-G", "Unix Makefiles", f"-DCMAKE_C_COMPILER={shutil.which('cc')}"],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return (root / "out/CMakeFiles/probe.dir/link.txt").read_text()

    def test_linux_passes_static_dependencies_to_consumers(self):
        for packaged in (True, False):
            with self.subTest(packaged=packaged):
                command = self.link_command(linux=True, packaged=packaged)
                for library in FFMPEG_LIBRARIES:
                    self.assertIn(f"-l{library}", command)

    def test_other_platforms_do_not_receive_linux_dependencies(self):
        command = self.link_command(linux=False, packaged=True)
        self.assertNotIn("-lavcodec", command)

    def test_disabled_plugins_do_not_require_a_target(self):
        command = self.link_command(linux=True, packaged=True, plugins=False)
        self.assertNotIn("-lavcodec", command)


if __name__ == "__main__":
    unittest.main()
