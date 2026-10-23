"""生成和校验跨仓构建的产物来源清单。"""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path


SOURCE_REPOSITORY = "Kindness-Kismet/AstraGram-Desktop"
SCHEMA_VERSION = 1
TARGETS = {
    "windows-x64": {
        "repository": "Kindness-Net/AstraGram-Desktop-Windows-Build",
        "archive_platform": "win",
        "updater_prefixes": ("tx64upd",),
    },
    "windows-arm64": {
        "repository": "Kindness-Net/AstraGram-Desktop-Windows-ARM64-Build",
        "archive_platform": "win",
        "updater_prefixes": ("tarm64upd",),
    },
    "linux-x64": {
        "repository": "Kindness-Net/AstraGram-Desktop-Linux-Build",
        "archive_platform": "linux",
        "updater_prefixes": ("tlinuxupd",),
    },
    "linux-arm64": {
        "repository": "Kindness-Net/AstraGram-Desktop-Linux-Build",
        "archive_platform": "linux",
        "updater_prefixes": ("tlinuxarmupd",),
    },
    "macos-x64": {
        "repository": "Kindness-Net/AstraGram-Desktop-macOS-Build",
        "archive_platform": "macos",
        "updater_prefixes": ("tmacupd",),
    },
    "macos-arm64": {
        "repository": "Kindness-Net/AstraGram-Desktop-macOS-Build",
        "archive_platform": "macos",
        "updater_prefixes": ("tarmacupd",),
    },
    # 兼容旧版 Universal builder 的来源清单；新 Release 不再声明这个目标。
    "macos-universal": {
        "repository": "Kindness-Net/AstraGram-Desktop-macOS-Build",
        "archive_platform": "macos",
        "updater_prefixes": ("tmacupd", "tarmacupd"),
    },
}

_SHA_PATTERN = re.compile(r"[0-9a-fA-F]{40}")
_FILE_HASH_PATTERN = re.compile(r"[0-9a-f]{64}")
_VERSION_PATTERN = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+(?:\.(?:[0-9]+|beta))?")
_REPOSITORY_PATTERN = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")


class ProvenanceError(ValueError):
    """表示来源或产物没有满足发布信任约束。"""


def validate_sha(value: str) -> str:
    if not _SHA_PATTERN.fullmatch(value):
        raise ProvenanceError("Source SHA must be a 40-character hexadecimal commit hash")
    return value.lower()


def validate_repository(value: str) -> str:
    if not _REPOSITORY_PATTERN.fullmatch(value):
        raise ProvenanceError(f"Invalid repository name: {value!r}")
    return value


def validate_source_ref(value: str) -> str:
    tag = value.removeprefix("refs/tags/")
    if tag == value or not tag or ".." in tag or "\\" in tag:
        raise ProvenanceError(f"Source ref must be a valid refs/tags/* reference: {value!r}")
    if any(character.isspace() or ord(character) < 32 for character in tag):
        raise ProvenanceError(f"Source ref contains invalid characters: {value!r}")
    return value


def validate_version(value: str) -> str:
    if not _VERSION_PATTERN.fullmatch(value):
        raise ProvenanceError(f"Invalid version format: {value!r}")
    return value


def is_positive_integer(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def require_positive_integers(values: dict[str, object]) -> None:
    for name, value in values.items():
        if not is_positive_integer(value):
            raise ProvenanceError(f"{name} must be a positive integer")


def _target_key(platform: str, arch: str) -> str:
    key = f"{platform}-{arch}"
    if key not in TARGETS:
        raise ProvenanceError(f"Unsupported build target: {key}")
    return key


def _expected_filenames(platform: str, arch: str, version: str, appupdateversion: int) -> set[str]:
    target = TARGETS[_target_key(platform, arch)]
    return {
        f"AstraGram-v{version}-{target['archive_platform']}-{arch}.zip",
        *(f"{prefix}{appupdateversion}" for prefix in target["updater_prefixes"]),
    }


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_artifact_manifest(
    *,
    platform: str,
    arch: str,
    source_repository: str,
    source_ref: str,
    source_sha: str,
    source_run_id: int,
    source_run_attempt: int,
    builder_repository: str,
    builder_run_id: int,
    builder_run_attempt: int,
    version: str,
    appupdateversion: int,
    output: Path,
    files: list[Path],
) -> dict:
    """散列单个架构的完整文件集，并写入与 artifact 同包的来源清单。"""
    key = _target_key(platform, arch)
    target = TARGETS[key]
    validate_repository(source_repository)
    if source_repository != SOURCE_REPOSITORY:
        raise ProvenanceError(f"Untrusted source repository: {source_repository}")
    source_ref = validate_source_ref(source_ref)
    source_sha = validate_sha(source_sha)
    version = validate_version(version)
    require_positive_integers(
        {
            "source run id": source_run_id,
            "source run attempt": source_run_attempt,
            "builder run id": builder_run_id,
            "builder run attempt": builder_run_attempt,
            "AppUpdateVersion": appupdateversion,
        }
    )
    validate_repository(builder_repository)
    if builder_repository != target["repository"]:
        raise ProvenanceError(f"{key} must be built by {target['repository']}")
    expected_output = f"provenance-{platform}-{arch}.json"
    if output.name != expected_output:
        raise ProvenanceError(f"Provenance manifest must be named {expected_output}")
    expected_names = _expected_filenames(platform, arch, version, appupdateversion)
    if len(files) != len(expected_names):
        raise ProvenanceError(f"{key} must record the complete archive and updater file set")
    if any(path.parent.resolve() != output.parent.resolve() for path in files):
        raise ProvenanceError("Provenance manifest must be in the same directory as the archive and updater")

    actual_names = [path.name for path in files]
    if len(set(actual_names)) != len(actual_names):
        raise ProvenanceError("Duplicate artifact filename")
    if set(actual_names) != expected_names:
        raise ProvenanceError(
            f"{key} file set mismatch: expected {sorted(expected_names)}, got {sorted(actual_names)}"
        )

    entries = []
    for path in files:
        if not path.is_file():
            raise ProvenanceError(f"Artifact file does not exist: {path}")
        size = path.stat().st_size
        if size <= 0:
            raise ProvenanceError(f"Artifact file is empty: {path}")
        entries.append({"name": path.name, "size": size, "sha256": _sha256(path)})

    manifest = {
        "schema_version": SCHEMA_VERSION,
        "platform": platform,
        "arch": arch,
        "source": {
            "repository": source_repository,
            "ref": source_ref,
            "sha": source_sha,
            "run_id": source_run_id,
            "run_attempt": source_run_attempt,
        },
        "builder": {
            "repository": builder_repository,
            "run_id": builder_run_id,
            "run_attempt": builder_run_attempt,
        },
        "version": version,
        "appupdateversion": appupdateversion,
        "files": sorted(entries, key=lambda entry: entry["name"]),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def _reject_duplicate_pairs(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ProvenanceError(f"Duplicate JSON field: {key}")
        result[key] = value
    return result


def _parse_build_runs(raw: str) -> dict[str, dict]:
    try:
        build_runs = json.loads(raw, object_pairs_hook=_reject_duplicate_pairs)
    except (json.JSONDecodeError, UnicodeError) as error:
        raise ProvenanceError("--build-runs must be valid JSON") from error
    if not isinstance(build_runs, dict):
        raise ProvenanceError("--build-runs must be an object")
    unknown = set(build_runs) - set(TARGETS)
    if unknown:
        raise ProvenanceError("--build-runs contains unknown targets: " + ", ".join(sorted(unknown)))
    # 单个平台失败不阻塞其余平台发布，至少要有一个目标。
    if not build_runs:
        raise ProvenanceError("--build-runs requires at least one target")
    for key, build in build_runs.items():
        if not isinstance(build, dict) or set(build) != {"repository", "run_id", "run_attempt"}:
            raise ProvenanceError(f"{key} build run must contain only repository, run_id and run_attempt")
        if build["repository"] != TARGETS[key]["repository"]:
            raise ProvenanceError(f"{key} builder repository mismatch")
        if not is_positive_integer(build["run_id"]) or not is_positive_integer(build["run_attempt"]):
            raise ProvenanceError(f"{key} builder run ID and attempt must be positive integers")
    return build_runs


def _read_manifest(path: Path) -> dict:
    try:
        manifest = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=_reject_duplicate_pairs,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ProvenanceError(f"Could not read provenance manifest {path}: {error}") from error
    if not isinstance(manifest, dict):
        raise ProvenanceError(f"Provenance manifest must be a JSON object: {path}")
    return manifest


def _validate_manifest_shape(manifest: dict, path: Path) -> None:
    expected_keys = {
        "schema_version", "platform", "arch", "source", "builder",
        "version", "appupdateversion", "files",
    }
    if set(manifest) != expected_keys:
        raise ProvenanceError(f"Provenance manifest has missing or unknown fields: {path}")
    if not is_positive_integer(manifest["schema_version"]) or manifest["schema_version"] != SCHEMA_VERSION:
        raise ProvenanceError(f"Unsupported provenance manifest schema: {path}")
    if not isinstance(manifest["source"], dict) or set(manifest["source"]) != {
        "repository", "ref", "sha", "run_id", "run_attempt",
    }:
        raise ProvenanceError(f"Invalid source field: {path}")
    if not isinstance(manifest["builder"], dict) or set(manifest["builder"]) != {
        "repository", "run_id", "run_attempt",
    }:
        raise ProvenanceError(f"Invalid builder field: {path}")
    if not isinstance(manifest["files"], list) or not manifest["files"]:
        raise ProvenanceError(f"Provenance manifest must record artifact files: {path}")


def _collect_artifacts(root: Path) -> tuple[list[Path], dict[str, Path]]:
    all_files = []
    for path in root.rglob("*"):
        if path.is_symlink():
            raise ProvenanceError(f"Symbolic links are not allowed in the artifact directory: {path}")
        if path.is_file():
            all_files.append(path)
    by_name = {}
    for path in all_files:
        if path.name in by_name:
            raise ProvenanceError(f"Duplicate artifact filename: {path.name}")
        by_name[path.name] = path
    return all_files, by_name


def _manifest_paths(all_files: list[Path]) -> dict[str, Path]:
    result = {}
    for path in all_files:
        if not path.name.startswith("provenance-") or not path.name.endswith(".json"):
            continue
        match = re.fullmatch(
            r"provenance-(windows|linux|macos)-(x64|arm64|universal)\.json",
            path.name,
        )
        if not match:
            raise ProvenanceError(f"Invalid provenance manifest filename: {path.name}")
        key = _target_key(match.group(1), match.group(2))
        if key in result:
            raise ProvenanceError(f"Duplicate provenance manifest: {key}")
        result[key] = path
    return result


def _validate_manifest_files(
    manifest: dict,
    path: Path,
    expected_names: set[str],
    by_name: dict[str, Path],
    referenced_names: set[str],
) -> None:
    entries = {}
    for entry in manifest["files"]:
        if not isinstance(entry, dict) or set(entry) != {"name", "size", "sha256"}:
            raise ProvenanceError(f"{path.name} has an invalid files entry")
        name = entry.get("name", "")
        if not name or name != Path(name).name or "/" in name or "\\" in name:
            raise ProvenanceError(f"Artifact manifests must record filenames without paths: {name!r}")
        if name in entries or name in referenced_names:
            raise ProvenanceError(f"Provenance manifest references a file more than once: {name}")
        if not is_positive_integer(entry.get("size")):
            raise ProvenanceError(f"Invalid file size in provenance manifest: {name}")
        if not isinstance(entry.get("sha256"), str) or not _FILE_HASH_PATTERN.fullmatch(entry["sha256"]):
            raise ProvenanceError(f"Invalid SHA-256 in provenance manifest: {name}")
        entries[name] = entry
    if set(entries) != expected_names:
        raise ProvenanceError(f"{path.name} file set does not match the target version")
    for name, entry in entries.items():
        artifact = by_name.get(name)
        if artifact is None:
            raise ProvenanceError(f"Artifact declared by the manifest is missing: {name}")
        if artifact.stat().st_size != entry["size"]:
            raise ProvenanceError(f"Artifact size does not match the provenance manifest: {name}")
        if _sha256(artifact) != entry["sha256"]:
            raise ProvenanceError(f"Artifact SHA-256 does not match the provenance manifest: {name}")
    referenced_names.update(entries)


def verify_artifacts(
    root: Path,
    *,
    source_repository: str,
    source_ref: str,
    source_sha: str,
    source_run_id: int,
    source_run_attempt: int,
    version: str,
    appupdateversion: int,
    build_runs_json: str,
) -> None:
    """验证声明构建的完整来源链和文件内容，确保主仓库只发布匹配产物。"""
    if source_repository != SOURCE_REPOSITORY:
        raise ProvenanceError(f"Untrusted source repository: {source_repository}")
    source_ref = validate_source_ref(source_ref)
    source_sha = validate_sha(source_sha)
    version = validate_version(version)
    require_positive_integers(
        {
            "source run id": source_run_id,
            "source run attempt": source_run_attempt,
            "AppUpdateVersion": appupdateversion,
        }
    )
    build_runs = _parse_build_runs(build_runs_json)
    if not root.is_dir():
        raise ProvenanceError(f"Artifact directory does not exist: {root}")

    all_files, by_name = _collect_artifacts(root)
    manifest_paths = _manifest_paths(all_files)
    if set(manifest_paths) != set(build_runs):
        missing = set(build_runs) - set(manifest_paths)
        extra = set(manifest_paths) - set(build_runs)
        details = []
        if missing:
            details.append("Missing " + ", ".join(sorted(missing)))
        if extra:
            details.append("Undeclared " + ", ".join(sorted(extra)))
        raise ProvenanceError("Provenance manifests do not match build runs: " + "; ".join(details))

    referenced_names = set()
    for key, path in manifest_paths.items():
        manifest = _read_manifest(path)
        _validate_manifest_shape(manifest, path)
        platform, arch = key.split("-", 1)
        if manifest["platform"] != platform or manifest["arch"] != arch:
            raise ProvenanceError(f"Provenance manifest target does not match its filename: {path.name}")
        if manifest["version"] != version or manifest["appupdateversion"] != appupdateversion:
            raise ProvenanceError(f"Provenance manifest version mismatch: {path.name}")
        expected_source = {
            "repository": source_repository,
            "ref": source_ref,
            "sha": source_sha,
            "run_id": source_run_id,
        }
        source = manifest["source"]
        for field, expected in expected_source.items():
            actual = str(source.get(field, "")).lower() if field == "sha" else source.get(field)
            if actual != expected:
                raise ProvenanceError(f"{path.name} source {field} mismatch")
        # 重跑失败任务时，已成功平台沿用旧 attempt 的产物；同一 run 的各 attempt 共用同一提交。
        attempt = source.get("run_attempt")
        if not is_positive_integer(attempt) or attempt > source_run_attempt:
            raise ProvenanceError(
                f"{path.name} source run_attempt must be between 1 and {source_run_attempt}, got {attempt!r}"
            )
        if manifest["builder"] != build_runs[key]:
            raise ProvenanceError(f"{path.name} builder run mismatch")
        expected_names = _expected_filenames(platform, arch, version, appupdateversion)
        _validate_manifest_files(manifest, path, expected_names, by_name, referenced_names)

    allowed_names = referenced_names | {path.name for path in manifest_paths.values()}
    unexpected = set(by_name) - allowed_names
    if unexpected:
        raise ProvenanceError("Artifact directory contains undeclared files: " + ", ".join(sorted(unexpected)))
