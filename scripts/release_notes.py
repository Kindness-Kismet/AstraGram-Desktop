#!/usr/bin/env python3
"""校验双语更新日志并生成 GitHub Release 正文。"""

import argparse
from pathlib import Path

from build_support.changelog import validate_changelog


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("changelog", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    notes = validate_changelog(args.changelog.read_text(encoding="utf-8"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(notes, encoding="utf-8")
    print(f"Release notes generated: {args.output}")


if __name__ == "__main__":
    main()
