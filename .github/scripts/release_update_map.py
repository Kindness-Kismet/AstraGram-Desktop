#!/usr/bin/env python3
"""按实际产出的更新包生成各架构的更新清单。"""

import argparse
import json
from pathlib import Path


def generate_update_map(artifacts: Path, version: int) -> dict:
    if not 1_016 < version <= 999_999_999:
        raise ValueError("Update version must be within the range supported by Packer: 1017..999999999")
    prefixes = {
        "win64": "tx64upd",
        "winarm": "tarm64upd",
        "linux": "tlinuxupd",
        "linuxarm": "tlinuxarmupd",
        "mac": "tmacupd",
        "armac": "tarmacupd",
    }
    result = {}
    for platform, prefix in prefixes.items():
        package = artifacts / f"{prefix}{version}"
        if not package.is_file():
            continue
        if not package.stat().st_size:
            raise ValueError(f"Update package is empty: {package.name}")
        result[platform] = {
            "stable": {
                "released": version,
                "testing": version,
                "link": f"/{prefix}{{version}}",
            },
        }

    # 每个键只登记本架构的更新包；缺席平台的客户端本次不提示更新。
    if not result:
        raise ValueError("No update packages found")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("version", type=int)
    args = parser.parse_args()
    result = generate_update_map(args.artifacts, args.version)
    output = json.dumps(result, indent=2) + "\n"
    (args.artifacts / "current6").write_text(output, encoding="utf-8")
    print(output, end="")


if __name__ == "__main__":
    main()
