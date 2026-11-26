#!/usr/bin/env python3
"""Synchronize project skills without loading the build or debug toolchain."""

import sys

sys.dont_write_bytecode = True

import os

os.environ["PYTHONDONTWRITEBYTECODE"] = "1"

import argparse
import hashlib
import json
import shutil
import stat
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def reject_redirect(path: Path) -> None:
    status = path.lstat()
    if stat.S_ISLNK(status.st_mode) or getattr(status, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
        raise ValueError(f"Redirected paths are not supported: {path}")


def check_path(root: Path, path: Path) -> None:
    relative = path.relative_to(root)
    current = root
    for part in ("", *relative.parts):
        current = current / part
        if current.exists() or current.is_symlink():
            reject_redirect(current)
    path.resolve().relative_to(root)


def inventory(directory: Path) -> dict[str, str]:
    result = {}
    if not directory.exists():
        return result
    pending = [directory]
    while pending:
        for path in sorted(pending.pop().iterdir()):
            reject_redirect(path)
            if path.name == "__pycache__" or path.suffix in {".pyc", ".pyo"}:
                continue
            if path.is_dir():
                pending.append(path)
            elif path.is_file():
                result[path.relative_to(directory).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def synchronize(root: Path, *, check: bool) -> int:
    source = root / ".codex/skills"
    mirror = root / ".claude/skills"
    baseline_path = root / "build/skill-sync.json"
    for path in (source, mirror, baseline_path, root / "build/skill-sync-removed"):
        check_path(root, path)
    current = inventory(source)
    if not current:
        raise ValueError("Skill source is missing or empty: .codex/skills")
    target = inventory(mirror)
    changed = sorted(name for name in current.keys() | target.keys() if current.get(name) != target.get(name))
    if check:
        for name in changed:
            print(f"Mismatch: {name}")
        print(f"Skills: {len(current)} source files, {len(target)} mirror files, {len(changed)} differences.")
        return int(bool(changed))

    if baseline_path.exists():
        baseline = json.loads(baseline_path.read_text(encoding="utf-8"))["files"]
    elif target and target != current:
        raise ValueError("No sync baseline; reconcile the existing mirror with the source before syncing.")
    else:
        baseline = target
    drift = sorted(
        name for name in baseline.keys() | target.keys()
        if target.get(name) != baseline.get(name) and target.get(name) != current.get(name)
    )
    if drift:
        for name in drift:
            print(f"Independent mirror change: {name}", file=sys.stderr)
        raise ValueError("Merge mirror changes into .codex/skills before syncing.")

    removed = root / "build/skill-sync-removed" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    old_directories = set()
    for name in changed:
        if name in current:
            continue
        # 先归档已删除项，避免目录与文件互换时写入旧目录内部。
        destination = mirror / name
        backup = removed / name
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(destination, backup)
        directory = destination.parent
        while directory != mirror:
            old_directories.add(directory)
            directory = directory.parent
    for directory in sorted(old_directories, key=lambda path: len(path.parts), reverse=True):
        if not any(directory.iterdir()):
            directory.rmdir()
    for name in changed:
        if name not in current:
            continue
        destination = mirror / name
        if destination.is_dir():
            raise ValueError(f"Destination is still a directory: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / name, destination)
    if inventory(mirror) != current:
        raise ValueError("Skill mirror verification failed after sync.")
    baseline_path.parent.mkdir(parents=True, exist_ok=True)
    baseline_path.write_text(json.dumps({"files": current}, indent=2) + "\n", encoding="utf-8")
    print(f"Synchronized {len(changed)} changes; {len(current)} skill files match.")
    if removed.exists():
        print(f"Removed mirror files preserved in {removed.relative_to(root).as_posix()}.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--sync", action="store_true", help="Copy Codex skills to Claude after checking mirror drift")
    mode.add_argument("--check", action="store_true", help="Check file sets and bytes without writing anything")
    args = parser.parse_args()
    if not args.sync and not args.check:
        parser.print_help()
        return 0
    try:
        return synchronize(ROOT, check=args.check)
    except ValueError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(
            f"Skill sync failed: {type(error).__name__}; errno={error.errno}; "
            f"winerror={getattr(error, 'winerror', None)}; "
            f"filename={error.filename}; filename2={error.filename2}",
            file=sys.stderr,
        )
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
