import ast
import glob
import hashlib
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PREPARE = ROOT / "Telegram/build/prepare/prepare.py"
PATCH = PREPARE.parent / "patches/qt-macos-pac-cleanup.patch"
SOURCE = Path("src/network/kernel/qnetworkproxy_darwin.cpp")
# Qt 6.11.2 的 PAC 等待及错误出口，独立于补丁文件。
FIXTURE = """void query()
{
            CFRunLoopAddSource(CFRunLoopGetCurrent(), pacRunLoopSource, pacRunLoopMode);
            while (!pacInfo.done)
                CFRunLoopRunInMode(pacRunLoopMode, 1000, /*returnAfterSourceHandled*/ true);

            if (!pacInfo.proxies) {
                QString pacLocation = QString::fromCFString(cfPacLocation);
                return result;
            }
            return pacInfo.proxies;
}
"""


def qt_recipe(root, *, mac=True, qt="6.11.2", patch=None):
    # 只加载配方及缓存函数，避免 prepare.py 顶层探测或执行依赖构建。
    tree = ast.parse(PREPARE.read_text())
    functions = {"filterByPlatform", "stage", "computeFileHash", "computeCacheKey",
                 "removeDir"}
    nodes = [node for node in tree.body
             if isinstance(node, ast.FunctionDef) and node.name in functions]
    patch_path = next(node for node in tree.body
                      if isinstance(node, ast.Assign)
                      and any(isinstance(target, ast.Name) and target.id == "qtMacPacPatch"
                              for target in node.targets))
    qt_branch = next(node for node in tree.body
                     if isinstance(node, ast.If)
                     and ast.unparse(node.test) == "qt < '6'")
    namespace = {
        "os": os, "re": re, "glob": glob, "hashlib": hashlib,
        "win": not mac, "win32": False, "win64": not mac, "winarm": False,
        "mac": mac, "options": [], "qt": qt, "scriptPath": str(PREPARE.parent),
        "libsDir": str(root / "Libraries"),
        "thirdPartyDir": str(root / "ThirdParty"), "stages": [],
        "environmentKey": "test", "envForThirdPartyKey": "test",
    }

    def fail(message):
        raise AssertionError(message)

    namespace["error"] = fail
    module = ast.Module(body=[*nodes, patch_path], type_ignores=[])
    exec(compile(module, str(PREPARE), "exec"), namespace)
    if patch is not None:
        namespace["qtMacPacPatch"] = str(patch)
    module = ast.Module(body=[qt_branch], type_ignores=[])
    exec(compile(module, str(PREPARE), "exec"), namespace)
    stage = next(stage for stage in namespace["stages"] if stage["name"] == "qt_" + qt)
    return stage, namespace


class MacPacPatchTests(unittest.TestCase):
    def apply(self, root):
        return subprocess.run(
            ["git", "apply", str(PATCH)], cwd=root,
            capture_output=True, text=True, timeout=10,
        )

    def test_cleanup_precedes_both_result_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / SOURCE
            source.parent.mkdir(parents=True)
            source.write_text(FIXTURE)
            result = self.apply(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            changed = source.read_text()
            cleanup = "            CFRunLoopSourceInvalidate(pacRunLoopSource);\n"
            self.assertEqual(changed.count(cleanup), 1)
            self.assertEqual(changed.replace(cleanup, ""), FIXTURE)
            self.assertLess(changed.index("CFRunLoopRunInMode"), changed.index(cleanup))
            self.assertLess(changed.index(cleanup), changed.index("if (!pacInfo.proxies)"))
            self.assertNotEqual(self.apply(root).returncode, 0)
            self.assertEqual(source.read_text(), changed)

    def test_changed_upstream_context_fails_without_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / SOURCE
            source.parent.mkdir(parents=True)
            original = FIXTURE.replace("while (!pacInfo.done)", "while (pending)")
            source.write_text(original)
            self.assertNotEqual(self.apply(root).returncode, 0)
            self.assertEqual(source.read_text(), original)

    def test_only_mac_qt_recipe_applies_patch_before_configure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage, _ = qt_recipe(root)
            command = f'git -C qtbase apply "{PATCH}"'
            self.assertIn(str(PATCH), stage["dependencies"])
            self.assertEqual(stage["commands"].count(command), 1)
            self.assertLess(stage["commands"].index("patches/qtbase_$QT"),
                            stage["commands"].index(command))
            self.assertLess(stage["commands"].index(command),
                            stage["commands"].index("./configure"))
            for qt in ("5.15.19", "6.11.2"):
                windows, _ = qt_recipe(root, mac=False, qt=qt)
                self.assertNotIn(str(PATCH), windows["dependencies"])
                self.assertNotIn(command, windows["commands"])

    def test_patch_content_invalidates_qt_dependency_key(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            official = root / "Libraries/patches/qtbase_6.11.2"
            official.mkdir(parents=True)
            (official / "official.patch").write_text("unchanged")
            patch = root / PATCH.name
            patch.write_bytes(PATCH.read_bytes())
            stage, namespace = qt_recipe(root, patch=patch)
            key = namespace["computeCacheKey"](stage)
            self.assertEqual(key, namespace["computeCacheKey"](stage))
            patch.write_bytes(patch.read_bytes() + b"\n")
            self.assertNotEqual(key, namespace["computeCacheKey"](stage))


if __name__ == "__main__":
    unittest.main()
