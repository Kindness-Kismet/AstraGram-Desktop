#!/usr/bin/env python3
"""按实际产出的 TDUP v2 更新包生成各架构的更新清单。"""

from __future__ import annotations

import sys

sys.dont_write_bytecode = True

import os

os.environ["PYTHONDONTWRITEBYTECODE"] = "1"

import argparse
import json
from pathlib import Path


UPDATE_TARGETS = {
    "win64": ("win", "x64"),
    "winarm": ("win", "arm"),
    "linux": ("linux", "x64"),
    "linuxarm": ("linux", "arm"),
    "mac": ("mac", "x64"),
    "armac": ("mac", "arm"),
}


def update_link(platform: str, channel: str) -> str:
    if channel not in ("stable", "beta"):
        raise ValueError(f"Unsupported update channel: {channel}")
    system, arch = UPDATE_TARGETS[platform]
    suffix = "-beta" if channel == "beta" else ""
    return f"/td-update-{system}-{arch}-{{version}}{suffix}"


def generate_update_map(artifacts: Path, version: int, channel: str = "stable") -> dict:
    if channel not in ("stable", "beta"):
        raise ValueError(f"Unsupported update channel: {channel}")
    if not 1_016 < version <= 999_999_999:
        raise ValueError("Update version must be within the range supported by Packer: 1017..999999999")
    result = {}
    for platform in UPDATE_TARGETS:
        link = update_link(platform, channel)
        package = artifacts / link.removeprefix("/").format(version=version)
        if not package.is_file():
            continue
        if not package.stat().st_size:
            raise ValueError(f"Update package is empty: {package.name}")
        result[platform] = {
            channel: {
                "released": version,
                "testing": version,
                "link": link,
            },
        }

    # 每个键只登记本架构的更新包；缺席平台的客户端本次不提示更新。
    if not result:
        raise ValueError("No TDUP v2 update packages found")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("version", type=int)
    parser.add_argument("--channel", choices=("stable", "beta"), default="stable")
    args = parser.parse_args()
    result = generate_update_map(args.artifacts, args.version, args.channel)
    output = json.dumps(result, indent=2) + "\n"
    (args.artifacts / "current6").write_text(output, encoding="utf-8")
    print(output, end="")


if __name__ == "__main__":
    main()
