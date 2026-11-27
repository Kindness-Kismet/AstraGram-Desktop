import os
import re
import subprocess
from pathlib import Path

from build_support.paths import TARGET_CMAKE_ARCH, TARGET_VCVARS_ARCH

VSWHERE = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"

# 本地与 CI 使用 VS 2026 的 v145 工具集，覆盖值须满足最低安全版本。
TOOLSET_VERSION = os.environ.get("AYUGRAM_TOOLSET_VERSION", "14.52")
CMAKE_TOOLSET = os.environ.get("AYUGRAM_CMAKE_TOOLSET", "v145")
CMAKE_GENERATOR = os.environ.get("AYUGRAM_CMAKE_GENERATOR", "Visual Studio 18 2026")

# 此版本已修复早期 14.52 的 ARM64 FFmpeg 编译错误。
_MINIMUM_TOOLSET = (14, 52, 37025)

# 清除外层终端的路径缓存与 MSYS 模式，避免登录脚本覆盖 MSVC 环境。
# PYTHONUTF8 会让 meson 用 UTF-8 解码 MSVC 的 GBK 输出。
_ENVIRONMENT_LEAKS = ("MSYSTEM", "MSYS_NO_PATHCONV", "EXEPATH", "ORIGINAL_PATH", "PYTHONUTF8")

_CMAKE_SUBPATH = Path("Common7") / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin"


def vs_installation() -> Path:
    if not VSWHERE.is_file():
        raise SystemExit(f"vswhere.exe not found at {VSWHERE}. Install Visual Studio or the Build Tools.")

    result = subprocess.run(
        [str(VSWHERE), "-all", "-products", "*", "-latest", "-property", "installationPath"],
        capture_output=True,
        text=True,
        errors="replace",
    )
    for line in result.stdout.splitlines():
        path = Path(line.strip())
        if (path / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat").is_file():
            return path
    raise SystemExit("vcvarsall.bat not found. Install the MSVC v145 build tools.")


def find_vcvars() -> Path:
    # vcvars64.bat 只是转发壳，直接用 vcvarsall.bat 避免 %~dp0 经多层 cmd 转发后解析失败
    return vs_installation() / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"


def _matches_toolset(version: str) -> bool:
    return version == TOOLSET_VERSION or version.startswith(TOOLSET_VERSION + ".")


def msvc_environment() -> dict[str, str]:
    """返回选定 MSVC 的环境；仅复用架构和工具集均匹配的开发终端。"""
    environment = {key.upper(): value for key, value in os.environ.items()}
    if (environment.get("PLATFORM", "").lower() == TARGET_CMAKE_ARCH.lower()
            and _matches_toolset(environment.get("VCTOOLSVERSION", "").rstrip("\\/"))):
        return _finalize(environment)

    vcvars = find_vcvars()
    # 用分隔符隔开 vcvars 自身输出，避免它的横幅被当成变量解析
    marker = "__VCVARS_ENV__"
    command = f'"{vcvars}" {TARGET_VCVARS_ARCH} -vcvars_ver={TOOLSET_VERSION} && echo {marker} && set'
    # 必须 shell=True 传单字符串：list 传参会给带空格路径再套一层引号，cmd 无法解析
    result = subprocess.run(command, shell=True, capture_output=True, text=True, errors="replace")
    if result.returncode != 0:
        raise SystemExit(f"vcvarsall.bat failed:\n{result.stdout}\n{result.stderr}")

    _, _, dump = result.stdout.partition(marker)
    for line in dump.splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            # Windows 环境键统一大写，避免 PATH/Path 重复及 CI 版本显示缺失。
            environment[key.upper()] = value
    return _finalize(environment)


def _finalize(environment: dict[str, str]) -> dict[str, str]:
    if environment.get("PLATFORM", "").lower() != TARGET_CMAKE_ARCH.lower():
        raise SystemExit(f"MSVC environment did not report Platform={TARGET_CMAKE_ARCH}.")
    version = environment.get("VCTOOLSVERSION", "").rstrip("\\/")
    if (not re.fullmatch(r"\d+(?:\.\d+){2,3}", version)
            or tuple(map(int, version.split("."))) < _MINIMUM_TOOLSET):
        minimum = ".".join(map(str, _MINIMUM_TOOLSET))
        raise SystemExit(f"MSVC {minimum} or newer is required; selected '{version}'.")
    if not _matches_toolset(version):
        raise SystemExit(f"Requested MSVC {TOOLSET_VERSION}, but selected {version}.")
    environment["VCTOOLSVERSION"] = version
    for key in _ENVIRONMENT_LEAKS:
        environment.pop(key, None)

    cmake_bin = vs_installation() / _CMAKE_SUBPATH
    # PATH 里常有更旧的 CMake（MinGW 的 3.30.0 在多配置生成器下 FindPython 会报错），
    # VS 配套的那份版本与工具链同步，必须排在最前
    if (cmake_bin / "cmake.exe").is_file():
        environment["PATH"] = f"{cmake_bin}{os.pathsep}" + environment.get("PATH", "")
    return environment


def describe_toolset(environment: dict[str, str]) -> str:
    version = environment.get("VCTOOLSVERSION", "Unknown")
    sdk = environment.get("WINDOWSSDKVERSION", "Unknown").rstrip("\\")
    return f"MSVC {version}  SDK {sdk}"
