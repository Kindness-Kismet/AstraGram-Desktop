#!/usr/bin/env python3
"""裁掉 Windows 依赖构建中间文件，同时保留 Qt 安装目标引用的对象。"""

import argparse
from pathlib import Path

_INTERMEDIATE_SUFFIXES = {".obj", ".pdb", ".pch"}


def is_qt_installed_object(path: Path) -> bool:
    parts = path.parts
    for index, part in enumerate(parts):
        if part.startswith("Qt-"):
            relative = parts[index + 1 : -1]
            return any(item.startswith("objects-") for item in relative)
    return False


def trim_intermediate_files(roots: list[Path]) -> list[Path]:
    """删除可重建中间文件，并按 objects-* 目录保留 Qt CMake 引用的安装对象。"""
    removed = []
    for root in roots:
        if not root.is_dir():
            raise FileNotFoundError(f"Cache root not found: {root}")
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in _INTERMEDIATE_SUFFIXES:
                continue
            if is_qt_installed_object(path):
                continue
            path.unlink()
            removed.append(path)
    return removed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", nargs="+", type=Path)
    args = parser.parse_args()
    removed = trim_intermediate_files(args.roots)
    print(f"Removed {len(removed)} intermediate files.")


if __name__ == "__main__":
    main()
