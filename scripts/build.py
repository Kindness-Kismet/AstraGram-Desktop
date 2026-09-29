#!/usr/bin/env python3
import os
import sys

sys.dont_write_bytecode = True
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
# 进度输出含 · 与 ✅，cp1252/cp936 控制台会抛 UnicodeEncodeError，故自行接管标准流编码。
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

import argparse
import subprocess
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_support.builder import (
    DEFAULT_BUILD_JOBS,
    MAX_BUILD_JOBS,
    build,
    cmake_executable,
    output_dir,
)
from build_support.console import header, utf8_output
from build_support.help import MultilineHelpFormatter
from build_support.paths import (
    DEFAULT_API_HASH,
    DEFAULT_API_ID,
    LIBRARIES_ARCH_DIR,
    ROOT,
    TARGET_SUFFIX,
    TMP_DIR,
)
from build_support.toolchain import describe_toolset, msvc_environment


def main() -> None:
    args = parse_args()
    environment = msvc_environment()
    print_environment(args, environment)
    with utf8_output():
        build(
            configurations=args.configurations,
            api_id=args.api_id,
            api_hash=args.api_hash,
            reconfigure=args.reconfigure,
            jobs=args.jobs,
            pack=args.pack,
            clean_pack=args.clean_pack,
        )


def print_environment(args: argparse.Namespace, environment: dict[str, str]) -> None:
    configurations = ", ".join(c.capitalize() for c in args.configurations)
    outputs = ", ".join(str(output_dir(p).relative_to(ROOT)) for p in sorted(args.configurations))

    print(header("Current build environment"))
    print(f"  Platform       Windows {TARGET_SUFFIX}")
    print(f"  Toolset        {describe_toolset(environment)}")
    print(f"  Configuration  {configurations}")
    print(f"  Compile jobs   {args.jobs} (maximum: {MAX_BUILD_JOBS})")
    print(f"  CMake          {get_tool_version([cmake_executable(environment), '--version'])}")
    print(f"  Dependencies   {describe_dependencies()}")
    print(f"  Output         {outputs}")
    print()


def describe_dependencies() -> str:
    state = "ready" if LIBRARIES_ARCH_DIR.is_dir() else "missing (run prebuild.py)"
    return f"{state}  {TMP_DIR}"


def get_tool_version(command: list[str]) -> str:
    try:
        result = subprocess.run(command, capture_output=True, text=True, errors="replace", timeout=5)
        return result.stdout.strip().splitlines()[0] if result.stdout.strip() else "Unknown"
    except Exception:
        return "Unknown"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Build AstraGram into versioned directories under build/.\n"
            "Builds Release by default; Debug also collects AstraGram.pdb."
        ),
        formatter_class=MultilineHelpFormatter,
    )
    configuration = parser.add_mutually_exclusive_group()
    configuration.add_argument(
        "--dev",
        action="store_true",
        help="Build Debug instead of Release",
    )
    configuration.add_argument("--all", action="store_true", help="Build both Debug and Release")
    parser.add_argument("--api-id", metavar="ID", default=DEFAULT_API_ID, help="Telegram API id")
    parser.add_argument("--api-hash", metavar="HASH", default=DEFAULT_API_HASH, help="Telegram API hash")
    parser.add_argument("--reconfigure", action="store_true", help="Discard the CMake cache before configuring")
    parser.add_argument(
        "--jobs",
        metavar="N",
        type=int,
        default=DEFAULT_BUILD_JOBS,
        help=f"Parallel compile jobs, 1-{MAX_BUILD_JOBS} (default: {DEFAULT_BUILD_JOBS})",
    )
    parser.add_argument(
        "--pack",
        action="store_true",
        help="Package the output directory into a zip archive under build/",
    )
    parser.add_argument(
        "--clean-pack",
        action="store_true",
        help="Remove runtime leftovers from the output directory before packaging",
    )
    args = parser.parse_args()
    if not 1 <= args.jobs <= MAX_BUILD_JOBS:
        parser.error(f"--jobs must be between 1 and {MAX_BUILD_JOBS}")
    args.configurations = resolve_configurations(args)
    return args


def resolve_configurations(args: argparse.Namespace) -> list[str]:
    if args.all:
        return ["dev", "release"]

    if args.dev:
        return ["dev"]

    return ["release"]


if __name__ == "__main__":
    main()
