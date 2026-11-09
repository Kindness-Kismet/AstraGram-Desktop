#!/usr/bin/env python3
"""在独立预发布中保存测试通道索引，按架构引用已发布的更新包。"""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import tempfile

from release_metadata import command, release_versions


FEED_TAG = "beta-updates"
PLATFORMS = {"win64", "winarm", "linux", "linuxarm", "mac", "armac"}


def merge_release_map(result: dict, release: dict, update_map: dict, version) -> None:
    assets = {asset["name"] for asset in release["assets"]}
    channel = version.channel
    for platform in PLATFORMS:
        entry = update_map.get(platform, {}).get(channel)
        if entry is None or channel in result.get(platform, {}):
            continue
        if entry.get("released") != version.update or entry.get("testing") != version.update:
            raise ValueError(f"Update code mismatch in {release['tag_name']}: {platform}")
        link = entry["link"]
        filename = link.removeprefix("/").replace("{version}", str(version.update))
        if not link.startswith("/") or "/" in filename or filename not in assets:
            raise ValueError(f"Missing update asset in {release['tag_name']}: {filename}")
        asset = next(asset for asset in release["assets"] if asset["name"] == filename)
        result.setdefault(platform, {})[channel] = {
            **entry, "link": asset["browser_download_url"],
        }


def main() -> None:
    repository = os.environ["GITHUB_REPOSITORY"]
    pages = json.loads(command("gh", "api", "--paginate", "--slurp", f"repos/{repository}/releases?per_page=100"))
    releases = [release for page in pages for release in page]
    result = {}
    for version, release in release_versions(releases):
        if all(version.channel in result.get(platform, {}) for platform in PLATFORMS):
            continue
        asset = next((asset for asset in release["assets"] if asset["name"] == "current6"), None)
        if asset is None:
            continue
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run([
                "gh", "release", "download", release["tag_name"], "--repo", repository,
                "--pattern", "current6", "--dir", directory,
            ], check=True)
            update_map = json.loads((Path(directory) / "current6").read_text(encoding="utf-8"))
        merge_release_map(result, release, update_map, version)
    if not result:
        raise ValueError("No published update packages are available")

    if not any(release["tag_name"] == FEED_TAG for release in releases):
        subprocess.run([
            "gh", "release", "create", FEED_TAG, "--repo", repository,
            "--target", os.environ["GITHUB_SHA"], "--prerelease", "--latest=false",
            "--title", "Beta update feed",
            "--notes", "Automatic update index. Download builds from the versioned releases.\n\n自动更新索引，请在带版本号的发布中下载安装包。",
        ], check=True)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "current6"
        path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        subprocess.run(["gh", "release", "upload", FEED_TAG, str(path), "--repo", repository, "--clobber"], check=True)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
