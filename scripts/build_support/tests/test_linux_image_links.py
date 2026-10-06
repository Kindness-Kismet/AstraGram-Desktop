import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
FFMPEG_LIBRARIES = (
    "avfilter", "avformat", "avcodec", "swresample", "swscale", "avutil",
    "openh264", "opus", "vpx", "va-x11", "va-drm", "va", "vdpau",
)


@unittest.skipUnless(shutil.which("cmake") and shutil.which("cc") and shutil.which("make"),
                     "CMake, a C compiler, and make are required")
class LinuxImageLinksTests(unittest.TestCase):
    def link_commands(self, *, linux=True, packaged=True, plugins=True,
                      heif_type="STATIC", ffmpeg=True, heif=True):
        source = (ROOT / "CMakeLists.txt").read_text()
        setup = source.split("set(desktop_app_skip_libs", 1)[1].split(
            "add_subdirectory(Telegram)", 1
        )[0]
        setup = "set(desktop_app_skip_libs" + setup
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            deps = root / "cmake"
            deps.mkdir()
            (root / "Telegram").symlink_to(ROOT / "Telegram", target_is_directory=True)
            (deps / "plugin.c").write_text("void image_plugin(void) {}\n")
            (root / "main.c").write_text("int main(void) { return 0; }\n")
            for library in (*FFMPEG_LIBRARIES, "heif", "de265", "heif_extra"):
                (root / f"lib{library}.a").touch()
            heif_libraries = ["avcodec", "avutil"] if ffmpeg else ["de265"]
            heif_links = ";".join(str(root / f"lib{name}.a")
                                  for name in (*heif_libraries, "heif_extra"))
            if heif:
                (root / "libheif-config.cmake").write_text("\n".join([
                    "if (TARGET heif)",
                    "  return()",
                    "endif()",
                    f"add_library(heif {heif_type} IMPORTED)",
                    "set_target_properties(heif PROPERTIES",
                    f'  IMPORTED_LOCATION "{root}/libheif.a"',
                    f'  INTERFACE_LINK_LIBRARIES "{heif_links}")',
                ]))
            (deps / "CMakeLists.txt").write_text("\n".join([
                "add_library(external_ffmpeg INTERFACE IMPORTED GLOBAL)",
                "add_library(desktop-app::external_ffmpeg ALIAS external_ffmpeg)",
                "add_library(PkgConfig::DESKTOP_APP_FFMPEG INTERFACE IMPORTED)",
                "target_link_libraries(PkgConfig::DESKTOP_APP_FFMPEG INTERFACE "
                + " ".join(f'"{root}/lib{name}.a"' for name in FFMPEG_LIBRARIES) + ")",
                "target_link_libraries(external_ffmpeg INTERFACE PkgConfig::DESKTOP_APP_FFMPEG)",
                "if (PLUGINS)",
                "  find_package(libheif QUIET)",
                "  add_library(external_qt_static_plugins_kimageformats STATIC plugin.c)",
                "  target_link_libraries(external_qt_static_plugins_kimageformats PRIVATE",
                "    $<TARGET_NAME_IF_EXISTS:heif>)",
                "endif()",
            ]))
            (root / "CMakeLists.txt").write_text("\n".join([
                "cmake_minimum_required(VERSION 3.25...3.31)",
                "project(ImagePluginLinkCheck LANGUAGES C)",
                f"set(LINUX {'ON' if linux else 'OFF'})",
                f"set(DESKTOP_APP_USE_PACKAGED {'ON' if packaged else 'OFF'})",
                f"set(PLUGINS {'ON' if plugins else 'OFF'})",
                f'set(libheif_DIR "{root}")',
                f"set(CMAKE_DISABLE_FIND_PACKAGE_libheif {'OFF' if heif else 'ON'})",
                setup,
                "foreach(name emoji_probe app_probe reversed_app_probe)",
                "  add_executable(${name} main.c)",
                "endforeach()",
                "target_link_libraries(app_probe PRIVATE desktop-app::external_ffmpeg)",
                "if (PLUGINS)",
                "  foreach(name emoji_probe app_probe reversed_app_probe)",
                "    target_link_libraries(${name} PRIVATE external_qt_static_plugins_kimageformats)",
                "  endforeach()",
                "endif()",
                "target_link_libraries(reversed_app_probe PRIVATE desktop-app::external_ffmpeg)",
            ]))
            result = subprocess.run(
                ["cmake", "-S", str(root), "-B", str(root / "out"),
                 "-G", "Unix Makefiles", "-DCMAKE_SYSTEM_NAME=Linux",
                 "-DCMAKE_C_COMPILER_WORKS=TRUE", f"-DCMAKE_C_COMPILER={shutil.which('cc')}"],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return {
                name: [Path(token).name for token in shlex.split(
                    (root / f"out/CMakeFiles/{name}.dir/link.txt").read_text())]
                for name in ("emoji_probe", "app_probe", "reversed_app_probe")
            }

    def test_linux_orders_heif_before_complete_ffmpeg_dependencies(self):
        for packaged in (True, False):
            with self.subTest(packaged=packaged):
                for name, command in self.link_commands(packaged=packaged).items():
                    with self.subTest(target=name):
                        for library in (*FFMPEG_LIBRARIES, "heif_extra"):
                            self.assertIn(f"lib{library}.a", command)
                        self.assertLess(command.index("libheif.a"), command.index("libavcodec.a"))
                        for library in ("swresample", "avutil", "openh264", "opus", "vpx",
                                        "va-x11", "va-drm", "va", "vdpau"):
                            self.assertLess(command.index("libavcodec.a"),
                                            command.index(f"lib{library}.a"))

    def test_other_platforms_keep_existing_heif_dependencies(self):
        command = self.link_commands(linux=False)["emoji_probe"]
        self.assertIn("libavcodec.a", command)
        self.assertIn("libheif_extra.a", command)
        self.assertNotIn("libvpx.a", command)

    def test_shared_heif_keeps_its_existing_dependencies(self):
        command = self.link_commands(heif_type="SHARED")["emoji_probe"]
        self.assertIn("libavcodec.a", command)
        self.assertNotIn("libvpx.a", command)

    def test_other_heif_backends_do_not_gain_ffmpeg_dependencies(self):
        command = self.link_commands(ffmpeg=False)["emoji_probe"]
        self.assertIn("libde265.a", command)
        self.assertIn("libheif_extra.a", command)
        self.assertNotIn("libavcodec.a", command)

    def test_missing_heif_does_not_require_a_target(self):
        command = self.link_commands(heif=False)["emoji_probe"]
        self.assertNotIn("libheif.a", command)
        self.assertNotIn("libavcodec.a", command)

    def test_disabled_plugins_do_not_add_image_dependencies(self):
        command = self.link_commands(plugins=False)["emoji_probe"]
        self.assertNotIn("libheif.a", command)
        self.assertNotIn("libavcodec.a", command)


if __name__ == "__main__":
    unittest.main()
