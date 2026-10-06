"""解析官方发布标签，按版本号选择最新目标。"""
from __future__ import annotations

from dataclasses import dataclass
import re


@dataclass(frozen=True)
class Release:
    version: str
    commit: str
    date: str
    channel: str = "stable"

    @property
    def key(self) -> tuple[int, ...]:
        return tuple(int(part) for part in self.version.split("."))


def parse_releases(raw: str) -> list[Release]:
    result = []
    for line in raw.splitlines():
        name, sha, subject, date = line.split("\t")
        if not re.fullmatch(r"v\d+\.\d+\.\d+", name):
            continue
        version = name[1:]
        match = re.match(rf"(Version|Beta version) {re.escape(version)}(?:\.|$)", subject)
        if match:
            channel = "beta" if match[1] == "Beta version" else "stable"
            result.append(Release(version, sha, date, channel))
    return sorted(result, key=lambda release: release.key)


def pick(items: list[Release], version: str) -> Release:
    for release in items:
        if release.version == version:
            return release
    raise SystemExit(f"Upstream has no release {version}.")


def select_target(items: list[Release], version: str | None) -> Release:
    if version:
        return pick(items, version)
    if not items:
        raise SystemExit("Upstream has no releases.")
    return max(items, key=lambda release: release.key)
