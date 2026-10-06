from datetime import datetime
from pathlib import Path


def recover_failed_compilations(output: Path, configuration: str, backups: Path) -> list[Path]:
    """保留失败项目的编译跟踪现场，让下一轮重新检查全部源文件。"""
    output = output.resolve()
    recovered = []
    destination = backups / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    for marker in sorted(output.rglob("unsuccessfulbuild")):
        tracking = marker.parent
        if tracking.parent.name != configuration or tracking.suffix.lower() != ".tlog":
            continue
        if not tracking.resolve().is_relative_to(output):
            raise RuntimeError(f"Compiler tracking directory is outside the build tree: {tracking}")
        files = [path for path in tracking.iterdir()
                 if path.is_file() and path.name.lower().startswith("cl.")
                 and path.suffix.lower() == ".tlog"]
        if not files:
            continue
        archive = destination / tracking.relative_to(output)
        archive.mkdir(parents=True)
        for path in files:
            path.rename(archive / path.name)
        recovered.append(tracking.relative_to(output))
    return recovered
