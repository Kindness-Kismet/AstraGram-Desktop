from datetime import datetime
from pathlib import Path
import shutil


def _blocks(path: Path) -> list[tuple[str, list[str]]]:
    result = []
    for line in path.read_text(encoding="utf-16").splitlines():
        if not line:
            continue
        if line.startswith("^"):
            result.append((line, []))
        elif result:
            result[-1][1].append(line)
        else:
            raise RuntimeError(f"Invalid compiler tracking record: {path}")
    return result


def _sources(key: str) -> set[str]:
    return {source.casefold() for source in key[1:].split("|")}


def recover_failed_compilations(output: Path, configuration: str, backups: Path) -> list[Path]:
    """作废不完整的编译记录，保留已成功的编译单元与预编译头。"""
    output = output.resolve()
    recovered = []
    destination = backups / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    for marker in sorted(output.rglob("unsuccessfulbuild")):
        tracking = marker.parent
        if tracking.parent.name != configuration or tracking.suffix.lower() != ".tlog":
            continue
        if not tracking.resolve().is_relative_to(output):
            raise RuntimeError(f"Compiler tracking directory is outside the build tree: {tracking}")
        files = {path.name.lower(): path for path in tracking.iterdir()
                 if path.is_file() and path.suffix.lower() == ".tlog"}
        reads, writes = set(), set()
        for name, path in files.items():
            target = (reads if name.startswith("cl.read.") else
                      writes if name.startswith("cl.write.") else None)
            if target is not None:
                for key, values in _blocks(path):
                    # 预编译头加载失败仍会留下读写记录，旧 obj 不能据此视为编译成功。
                    complete = values and (target is writes or any(
                        not value.lower().endswith(".pch") for value in values
                    ))
                    if complete:
                        target.update(_sources(key))
        complete = reads & writes
        changed = []
        for name, path in files.items():
            if not name.startswith("cl.command."):
                continue
            blocks = _blocks(path)
            retained = [(key, values) for key, values in blocks
                        if values and _sources(key) <= complete]
            if len(retained) != len(blocks):
                changed.append((path, retained))
        if not changed:
            continue
        archive = destination / tracking.relative_to(output)
        archive.mkdir(parents=True)
        for path, retained in changed:
            shutil.copy2(path, archive / path.name)
            contents = "".join("\n".join([key, *values]) + "\n"
                               for key, values in retained)
            path.write_text(contents, encoding="utf-16")
        recovered.append(tracking.relative_to(output))
    return recovered
