"""Check the line limit for owned source inputs, including untracked files."""

import os
import stat
from collections.abc import Iterator
from pathlib import Path

LINE_LIMIT = 6000
SOURCE_ROOTS = (
    "Telegram/SourceFiles", "Telegram/Resources", "Telegram/cmake",
    "Telegram/build", "Telegram/shaders", "scripts", ".github/scripts",
    ".github/workflows", ".codex/skills", ".claude/skills",
)
ROOT_INPUTS = (
    "CMakeLists.txt", "Telegram/CMakeLists.txt", "Telegram/configure.py",
    "Telegram/configure.bat", "Telegram/configure.sh", "Telegram/create.bat",
)
CODE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".m", ".mm",
    ".py", ".pyi", ".sh", ".bash", ".ps1", ".bat", ".cmd", ".rs", ".js",
    ".ts", ".tsx", ".jsx", ".qml", ".style", ".cmake", ".comp", ".frag",
    ".vert", ".idl", ".css", ".html", ".yml", ".yaml", ".vbs", ".iss",
    ".xml", ".qrc", ".rc", ".manifest", ".props",
}
DEPENDENCY_PATHS = (
    "Telegram/SourceFiles/extras/libs/json.hpp",
    "Telegram/SourceFiles/extras/libs/sqlite",
)
GENERATED_DIRECTORIES = {"__pycache__", ".git", ".cache", ".venv", "CMakeFiles"}


def within(name: str, directory: str) -> bool:
    return name == directory or name.startswith(directory + "/")


def is_owned_code(root: Path, path: Path) -> bool:
    relative = path.relative_to(root)
    name = relative.as_posix()
    if any(part in GENERATED_DIRECTORIES for part in relative.parts):
        return False
    if any(within(name, dependency) for dependency in DEPENDENCY_PATHS):
        return False
    if name in ROOT_INPUTS:
        return True
    return (
        any(within(name, directory) for directory in SOURCE_ROOTS)
        and (
            path.suffix in CODE_SUFFIXES
            or path.name in {"CMakeLists.txt", "Dockerfile"}
            or path.name.endswith((".h.in", ".hpp.in"))
        )
    )


def check_input_path(root: Path, path: Path) -> None:
    relative = path.relative_to(root)
    current = root
    for part in ("", *relative.parts):
        current = current / part
        status = current.lstat()
        if stat.S_ISLNK(status.st_mode) or getattr(status, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError(f"Redirected source paths are not supported: {relative.as_posix()}")
    path.resolve().relative_to(root)


def walk_inputs(root: Path, start: Path) -> Iterator[Path]:
    pending = [start]
    while pending:
        path = pending.pop()
        relative = path.relative_to(root)
        if any(part in GENERATED_DIRECTORIES for part in relative.parts):
            continue
        if any(within(relative.as_posix(), dependency) for dependency in DEPENDENCY_PATHS):
            continue
        check_input_path(root, path)
        if path.is_dir():
            pending.extend(path.iterdir())
        elif path.is_file():
            yield path


def source_files(root: Path, requested: list[str]) -> list[Path]:
    inputs = [root / name for name in (requested or (*SOURCE_ROOTS, *ROOT_INPUTS))]
    found = set()
    for item in inputs:
        item = Path(os.path.abspath(item))
        check_input_path(root, item)
        selected = {path for path in walk_inputs(root, item) if is_owned_code(root, path)}
        if requested and not selected:
            raise ValueError(f"No owned code inputs selected: {item.relative_to(root).as_posix()}")
        found.update(selected)
    return sorted(found)


def check_sources(root: Path, requested: list[str]) -> int:
    paths = source_files(root, requested)
    violations = []
    for path in paths:
        count = len(path.read_bytes().splitlines())
        if count > LINE_LIMIT:
            violations.append((path.relative_to(root).as_posix(), count))
    for name, count in violations:
        print(f"Over limit: {name}: {count} lines (maximum {LINE_LIMIT})")
    print(f"Code files: {len(paths)} checked, {len(violations)} over the {LINE_LIMIT}-line limit.")
    return int(bool(violations))
