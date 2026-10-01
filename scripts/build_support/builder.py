import os
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

from build_support.build_tracking import recover_failed_compilations
from build_support.cmake_patch import LIBS_LOC_OPTION, PYTHON_OPTION, ensure_libs_loc_override
from build_support.console import format_bytes, header, print_summary, warn
from build_support.paths import (
    APP_NAME,
    BUILD_DIR,
    CMAKE_OUT_DIR,
    DEBUG_SYMBOLS,
    LIBRARIES_ARCH_DIR,
    PRODUCT_BINARIES,
    ROOT,
    TARGET,
    TARGET_CMAKE_ARCH,
    TARGET_SPECIAL_TARGET,
    TARGET_SUFFIX,
    VENV_PYTHON,
    NUGET_EXE,
)
from build_support.processes import run
from build_support.recipes import qt_version
from build_support.style_regen import regenerate_styles
from build_support.timer import timed_step
from build_support.toolchain import CMAKE_GENERATOR, CMAKE_TOOLSET, msvc_environment
from build_support.version import parse_version, read_current_version

_CONFIGURATIONS = {"dev": "Debug", "release": "Release"}

# 非空值启用官方发布语义：Updater、Packer 与更新检查，另含过旧版本提示
# 和闭源 alpha 支持。该标记同时决定 AUTOUPDATE 的默认值。
_SPECIAL_TARGET = TARGET_SPECIAL_TARGET

DEFAULT_BUILD_JOBS = 32
MAX_BUILD_JOBS = 128


def output_dir(profile: str) -> Path:
    # 版本号取自 Telegram/build/version，与 cli.py 的产物解析保持一致
    version = parse_version(read_current_version()).original
    return BUILD_DIR / f"{APP_NAME}-v{version}-win-{TARGET_SUFFIX}-{profile}"


def clean_output_dir(profile: str) -> int:
    """删除产物目录里的运行残留，只保留构建产物，返回删除项数。"""
    directory = output_dir(profile)
    wanted = set(PRODUCT_BINARIES + (DEBUG_SYMBOLS if profile == "dev" else ()))
    removed = 0
    for entry in directory.iterdir():
        if entry.name in wanted:
            continue
        if entry.is_dir():
            shutil.rmtree(entry)
        else:
            entry.unlink()
        removed += 1
    return removed


def zip_output(profile: str) -> Path:
    directory = output_dir(profile)
    # release 是发布产物不带后缀，dev 本地调试用带 -dev 区分
    version = parse_version(read_current_version()).original
    suffix = "" if profile == "release" else f"-{profile}"
    archive = BUILD_DIR / f"{APP_NAME}-v{version}-win-{TARGET_SUFFIX}{suffix}.zip"
    if archive.exists():
        archive.unlink()
    # 目录里可能残留运行期数据（tdata、日志），只打包构建产物本身
    wanted = PRODUCT_BINARIES + (DEBUG_SYMBOLS if profile == "dev" else ())
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
        for name in wanted:
            source = directory / name
            if source.is_file():
                bundle.write(source, name)
    return archive


def build(configurations: list[str], api_id: str, api_hash: str, reconfigure: bool, jobs: int, pack: bool = False, clean_pack: bool = False, pdb: bool = False) -> None:
    environment = msvc_environment()
    # cmake/external/qt 靠 %QT% 定位 Qt-<版本> 目录，缺失会直接 FATAL_ERROR
    environment["QT"] = qt_version(TARGET)

    if not LIBRARIES_ARCH_DIR.is_dir():
        raise SystemExit(f"Dependencies missing at {LIBRARIES_ARCH_DIR}. Run scripts/prebuild.py first.")

    with timed_step("Patch cmake helpers"):
        for note in ensure_libs_loc_override():
            print(f"  {note}", flush=True)

    if reconfigure and CMAKE_OUT_DIR.exists():
        with timed_step("Clear CMake cache"):
            shutil.rmtree(CMAKE_OUT_DIR, ignore_errors=True)
            print(f"  removed {CMAKE_OUT_DIR}", flush=True)

    with timed_step("Configure CMake"):
        configure(environment, api_id, api_hash, pdb)

    # 运行中的实例会占住产物，编译完再停就白等一轮链接，开工前先腾出来
    with timed_step("Release running instances"):
        stopped = stop_running_instances(configurations)
        print(f"  {stopped}", flush=True)

    produced: list[tuple[str, object]] = []
    for profile in configurations:
        cmake_config = _CONFIGURATIONS[profile]
        with timed_step(f"Regenerate styles {cmake_config}"):
            print(f"  {regenerate_styles(environment, cmake_config)}", flush=True)
        with timed_step(f"Build {cmake_config}"):
            compile_target(environment, cmake_config, jobs)
        with timed_step(f"Collect {cmake_config}"):
            produced.extend(collect(cmake_config, profile, pdb))
        if pack:
            if clean_pack:
                with timed_step(f"Clean {cmake_config} output"):
                    removed = clean_output_dir(profile)
                    print(f"  removed {removed} runtime entries", flush=True)
            with timed_step(f"Package {cmake_config}"):
                archive = zip_output(profile)
                print(f"  {archive.name}  {format_bytes(archive.stat().st_size)}", flush=True)

    total = sum(path.stat().st_size for _, path in produced)
    print_summary(
        *(f"Location {directory}" for directory in sorted({path.parent for _, path in produced})),
        f"Files {len(produced)}",
        f"Total {format_bytes(total)}",
    )


def cmake_executable(environment: dict[str, str]) -> str:
    """CreateProcess 按调用进程的 PATH 解析程序名，须显式取环境里那份 cmake。"""
    found = shutil.which("cmake", path=environment["PATH"])
    if not found:
        raise SystemExit("cmake not found in the MSVC environment PATH.")
    return found


def configure(environment: dict[str, str], api_id: str, api_hash: str, pdb: bool) -> None:
    CMAKE_OUT_DIR.mkdir(parents=True, exist_ok=True)
    command = [
        cmake_executable(environment),
        "-B",
        str(CMAKE_OUT_DIR),
        "-S",
        str(ROOT),
        "-G",
        CMAKE_GENERATOR,
        "-A",
        TARGET_CMAKE_ARCH,
        "-T",
        CMAKE_TOOLSET,
        f"-DTDESKTOP_API_ID={api_id}",
        f"-DTDESKTOP_API_HASH={api_hash}",
        f"-D{LIBS_LOC_OPTION}={LIBRARIES_ARCH_DIR.as_posix()}",
        f"-D{PYTHON_OPTION}={VENV_PYTHON.as_posix()}",
        f"-DNUGET_EXE={NUGET_EXE.as_posix()}",
    ]

    # 发布构建必需：官方构建才带 Updater 与更新检查，且该标记受版本控制，
    # 不依赖本地标记文件。
    command.append(f"-DDESKTOP_APP_SPECIAL_TARGET={_SPECIAL_TARGET}")

    # 官方发布语义：非空 SPECIAL_TARGET 关闭该选项的默认禁用，但缓存里的旧值
    # 不会被 option() 覆盖，故显式传 OFF，让 Updater 参与构建。
    command.append("-DDESKTOP_APP_DISABLE_AUTOUPDATE=OFF")

    # 默认不生成调试信息；--pdb 时 Debug 把调试信息嵌进 obj，链接据此完整生成 pdb。
    # 切换会改变全部编译参数，触发全量重编。
    debug_format = "$<$<CONFIG:Debug>:Embedded>" if pdb else ""
    command.append(f"-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT={debug_format}")

    # Qt5 的官方配置文件含有未初始化变量，开启该诊断会把外部包告警当成配置失败
    command += ["-Werror=dev", "-Werror=deprecated"]
    run(command, ROOT, environment, "CMake configure")


def compile_target(environment: dict[str, str], cmake_config: str, jobs: int) -> None:
    for project in recover_failed_compilations(
        CMAKE_OUT_DIR, cmake_config, BUILD_DIR / "build-recovery"
    ):
        print(f"  Recompile after unsuccessful build: {project}", flush=True)
    command = [
        cmake_executable(environment),
        "--build",
        str(CMAKE_OUT_DIR),
        "--config",
        cmake_config,
        "--target",
        "Telegram",
    ]
    # 项目串行、源文件并行，避免两级并发相乘突破指定上限。
    command.extend(["--parallel", "1", "--", f"/p:CL_MPCount={jobs}"])
    command.extend(ccache_properties(environment))
    run(command, ROOT, environment, f"Build {cmake_config}")


def ccache_properties(environment: dict[str, str]) -> list[str]:
    """AYUGRAM_CCACHE 指向 ccache.exe 时，让 MSBuild 经 ccache 调用编译器。"""
    ccache = environment.get("AYUGRAM_CCACHE")
    if not ccache:
        return []
    # ccache 以 cl.exe 的名字运行时，会到 PATH 里找真正的编译器来调用。
    shim = BUILD_DIR / "ccache-cl" / "cl.exe"
    shim.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ccache, shim)
    # 放宽预编译头的宏定义与时间宏检查，否则用到预编译头的源文件一律不缓存。
    environment.setdefault("CCACHE_SLOPPINESS", "pch_defines,time_macros")
    # 未命中时直接编译并由 /showIncludes 记录依赖，省掉一遍预处理。
    environment.setdefault("CCACHE_DEPEND", "1")
    print(f"  ccache {ccache}", flush=True)
    # ccache 每次只能编译一个文件，须由 MultiToolTask 逐文件调用；
    # 关闭文件跟踪，免得缓存目录里的文件被记成编译输入。
    return [f"/p:CLToolPath={shim.parent}", "/p:UseMultiToolTask=true", "/p:TrackFileAccess=false"]


def stop_running_instances(configurations: list[str]) -> str:
    """停掉本项目产物目录里正在运行的实例，按可执行文件绝对路径匹配。"""
    targets = [
        output_dir(profile) / name
        for profile in configurations
        for name in PRODUCT_BINARIES
        if (output_dir(profile) / name).is_file()
    ]
    stopped = 0
    for target in targets:
        if locking_pids(target):
            release_target(target)
            stopped += 1
    return f"stopped {stopped} instance(s)" if stopped else "nothing running"


def locking_pids(path: Path) -> list[int]:
    """按可执行文件绝对路径匹配进程，不按进程名，避免误杀正式安装版。"""
    if sys.platform != "win32":
        return []
    escaped_path = str(path).replace("'", "''")
    command = [
        "powershell",
        "-NoProfile",
        "-Command",
        f"$expected = '{escaped_path}'; "
        "Get-CimInstance Win32_Process | "
        "Where-Object { $_.ExecutablePath -eq $expected } | "
        "ForEach-Object { $_.ProcessId }",
    ]
    result = subprocess.run(command, cwd=str(ROOT), text=True, errors="replace", capture_output=True, check=False)
    return [int(line.strip()) for line in result.stdout.splitlines() if line.strip().isdigit()]


def release_target(path: Path) -> None:
    """腾出被运行中实例占用的产物，先请求正常退出，超时才强制结束。"""
    pids = locking_pids(path)
    if not pids:
        return

    for pid in pids:
        print(warn(f"  {path.name} in use by pid {pid}, stopping"), flush=True)
        subprocess.run(["taskkill", "/PID", str(pid)], capture_output=True, check=False)

    for _ in range(20):
        if not locking_pids(path):
            return
        time.sleep(0.25)

    for pid in locking_pids(path):
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True, check=False)
    time.sleep(0.5)


def collect(cmake_config: str, profile: str, pdb: bool) -> list[tuple[str, object]]:
    source_dir = CMAKE_OUT_DIR / cmake_config
    destination = output_dir(profile)
    destination.mkdir(parents=True, exist_ok=True)

    # 运行中的旧产物会占住 exe 与同目录 pdb，拷贝时报 WinError 32，先停掉它
    for name in PRODUCT_BINARIES:
        release_target(destination / name)

    wanted = PRODUCT_BINARIES + (DEBUG_SYMBOLS if pdb and profile == "dev" else ())
    # 不带 --pdb 时清掉上次留下的 pdb，免得与新 exe 对不上
    for name in set(DEBUG_SYMBOLS) - set(wanted):
        (source_dir / name).unlink(missing_ok=True)
        (destination / name).unlink(missing_ok=True)

    collected: list[tuple[str, object]] = []
    for name in wanted:
        source = source_dir / name
        if not source.is_file():
            print(warn(f"  {name} not produced, skipped"), flush=True)
            continue
        target = destination / name
        shutil.copy2(source, target)
        print(f"  {name:<14} {format_bytes(target.stat().st_size)}", flush=True)
        collected.append((profile, target))

    if not any(path.suffix == ".exe" for _, path in collected):
        raise SystemExit(f"No executable found in {source_dir}")
    return collected
