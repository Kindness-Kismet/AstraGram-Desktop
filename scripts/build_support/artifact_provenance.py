"""生成和校验跨仓构建的产物来源清单。"""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path


SOURCE_REPOSITORY = "Kindness-Kismet/AyuGramDesktop-Plus"
SCHEMA_VERSION = 1
TARGETS = {
    "windows-x64": {
        "repository": "Kindness-Net/AyuGramDesktop-Plus-Windows-Build",
        "archive_platform": "win",
        "updater_prefixes": ("tx64upd",),
        "required": True,
    },
    "windows-arm64": {
        "repository": "Kindness-Net/AyuGramDesktop-Plus-Windows-ARM64-Build",
        "archive_platform": "win",
        "updater_prefixes": ("tarm64upd",),
        "required": False,
    },
    "linux-x64": {
        "repository": "Kindness-Net/AyuGramDesktop-Plus-Linux-Build",
        "archive_platform": "linux",
        "updater_prefixes": ("tlinuxupd",),
        "required": True,
    },
    "linux-arm64": {
        "repository": "Kindness-Net/AyuGramDesktop-Plus-Linux-Build",
        "archive_platform": "linux",
        "updater_prefixes": ("tlinuxarmupd",),
        "required": False,
    },
    "macos-x64": {
        "repository": "Kindness-Net/AyuGramDesktop-Plus-macOS-Build",
        "archive_platform": "macos",
        "updater_prefixes": ("tmacupd",),
        "required": True,
    },
    "macos-arm64": {
        "repository": "Kindness-Net/AyuGramDesktop-Plus-macOS-Build",
        "archive_platform": "macos",
        "updater_prefixes": ("tarmacupd",),
        "required": True,
    },
    # 兼容旧版 Universal builder 的来源清单；新 Release 不再声明这个目标。
    "macos-universal": {
        "repository": "Kindness-Net/AyuGramDesktop-Plus-macOS-Build",
        "archive_platform": "macos",
        "updater_prefixes": ("tmacupd", "tarmacupd"),
        "required": False,
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
        raise ProvenanceError("source SHA 必须是 40 位十六进制提交哈希")
    return value.lower()


def validate_repository(value: str) -> str:
    if not _REPOSITORY_PATTERN.fullmatch(value):
        raise ProvenanceError(f"仓库名格式无效：{value!r}")
    return value


def validate_source_ref(value: str) -> str:
    tag = value.removeprefix("refs/tags/")
    if tag == value or not tag or ".." in tag or "\\" in tag:
        raise ProvenanceError(f"source ref 必须是有效的 refs/tags/*：{value!r}")
    if any(character.isspace() or ord(character) < 32 for character in tag):
        raise ProvenanceError(f"source ref 包含无效字符：{value!r}")
    return value


def validate_version(value: str) -> str:
    if not _VERSION_PATTERN.fullmatch(value):
        raise ProvenanceError(f"版本格式无效：{value!r}")
    return value


def is_positive_integer(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def require_positive_integers(values: dict[str, object]) -> None:
    for name, value in values.items():
        if not is_positive_integer(value):
            raise ProvenanceError(f"{name} 必须是正整数")


def _target_key(platform: str, arch: str) -> str:
    key = f"{platform}-{arch}"
    if key not in TARGETS:
        raise ProvenanceError(f"不支持的构建目标：{key}")
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
        raise ProvenanceError(f"不受信任的 source 仓库：{source_repository}")
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
        raise ProvenanceError(f"{key} 必须由 {target['repository']} 构建")
    expected_output = f"provenance-{platform}-{arch}.json"
    if output.name != expected_output:
        raise ProvenanceError(f"来源清单必须命名为 {expected_output}")
    expected_names = _expected_filenames(platform, arch, version, appupdateversion)
    if len(files) != len(expected_names):
        raise ProvenanceError(f"{key} 必须记录完整的 archive 和 updater 文件集")
    if any(path.parent.resolve() != output.parent.resolve() for path in files):
        raise ProvenanceError("来源清单必须与 archive 和 updater 位于同一目录")

    actual_names = [path.name for path in files]
    if len(set(actual_names)) != len(actual_names):
        raise ProvenanceError("产物文件名重复")
    if set(actual_names) != expected_names:
        raise ProvenanceError(
            f"{key} 文件集不一致：期待 {sorted(expected_names)}，实际 {sorted(actual_names)}"
        )

    entries = []
    for path in files:
        if not path.is_file():
            raise ProvenanceError(f"产物文件不存在：{path}")
        size = path.stat().st_size
        if size <= 0:
            raise ProvenanceError(f"产物文件为空：{path}")
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
            raise ProvenanceError(f"JSON 字段重复：{key}")
        result[key] = value
    return result


def _parse_build_runs(raw: str) -> dict[str, dict]:
    try:
        build_runs = json.loads(raw, object_pairs_hook=_reject_duplicate_pairs)
    except (json.JSONDecodeError, UnicodeError) as error:
        raise ProvenanceError("--build-runs 必须是有效 JSON") from error
    if not isinstance(build_runs, dict):
        raise ProvenanceError("--build-runs 必须是对象")
    unknown = set(build_runs) - set(TARGETS)
    if unknown:
        raise ProvenanceError("--build-runs 包含未知目标：" + ", ".join(sorted(unknown)))
    missing = {key for key, target in TARGETS.items() if target["required"]} - set(build_runs)
    if missing:
        raise ProvenanceError("--build-runs 缺少必需目标：" + ", ".join(sorted(missing)))
    for key, build in build_runs.items():
        if not isinstance(build, dict) or set(build) != {"repository", "run_id", "run_attempt"}:
            raise ProvenanceError(f"{key} build run 必须只包含 repository、run_id、run_attempt")
        if build["repository"] != TARGETS[key]["repository"]:
            raise ProvenanceError(f"{key} builder 仓库不匹配")
        if not is_positive_integer(build["run_id"]) or not is_positive_integer(build["run_attempt"]):
            raise ProvenanceError(f"{key} builder run id 和 attempt 必须是正整数")
    return build_runs


def _read_manifest(path: Path) -> dict:
    try:
        manifest = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=_reject_duplicate_pairs,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ProvenanceError(f"无法读取来源清单 {path}: {error}") from error
    if not isinstance(manifest, dict):
        raise ProvenanceError(f"来源清单必须是 JSON 对象：{path}")
    return manifest


def _validate_manifest_shape(manifest: dict, path: Path) -> None:
    expected_keys = {
        "schema_version", "platform", "arch", "source", "builder",
        "version", "appupdateversion", "files",
    }
    if set(manifest) != expected_keys:
        raise ProvenanceError(f"来源清单字段不完整或包含未知字段：{path}")
    if not is_positive_integer(manifest["schema_version"]) or manifest["schema_version"] != SCHEMA_VERSION:
        raise ProvenanceError(f"不支持的来源清单 schema：{path}")
    if not isinstance(manifest["source"], dict) or set(manifest["source"]) != {
        "repository", "ref", "sha", "run_id", "run_attempt",
    }:
        raise ProvenanceError(f"source 字段格式无效：{path}")
    if not isinstance(manifest["builder"], dict) or set(manifest["builder"]) != {
        "repository", "run_id", "run_attempt",
    }:
        raise ProvenanceError(f"builder 字段格式无效：{path}")
    if not isinstance(manifest["files"], list) or not manifest["files"]:
        raise ProvenanceError(f"来源清单必须记录产物文件：{path}")


def _collect_artifacts(root: Path) -> tuple[list[Path], dict[str, Path]]:
    all_files = []
    for path in root.rglob("*"):
        if path.is_symlink():
            raise ProvenanceError(f"产物目录不允许符号链接：{path}")
        if path.is_file():
            all_files.append(path)
    by_name = {}
    for path in all_files:
        if path.name in by_name:
            raise ProvenanceError(f"产物文件名重复：{path.name}")
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
            raise ProvenanceError(f"来源清单文件名无效：{path.name}")
        key = _target_key(match.group(1), match.group(2))
        if key in result:
            raise ProvenanceError(f"来源清单重复：{key}")
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
            raise ProvenanceError(f"{path.name} 的 files 项格式无效")
        name = entry.get("name", "")
        if not name or name != Path(name).name or "/" in name or "\\" in name:
            raise ProvenanceError(f"产物清单只能记录文件名，不能包含路径：{name!r}")
        if name in entries or name in referenced_names:
            raise ProvenanceError(f"来源清单重复引用文件：{name}")
        if not is_positive_integer(entry.get("size")):
            raise ProvenanceError(f"来源清单文件大小无效：{name}")
        if not isinstance(entry.get("sha256"), str) or not _FILE_HASH_PATTERN.fullmatch(entry["sha256"]):
            raise ProvenanceError(f"来源清单 SHA-256 无效：{name}")
        entries[name] = entry
    if set(entries) != expected_names:
        raise ProvenanceError(f"{path.name} 的文件集与目标版本不匹配")
    for name, entry in entries.items():
        artifact = by_name.get(name)
        if artifact is None:
            raise ProvenanceError(f"缺少清单声明的产物：{name}")
        if artifact.stat().st_size != entry["size"]:
            raise ProvenanceError(f"产物大小与来源清单不匹配：{name}")
        if _sha256(artifact) != entry["sha256"]:
            raise ProvenanceError(f"产物 SHA-256 与来源清单不匹配：{name}")
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
        raise ProvenanceError(f"不受信任的 source 仓库：{source_repository}")
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
        raise ProvenanceError(f"产物目录不存在：{root}")

    all_files, by_name = _collect_artifacts(root)
    manifest_paths = _manifest_paths(all_files)
    if set(manifest_paths) != set(build_runs):
        missing = set(build_runs) - set(manifest_paths)
        extra = set(manifest_paths) - set(build_runs)
        details = []
        if missing:
            details.append("缺少 " + ", ".join(sorted(missing)))
        if extra:
            details.append("未声明 " + ", ".join(sorted(extra)))
        raise ProvenanceError("来源清单与 build runs 不一致：" + "；".join(details))

    referenced_names = set()
    for key, path in manifest_paths.items():
        manifest = _read_manifest(path)
        _validate_manifest_shape(manifest, path)
        platform, arch = key.split("-", 1)
        if manifest["platform"] != platform or manifest["arch"] != arch:
            raise ProvenanceError(f"来源清单目标与文件名不一致：{path.name}")
        if manifest["version"] != version or manifest["appupdateversion"] != appupdateversion:
            raise ProvenanceError(f"来源清单版本不匹配：{path.name}")
        expected_source = {
            "repository": source_repository,
            "ref": source_ref,
            "sha": source_sha,
            "run_id": source_run_id,
            "run_attempt": source_run_attempt,
        }
        source = manifest["source"]
        for field, expected in expected_source.items():
            actual = str(source.get(field, "")).lower() if field == "sha" else source.get(field)
            if actual != expected:
                raise ProvenanceError(f"{path.name} 的 source {field} 不匹配")
        if manifest["builder"] != build_runs[key]:
            raise ProvenanceError(f"{path.name} 的 builder run 不匹配")
        expected_names = _expected_filenames(platform, arch, version, appupdateversion)
        _validate_manifest_files(manifest, path, expected_names, by_name, referenced_names)

    allowed_names = referenced_names | {path.name for path in manifest_paths.values()}
    unexpected = set(by_name) - allowed_names
    if unexpected:
        raise ProvenanceError("产物目录包含未声明文件：" + ", ".join(sorted(unexpected)))
