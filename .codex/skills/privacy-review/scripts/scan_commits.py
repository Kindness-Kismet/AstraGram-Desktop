"""Inspect local main history for machine-specific paths without printing matches."""

import argparse
from functools import lru_cache
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile


MAX_BLOB_BYTES = 32 * 1024 * 1024
MAX_ARCHIVE_BYTES = 64 * 1024 * 1024
PATTERNS = {
    "absolute_path": re.compile(
        r"(?i)(?<!\w)[a-z]:[\\/][^\r\n\"<>]*"
        r"|\\\\[a-z0-9_.-]+\\[^\r\n\"<>]*"
        r"|/(?:home|Users|root|tmp|private/var|mnt/[a-z])/[\w./\\ -]+"
        r"|(?<![\w/])/[a-z]/[\w./\\ -]+"
    ),
}


def git(repo, *args):
    return subprocess.check_output(
        ["git", "--no-optional-locks", "-C", str(repo), *args],
        stderr=subprocess.PIPE,
    )


def resolve(repo, revision):
    return git(repo, "rev-parse", "--verify", "--end-of-options", revision + "^{commit}").decode().strip()


def changes(repo, commit, parent):
    args = ["diff-tree", "--root", "--no-commit-id", "--no-renames", "--raw", "--no-abbrev", "-r", "-z"]
    if parent:
        args.append(parent)
    args.append(commit)
    parts = git(repo, *args).split(b"\0")
    result = []
    for index in range(0, len(parts) - 1, 2):
        old_mode, new_mode, old, new, status = parts[index].decode().split()
        result.append({
            "path": parts[index + 1].decode("utf-8", "replace"),
            "status": status,
            "old_mode": old_mode.lstrip(":"),
            "new_mode": new_mode,
            "old": old,
            "new": new,
        })
    return result


def scan_text(raw, member=""):
    encoding = "utf-16" if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8"
    hits = []
    for number, line in enumerate(raw.decode(encoding, "replace").splitlines(), 1):
        for kind, pattern in PATTERNS.items():
            for match in pattern.finditer(line):
                # 指纹只在内存里比较父版本，报告不保存命中内容或其摘要。
                fingerprint = hashlib.sha256(match.group().encode()).hexdigest()
                hits.append({"kind": kind, "line": number, "member": member, "fingerprint": fingerprint})
    return hits


class Scanner:
    def __init__(self, repo):
        self.repo = repo
        self.blobs = set()
        self.objects = subprocess.Popen(
            ["git", "--no-optional-locks", "-C", str(repo), "cat-file", "--batch-command"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        )

    def close(self):
        self.objects.stdin.close()
        self.objects.stdout.close()
        self.objects.wait()

    def object_command(self, command, object_id):
        self.objects.stdin.write(f"{command} {object_id}\n".encode())
        self.objects.stdin.flush()
        header = self.objects.stdout.readline().split()
        if len(header) != 3 or header[1] != b"blob":
            raise OSError("Unable to read Git blob")
        return int(header[2])

    @lru_cache(maxsize=128)
    def blob(self, object_id):
        result = {"hits": [], "entries": [], "gaps": [], "binary": False, "size": 0}
        if set(object_id) == {"0"}:
            return result
        self.blobs.add(object_id)
        size = self.object_command("info", object_id)
        result["size"] = size
        if size > MAX_BLOB_BYTES:
            result["gaps"].append({"reason": "blob_size_limit"})
            return result
        self.object_command("contents", object_id)
        raw = self.objects.stdout.read(size)
        if len(raw) != size or self.objects.stdout.read(1) != b"\n":
            raise OSError("Incomplete Git blob")
        result["binary"] = b"\0" in raw[:8192]
        result["hits"] = scan_text(raw)
        if not zipfile.is_zipfile(io.BytesIO(raw)):
            if raw.startswith(b"PK\x03\x04"):
                result["gaps"].append({"reason": "invalid_zip"})
            return result
        total = 0
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            result["hits"].extend(scan_text(archive.comment, "<archive-comment>"))
            for index, entry in enumerate(archive.infolist()):
                member = f"<absolute-member-{index}>" if PATTERNS["absolute_path"].search(entry.filename) else entry.filename
                result["entries"].append({"path": member, "size": entry.file_size})
                result["hits"].extend(scan_text(entry.filename.encode(), member))
                result["hits"].extend(scan_text(entry.comment + entry.extra, member))
                total += entry.file_size
                if total > MAX_ARCHIVE_BYTES or entry.file_size > MAX_BLOB_BYTES:
                    result["gaps"].append({"member": member, "reason": "archive_size_limit"})
                    continue
                if entry.flag_bits & 1:
                    result["gaps"].append({"member": member, "reason": "encrypted_zip_member"})
                    continue
                try:
                    content = archive.read(entry)
                except (zipfile.BadZipFile, NotImplementedError, RuntimeError):
                    result["gaps"].append({"member": member, "reason": "unreadable_zip_member"})
                    continue
                result["hits"].extend(scan_text(content, member))
                if zipfile.is_zipfile(io.BytesIO(content)):
                    result["gaps"].append({"member": member, "reason": "nested_archive_requires_review"})
        return result

    def run(self, base, head):
        base = resolve(self.repo, base)
        head = resolve(self.repo, head)
        ancestor = git(self.repo, "merge-base", base, head).decode().strip()
        commits = git(self.repo, "rev-list", "--reverse", "--topo-order", head, "--not", base).decode().splitlines()
        result = {
            "base": base, "head": head, "merge_base": ancestor,
            "commits": [], "content_candidates": [],
            "binaries": [], "archives": [], "submodules": [], "coverage_gaps": [],
        }
        seen_hits, seen_binaries, seen_archives = set(), set(), set()
        paths = set()
        for commit in commits:
            parents = git(self.repo, "show", "-s", "--format=%P", commit).decode().split()
            changed = changes(self.repo, commit, parents[0] if parents else None)
            result["commits"].append({"commit": commit, "parents": parents, "files": changed})
            message = git(self.repo, "show", "-s", "--format=%B", commit)
            for hit in scan_text(message):
                result["content_candidates"].append({
                    "commit": commit, "path": "<commit-message>", "line": hit["line"],
                    "kind": hit["kind"], "introduced": True,
                })
            for change in changed:
                path, old, new = change["path"], change["old"], change["new"]
                paths.add(path)
                if "160000" in (change["old_mode"], change["new_mode"]):
                    result["submodules"].append({"commit": commit, **change})
                if change["new_mode"] in ("000000", "160000"):
                    continue
                scanned = self.blob(new)
                previous = self.blob(old) if change["old_mode"] not in ("000000", "160000") else {"hits": []}
                prior_hits = {(h["kind"], h["member"], h["fingerprint"]) for h in previous["hits"]}
                for hit in scanned["hits"]:
                    identity = (hit["kind"], hit["member"], hit["fingerprint"])
                    introduced = identity not in prior_hits
                    key = (path, new, hit["line"], identity, introduced)
                    if key in seen_hits:
                        continue
                    seen_hits.add(key)
                    result["content_candidates"].append({
                        "commit": commit, "path": path, "blob": new,
                        "kind": hit["kind"], "line": hit["line"],
                        "member": hit["member"], "introduced": introduced,
                    })
                for gap in scanned["gaps"]:
                    result["coverage_gaps"].append({"commit": commit, "path": path, "blob": new, **gap})
                if scanned["binary"] and (path, new) not in seen_binaries:
                    seen_binaries.add((path, new))
                    result["binaries"].append({"commit": commit, "path": path, "blob": new, "size": scanned["size"]})
                if scanned["entries"] and (path, new) not in seen_archives:
                    seen_archives.add((path, new))
                    result["archives"].append({"commit": commit, "path": path, "blob": new, "entries": scanned["entries"]})
        result["summary"] = {
            "commits": len(commits), "historical_paths": len(paths), "scanned_blobs": len(self.blobs),
            "introduced_content_candidates": sum(h["introduced"] for h in result["content_candidates"]),
            "binaries": len(result["binaries"]),
            "submodule_changes": len(result["submodules"]), "coverage_gaps": len(result["coverage_gaps"]),
        }
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--base", default="origin/main")
    parser.add_argument("--head", default="main")
    parser.add_argument("--output", type=Path, help="Optionally save the full commit manifest")
    args = parser.parse_args()
    try:
        scanner = Scanner(args.repo.resolve())
        try:
            result = scanner.run(args.base, args.head)
        finally:
            scanner.close()
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    except (subprocess.CalledProcessError, OSError, zipfile.BadZipFile) as error:
        print(f"Audit failed: {type(error).__name__}. No complete result was produced.", file=sys.stderr)
        return 2
    summary = result["summary"]
    visible = summary if args.output else {key: value for key, value in result.items() if key != "commits"}
    if not args.output:
        visible["content_candidates"] = [hit for hit in result["content_candidates"] if hit["introduced"]]
    print(json.dumps(visible, ensure_ascii=False, indent=2))
    if summary["coverage_gaps"]:
        return 2
    return int(bool(summary["introduced_content_candidates"]))


if __name__ == "__main__":
    sys.exit(main())
