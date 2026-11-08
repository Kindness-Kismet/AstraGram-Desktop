#!/usr/bin/env python3
"""为 main 和 beta 推送选择发布版本，保持更新码严格递增。"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from build_support.version import Version, parse_version, read_current_version


TAG_PATTERN = re.compile(r"v\d+\.\d+\.\d+(?:\.[1-9]\d*)?(?:-beta)?")


def command(*args: str) -> str:
    return subprocess.check_output(args, text=True, encoding="utf-8").strip()


def release_versions(releases: list[dict]) -> list[tuple[Version, dict]]:
    result = []
    for release in releases:
        tag = release["tag_name"]
        if release["draft"] or not TAG_PATTERN.fullmatch(tag):
            continue
        version = parse_version(tag[1:])
        if version.beta != release["prerelease"]:
            raise ValueError(f"Release channel does not match its tag: {tag}")
        result.append((version, release))
    return sorted(result, key=lambda pair: pair[0].update, reverse=True)


def published_versions() -> list[tuple[Version, dict]]:
    repository = os.environ["GITHUB_REPOSITORY"]
    pages = json.loads(command("gh", "api", "--paginate", "--slurp", f"repos/{repository}/releases?per_page=100"))
    return release_versions([release for page in pages for release in page])


def publication_state(version: Version, published: list[tuple[Version, dict]]) -> dict[str, str]:
    return {
        "published": str(any(release["tag_name"] == f"v{version.text_small}" for _, release in published)).lower(),
        "latest": str(not version.beta and not any(
            not candidate.beta and candidate.update > version.update for candidate, _ in published
        )).lower(),
    }


def choose_version(
    current: Version,
    branch: str,
    tags: dict[str, str],
    published: list[tuple[Version, dict]],
    sha: str,
) -> Version:
    if current.beta:
        raise ValueError("The committed version must describe the next stable release")
    if branch == "main":
        tag = f"v{current.text_small}"
        existing = tags.get(tag)
        if existing and existing != sha:
            raise ValueError(f"{tag} already belongs to another commit; bump the version")
        if not existing and published and current.update <= published[0][0].update:
            raise ValueError("Stable update code must exceed every published stable and beta version")
        return current
    if branch != "beta":
        raise ValueError(f"Unsupported release branch: {branch}")
    if published and current.full < published[0][0].full:
        raise ValueError("Merge the newer official baseline into beta before publishing")

    revision = current.revision
    for tag, target in tags.items():
        if not TAG_PATTERN.fullmatch(tag):
            continue
        version = parse_version(tag[1:])
        if version.full != current.full:
            continue
        if version.beta and target == sha:
            return version
        revision = max(revision, version.revision)
    if revision >= 99:
        raise ValueError("All 99 revision slots are used; adapt a newer official baseline before publishing")
    return parse_version(f"{current.text}.{revision + 1}-beta")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--publication-state", help="Refresh publication state immediately before uploading")
    args = parser.parse_args()
    output = Path(os.environ["GITHUB_OUTPUT"])
    if args.publication_state:
        state = publication_state(parse_version(args.publication_state), published_versions())
        with output.open("a", encoding="utf-8") as stream:
            stream.writelines(f"{key}={value}\n" for key, value in state.items())
        return
    branch = os.environ["GITHUB_REF_NAME"]
    sha = os.environ["GITHUB_SHA"]
    before = os.environ["BEFORE_SHA"]
    if branch == "main" and before != "0" * 40:
        changed = command("git", "diff", "--name-only", before, sha, "--", "Telegram/build/version")
        if not changed:
            with output.open("a", encoding="utf-8") as stream:
                stream.write("enabled=false\n")
            print("No stable version change; release skipped.")
            return

    published = published_versions()
    tag_lines = command("git", "for-each-ref", "--format=%(refname:strip=2) %(objectname) %(*objectname)", "refs/tags")
    tags = {}
    for line in tag_lines.splitlines():
        parts = line.split()
        tags[parts[0]] = parts[-1]
    version = choose_version(parse_version(read_current_version()), branch, tags, published, sha)
    previous = ""
    for candidate, release in published:
        if candidate.update >= version.update:
            continue
        tag = release["tag_name"]
        if subprocess.run(["git", "merge-base", "--is-ancestor", tag, sha], check=False).returncode == 0:
            previous = tag
            break
    values = {
        "enabled": "true", "version": version.text_small, "tag": f"v{version.text_small}",
        "appversion": str(version.update), "channel": version.channel, "previous": previous,
    }
    with output.open("a", encoding="utf-8") as stream:
        stream.writelines(f"{key}={value}\n" for key, value in values.items())
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a", encoding="utf-8") as stream:
        stream.write(f"## AstraGram {version.text_small}\n\nChannel: {version.channel}\n\nUpdate code: {version.update}\n\nSource: `{sha}`\n")
    print(json.dumps(values, indent=2))


if __name__ == "__main__":
    main()
