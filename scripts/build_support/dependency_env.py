import hashlib
import os
import sys
from pathlib import Path

from build_support.paths import (
    LIBRARIES_ARCH_DIR,
    TARGET,
    TARGET_MSVC_PLATFORM,
    TARGET_SPECIAL_TARGET,
    THIRD_PARTY_DIR,
    TMP_DIR,
    USED_PREFIX_DIR,
)
from build_support.recipes import qt_version
from build_support.toolchain import CMAKE_TOOLSET

_FETCH_SCRIPT = Path(__file__).resolve().parent / "fetch.py"

# 这些键不影响 ThirdParty 工具本身（随库前缀变动，或仅作用于库构建），
# 计算 ThirdParty 缓存键时排除，避免库前缀或工具集变化引起工具链无谓重建
_IGNORE_FOR_THIRD_PARTY = frozenset({"USED_PREFIX", "LIBS_DIR", "SPECIAL_TARGET", "X8664", "MSBUILD_TOOLSET"})

# 上游把工具目录拼进 PATH，路径改到 build/tmp 后同样处理
_PATH_PREFIXES = (
    THIRD_PARTY_DIR / "msys64" / "ucrt64" / "bin",
    THIRD_PARTY_DIR / "jom",
    THIRD_PARTY_DIR / "gyp",
)

# Git Bash / MSYS 的 bin 目录提供 find、sort、link 等 GNU 同名程序，遮蔽后
# nmake 与 MSVC 会拿到错误实现，故让系统目录排在继承来的 PATH 之前
_SYSTEM_DIR = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"


def stage_variables() -> dict[str, str]:
    """阶段配方可见的变量，全部指向 build/tmp。"""
    return {
        "USED_PREFIX": str(USED_PREFIX_DIR),
        "ROOT_DIR": str(TMP_DIR),
        "LIBS_DIR": str(LIBRARIES_ARCH_DIR),
        "THIRDPARTY_DIR": str(THIRD_PARTY_DIR),
        "PATH_PREFIX": "".join(f"{path}{os.pathsep}" for path in _PATH_PREFIXES),
        "CMAKE_GENERATOR": "Ninja Multi-Config",
        "SPECIAL_TARGET": TARGET_SPECIAL_TARGET,
        "X8664": TARGET_MSVC_PLATFORM,
        # 覆盖上游工程里写死的 v143，命令行属性优先于工程文件
        "MSBUILD_TOOLSET": CMAKE_TOOLSET,
        # 配方用它下载与解压，避开 PowerShell 执行策略限制
        "FETCH": f'"{sys.executable}" "{_FETCH_SCRIPT}"',
    }


def build_environment(base: dict[str, str]) -> dict[str, str]:
    """把阶段变量并进 MSVC 环境，并前置工具目录。"""
    variables = stage_variables()
    environment = dict(base)
    environment.update(variables)
    # qt 配方用 %QT% 拼产物目录，上游同样只放进环境而不计入缓存键
    environment["QT"] = qt_version(TARGET)
    # dav1d 的 arm64 补丁靠 git revert 落提交，没有身份会直接失败
    environment.update({
        "GIT_AUTHOR_NAME": "AyuGram Build",
        "GIT_AUTHOR_EMAIL": "build@ayugram.invalid",
        "GIT_COMMITTER_NAME": "AyuGram Build",
        "GIT_COMMITTER_EMAIL": "build@ayugram.invalid",
    })
    # 该变量会让 bat 里的相对路径调用失效
    environment.pop("NoDefaultCurrentDirectoryInExePath", None)
    # 系统目录只压在继承的 PATH 之上，同样不参与缓存键
    inherited = f"{_SYSTEM_DIR}{os.pathsep}" + environment.get("PATH", "")
    environment["PATH"] = variables["PATH_PREFIX"] + inherited
    return environment


def environment_keys() -> tuple[str, str]:
    """返回 (Libraries 用键, ThirdParty 用键)，环境变动即触发重建。"""
    variables = stage_variables()
    full = ""
    partial = ""
    for key, value in variables.items():
        part = f"{key}={value};"
        full += part
        if key not in _IGNORE_FOR_THIRD_PARTY:
            partial += part
    return (
        hashlib.sha1(full.encode("utf-8")).hexdigest(),
        hashlib.sha1(partial.encode("utf-8")).hexdigest(),
    )
