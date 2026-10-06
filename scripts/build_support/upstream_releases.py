"""解析官方发布标签，并区分稳定版与测试版目标。"""
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


def targets(items: list[Release], channel: str) -> list[Release]:
    # beta 通道也接收稳定版，避免正式发布后无法继续向前同步。
    return [item for item in items if channel == "beta" or item.channel == "stable"]


def select_target(items: list[Release], version: str | None, channel: str) -> Release:
    candidates = targets(items, channel)
    if version:
        target = pick(items, version)
        if target not in candidates:
            raise SystemExit(f"Upstream {version} is a beta release; use --channel beta on dev.")
        return target
    if not candidates:
        raise SystemExit(f"Upstream has no releases for channel {channel}.")
    return candidates[-1]
