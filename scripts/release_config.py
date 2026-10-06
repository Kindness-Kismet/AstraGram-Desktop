#!/usr/bin/env python3
"""为发版工作流和远程来源校验生成相同的分支、版本与通道约束。"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from build_support.paths import ROOT
from build_support.version import read_version_file, validate_baseline


def release_config(root: Path, branch: str) -> dict[str, str]:
    if branch not in ("main", "dev"):
        raise ValueError(f"Releases are only allowed from main or dev, got {branch!r}.")
    version = read_version_file(root / "Telegram/build/version")
    tracking = json.loads((root / ".github/upstream.json").read_text(encoding="utf-8"))
    validate_baseline(version, tracking)
    if branch == "main" and version.beta:
        raise ValueError("main cannot publish beta releases; finish stable promotion on dev first.")
    # 新建 dev 和回合 main 修复时可以带入稳定版版本文件，此时不发版。
    publish = branch == "main" or version.beta
    return {
        "publish": str(publish).lower(),
        "version": version.text_small,
        "tag": f"v{version.original}",
        "name": version.original,
        "appversion": str(version.update),
        "prerelease": str(version.beta).lower(),
        "make_latest": str(not version.beta).lower(),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--branch", required=True, choices=("main", "dev"))
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    config = release_config(args.root, args.branch)
    output = "".join(f"{key}={value}\n" for key, value in config.items())
    if args.output:
        with args.output.open("a", encoding="utf-8") as stream:
            stream.write(output)
    print(output, end="")


if __name__ == "__main__":
    main()
