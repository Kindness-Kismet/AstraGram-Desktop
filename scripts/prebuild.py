#!/usr/bin/env python3
import os
import sys

sys.dont_write_bytecode = True
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
# 进度输出含 · 与 ✅，cp1252/cp936 控制台会抛 UnicodeEncodeError，故自行接管标准流编码。
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

import argparse
import shutil
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_support.console import format_bytes, header, print_summary, utf8_output
from build_support.dependency_runner import run_stages
from build_support.help import MultilineHelpFormatter, format_choice_help
from build_support.paths import LIBRARIES_DIR, TARGET, TARGET_SUFFIX, THIRD_PARTY_DIR, TMP_DIR
from build_support.recipes import STAGE_NAMES, qt_version, resolved_stages
from build_support.timer import timed_step
from build_support.toolchain import describe_toolset, msvc_environment

STAGE_HELP = format_choice_help(
    "Build only the named stages, repeatable. Available stages:",
    [(name, "") for name in STAGE_NAMES],
)


def main() -> None:
    args = parse_args()

    if args.list:
        print(header("Available stages"))
        for stage in resolved_stages(TARGET):
            version = f"#{stage.version}" if stage.version != "0" else ""
            print(f"  {stage.name:<18} {stage.location}{version}")
        return

    if args.check_code_style is not None:
        from build_support.source_policy import check_sources

        try:
            result = check_sources(Path(__file__).resolve().parents[1], args.check_code_style)
        except ValueError as error:
            raise SystemExit(f"Invalid source selection: {error}") from error
        except OSError as error:
            raise SystemExit(
                f"Source check failed: {type(error).__name__}; errno={error.errno}; "
                f"winerror={getattr(error, 'winerror', None)}; filename={error.filename}"
            ) from error
        raise SystemExit(result)

    environment = msvc_environment()

    print(header("Current environment"))
    print(f"  Platform       Windows {TARGET_SUFFIX}")
    print(f"  Toolset        {describe_toolset(environment)}")
    print(f"  Python         {sys.version.split()[0]}")
    print(f"  Qt             {qt_version(TARGET)}")
    print(f"  Dependencies   {TMP_DIR}")
    print(f"  Proxy          {describe_proxy()}")
    print()

    if args.clean:
        with timed_step("Clean dependency cache"):
            clean_dependencies()

    with timed_step("Prepare third party libraries"):
        with utf8_output():
            built = run_stages(resolved_stages(TARGET), environment, args.stages, args.verbose)
        print(f"  {built} stage(s) built", flush=True)

    report()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fetch and build the AyuGram third party libraries into build/tmp",
        formatter_class=MultilineHelpFormatter,
    )
    parser.add_argument(
        "--stage",
        dest="stages",
        metavar="NAME",
        action="append",
        default=[],
        help=STAGE_HELP,
    )
    inspection = parser.add_mutually_exclusive_group()
    inspection.add_argument("--list", action="store_true", help="List available stages and exit")
    inspection.add_argument(
        "--check-code-style", nargs="*", metavar="PATH",
        help="Check the owned-code line limit without preparing dependencies;\n"
        "check all source inputs when no repository-relative paths are given",
    )
    parser.add_argument("--verbose", action="store_true", help="Print the commands of every stage before running")
    parser.add_argument("--clean", action="store_true", help="Remove build/tmp dependencies before preparing")
    args = parser.parse_args()
    if args.check_code_style is not None and (args.stages or args.clean or args.verbose):
        parser.error("--check-code-style cannot be combined with dependency preparation options")
    return args


def clean_dependencies() -> None:
    for path in (LIBRARIES_DIR, THIRD_PARTY_DIR):
        if path.exists():
            shutil.rmtree(path, ignore_errors=True)
            print(f"  removed {path}", flush=True)


def report() -> None:
    entries = [path for path in (LIBRARIES_DIR, THIRD_PARTY_DIR) if path.is_dir()]

    def size_of(path: Path) -> int:
        # 缓存恢复可能留下无法访问的符号链接，统计体积时跳过
        try:
            return path.stat().st_size if path.is_file() else 0
        except OSError:
            return 0

    total = sum(sum(size_of(f) for f in path.rglob("*")) for path in entries)
    print_summary(f"Location {TMP_DIR}", f"Total {format_bytes(total)}")


def describe_proxy() -> str:
    for key in ("HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy", "ALL_PROXY", "all_proxy"):
        value = os.environ.get(key)
        if value:
            return f"configured ({key})"
    return "none (direct system connection)"


if __name__ == "__main__":
    main()
