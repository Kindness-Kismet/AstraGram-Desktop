import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

BUILD_DIR = ROOT / "build"
TMP_DIR = BUILD_DIR / "tmp"
LOGS_DIR = BUILD_DIR / "logs"

# 产物目录带版本号：build/<APP_NAME>-v<版本>-win-<架构>-<profile>
APP_NAME = "AstraGram"

# 依赖与中间产物一律收在 build/tmp，不外溢到仓库同级目录
LIBRARIES_DIR = TMP_DIR / "Libraries"
THIRD_PARTY_DIR = TMP_DIR / "ThirdParty"
CMAKE_OUT_DIR = TMP_DIR / "out"

# 目标平台由 AYUGRAM_TARGET 选择，取值与上游一致：win64 或 winarm64。
# arm64 在 arm64 runner 上构建，依赖与产物都不与 x64 混用。
_TARGETS = {
    "win64": ("x64", "x64", "x64"),      # CMake -A、vcvarsall 架构、产物后缀
    "winarm64": ("ARM64", "arm64", "arm64"),
}
TARGET = os.environ.get("AYUGRAM_TARGET", "win64")
if TARGET not in _TARGETS:
    raise SystemExit(f"unknown AYUGRAM_TARGET: {TARGET}, expect win64 or winarm64")
TARGET_CMAKE_ARCH, TARGET_VCVARS_ARCH, TARGET_SUFFIX = _TARGETS[TARGET]

# CMake 的 special target 与 MSVC 平台名，取值与上游构建脚本一致
TARGET_SPECIAL_TARGET = {"win64": "win64", "winarm64": "winarm"}[TARGET]
TARGET_MSVC_PLATFORM = {"win64": "x64", "winarm64": "ARM64"}[TARGET]

# 依赖按目标平台分目录，与固化配方中的目录约定一致
LIBRARIES_ARCH_DIR = LIBRARIES_DIR / TARGET
USED_PREFIX_DIR = LIBRARIES_ARCH_DIR / "local"

# prebuild 建出的工具，codegen、d3d 校验和 NuGet 都从这里取
VENV_PYTHON = THIRD_PARTY_DIR / "python" / "Scripts" / "python.exe"
NUGET_EXE = THIRD_PARTY_DIR / "NuGet" / "nuget.exe"

VERSION_FILE = ROOT / "Telegram" / "build" / "version"

# 官方文档公开的测试用凭据，正式分发需自行申请
DEFAULT_API_ID = "2040"
DEFAULT_API_HASH = "b18441a1ff607e10a989891a5462e627"

# 构建产物，Updater 缺失不视为失败
PRODUCT_BINARIES = ("AstraGram.exe", "Updater.exe")

# 仅带 --pdb 构建 Debug 时生成与收集
DEBUG_SYMBOLS = ("AstraGram.pdb",)
