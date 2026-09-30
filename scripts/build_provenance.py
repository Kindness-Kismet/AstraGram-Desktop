#!/usr/bin/env python3
"""校验分布式构建来源，并为发布产物生成和验证来源清单。"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Callable

from build_support.artifact_provenance import (
    SOURCE_REPOSITORY,
    TARGETS,
    ProvenanceError,
    require_positive_integers,
    validate_repository,
    validate_sha,
    validate_source_ref,
    validate_version,
    verify_artifacts,
    write_artifact_manifest,
)


ALLOWED_SOURCE_EVENTS = {"push"}
# 发版由推送 main 上的版本文件触发，tag 由该运行自行创建。
SOURCE_BRANCH = "main"
SOURCE_WORKFLOW = ".github/workflows/build-release.yml"
SOURCE_RUN_API_ATTEMPTS = 4
SOURCE_RUN_API_TIMEOUT_SECONDS = 15
MAX_RETRY_DELAY_SECONDS = 30


def _positive_int(value: str) -> int:
    result = int(value)
    if result <= 0:
        raise argparse.ArgumentTypeError("必须是正整数")
    return result


def _read_version_fields(root: Path) -> dict[str, str]:
    path = root / "Telegram/build/version"
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ProvenanceError(f"无法读取版本文件 {path}: {error}") from error
    fields = {}
    for line in lines:
        parts = line.split()
        if len(parts) != 2:
            continue
        name, value = parts
        if name in fields:
            raise ProvenanceError(f"版本文件字段重复：{name}")
        fields[name] = value
    return fields


def _git_head(root: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=root,
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise ProvenanceError("无法读取当前 checkout 的 Git HEAD") from error
    return result.stdout.strip()


def _http_error_diagnostic(error: urllib.error.HTTPError) -> str:
    parts = [f"HTTP {error.code}"]
    headers = (
        ("X-GitHub-Request-Id", "request_id"),
        ("X-RateLimit-Remaining", "rate_limit_remaining"),
        ("X-RateLimit-Reset", "rate_limit_reset"),
        ("X-RateLimit-Resource", "rate_limit_resource"),
        ("Retry-After", "retry_after"),
    )
    for header, label in headers:
        value = error.headers.get(header) if error.headers else None
        if value:
            parts.append(f"{label}={value[:128]}")
    return ", ".join(parts)


def _network_error_diagnostic(error: OSError) -> str:
    reason = getattr(error, "reason", error)
    details = type(reason).__name__
    error_number = getattr(reason, "errno", None)
    if error_number is not None:
        details += f", errno={error_number}"
    return details


def _is_transient_http_error(error: urllib.error.HTTPError) -> bool:
    if error.code == 429 or 500 <= error.code <= 599:
        return True
    if error.code != 403 or not error.headers:
        return False
    return bool(error.headers.get("Retry-After")) or error.headers.get("X-RateLimit-Remaining") == "0"


def _retry_delay(error: OSError, attempt: int) -> float:
    delay = float(2 ** (attempt - 1))
    if isinstance(error, urllib.error.HTTPError) and error.headers:
        retry_after = error.headers.get("Retry-After")
        try:
            delay = max(delay, float(retry_after)) if retry_after is not None else delay
        except ValueError:
            pass
    return min(delay, MAX_RETRY_DELAY_SECONDS)


def fetch_workflow_run(
    repository: str,
    run_id: int,
    run_attempt: int,
    *,
    opener: Callable[..., object] | None = None,
    sleep: Callable[[float], None] = time.sleep,
) -> dict:
    """读取 source run attempt，并仅对短暂传输故障做有限重试。"""
    repository_path = urllib.parse.quote(repository, safe="/")
    url = (
        f"https://api.github.com/repos/{repository_path}/actions/runs/"
        f"{run_id}/attempts/{run_attempt}"
    )
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "AstraGram-build-provenance",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    token = os.environ.get("GH_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers)
    open_request = opener or urllib.request.urlopen
    for attempt in range(1, SOURCE_RUN_API_ATTEMPTS + 1):
        try:
            with open_request(request, timeout=SOURCE_RUN_API_TIMEOUT_SECONDS) as response:
                payload = json.load(response)
        except urllib.error.HTTPError as error:
            diagnostic = _http_error_diagnostic(error)
            transient = _is_transient_http_error(error)
            error.close()
            if transient and attempt < SOURCE_RUN_API_ATTEMPTS:
                sleep(_retry_delay(error, attempt))
                continue
            raise ProvenanceError(
                f"GitHub API 无法确认 source workflow run"
                f"（尝试 {attempt}/{SOURCE_RUN_API_ATTEMPTS}，{diagnostic}）"
            ) from error
        except OSError as error:
            diagnostic = _network_error_diagnostic(error)
            if attempt < SOURCE_RUN_API_ATTEMPTS:
                sleep(_retry_delay(error, attempt))
                continue
            raise ProvenanceError(
                f"GitHub API 无法确认 source workflow run（尝试 {attempt}/{SOURCE_RUN_API_ATTEMPTS}，"
                f"网络错误 {diagnostic}）"
            ) from error
        except (UnicodeError, json.JSONDecodeError) as error:
            raise ProvenanceError("GitHub API source workflow run 返回无效 JSON") from error
        if not isinstance(payload, dict):
            raise ProvenanceError("GitHub API 返回的 source workflow run 格式无效")
        return payload
    raise AssertionError("unreachable")


def validate_source(
    root: Path,
    *,
    repository: str,
    sha: str,
    ref: str,
    run_id: int,
    run_attempt: int,
    version: str,
    appupdateversion: int,
    workflow_path: str,
    run_loader: Callable[[str, int, int], dict] | None = None,
) -> None:
    """同时核对 checkout、版本文件和 GitHub source run，防止构建来源被替换。"""
    validate_repository(repository)
    if repository != SOURCE_REPOSITORY:
        raise ProvenanceError(f"不受信任的 source 仓库：{repository}")
    sha = validate_sha(sha)
    ref = validate_source_ref(ref)
    version = validate_version(version)
    if ref != f"refs/tags/v{version}":
        raise ProvenanceError(f"source tag 与版本不一致：{ref!r}")
    require_positive_integers(
        {"source run id": run_id, "source run attempt": run_attempt, "AppUpdateVersion": appupdateversion}
    )
    if workflow_path != SOURCE_WORKFLOW:
        raise ProvenanceError(f"不受信任的 source workflow：{workflow_path}")
    if validate_sha(_git_head(root)) != sha:
        raise ProvenanceError("当前 checkout HEAD 与 source SHA 不一致")

    fields = _read_version_fields(root)
    expected_versions = {
        "AppVersionStrSmall": version,
        "AppVersionStr": version,
        "AppVersionOriginal": version,
        "AppUpdateVersion": str(appupdateversion),
    }
    for name, expected in expected_versions.items():
        if fields.get(name) != expected:
            raise ProvenanceError(
                f"Telegram/build/version 的 {name} 不一致：期待 {expected!r}，实际 {fields.get(name)!r}"
            )

    loader = run_loader or fetch_workflow_run
    run = loader(repository, run_id, run_attempt)
    actual_repository = run.get("repository", {}).get("full_name")
    checks = {
        "run id": (run.get("id"), run_id),
        "run attempt": (run.get("run_attempt"), run_attempt),
        "repository": (actual_repository, repository),
        "head SHA": (str(run.get("head_sha", "")).lower(), sha),
        "head branch": (run.get("head_branch"), SOURCE_BRANCH),
        "workflow path": (run.get("path"), workflow_path),
    }
    for label, (actual, expected) in checks.items():
        if actual != expected:
            raise ProvenanceError(f"source workflow {label} 不一致：期待 {expected!r}，实际 {actual!r}")
    if run.get("event") not in ALLOWED_SOURCE_EVENTS:
        raise ProvenanceError(f"不允许的 source workflow 事件：{run.get('event')!r}")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    source = commands.add_parser("validate-source", help="验证 source checkout 和 GitHub workflow run")
    source.add_argument("--repository", required=True)
    source.add_argument("--sha", required=True)
    source.add_argument("--ref", required=True)
    source.add_argument("--run-id", type=_positive_int, required=True)
    source.add_argument("--run-attempt", type=_positive_int, required=True)
    source.add_argument("--version", required=True)
    source.add_argument("--appupdateversion", type=_positive_int, required=True)
    source.add_argument("--workflow-path", required=True)

    write = commands.add_parser("write-artifact-manifest", help="生成目标产物来源清单")
    write.add_argument("--platform", choices=("windows", "linux", "macos"), required=True)
    write.add_argument("--arch", choices=("x64", "arm64", "universal"), required=True)
    for name in (
        "source-repository", "source-ref", "source-sha", "builder-repository", "version",
    ):
        write.add_argument(f"--{name}", required=True)
    for name in (
        "source-run-id", "source-run-attempt", "builder-run-id", "builder-run-attempt",
        "appupdateversion",
    ):
        write.add_argument(f"--{name}", type=_positive_int, required=True)
    write.add_argument("--output", type=Path, required=True)
    write.add_argument("--file", type=Path, action="append", required=True)

    verify = commands.add_parser("verify-artifacts", help="验证主仓库下载的全部发布产物")
    verify.add_argument("--root", type=Path, required=True)
    for name in ("source-repository", "source-ref", "source-sha", "version", "build-runs"):
        verify.add_argument(f"--{name}", required=True)
    for name in ("source-run-id", "source-run-attempt", "appupdateversion"):
        verify.add_argument(f"--{name}", type=_positive_int, required=True)
    return parser


def main() -> None:
    parser = _build_parser()
    args = parser.parse_args()
    try:
        if args.command == "validate-source":
            validate_source(
                Path.cwd(), repository=args.repository, sha=args.sha, ref=args.ref,
                run_id=args.run_id, run_attempt=args.run_attempt, version=args.version,
                appupdateversion=args.appupdateversion, workflow_path=args.workflow_path,
            )
        elif args.command == "write-artifact-manifest":
            write_artifact_manifest(
                platform=args.platform, arch=args.arch,
                source_repository=args.source_repository, source_ref=args.source_ref,
                source_sha=args.source_sha, source_run_id=args.source_run_id,
                source_run_attempt=args.source_run_attempt,
                builder_repository=args.builder_repository, builder_run_id=args.builder_run_id,
                builder_run_attempt=args.builder_run_attempt, version=args.version,
                appupdateversion=args.appupdateversion, output=args.output, files=args.file,
            )
        else:
            verify_artifacts(
                args.root, source_repository=args.source_repository, source_ref=args.source_ref,
                source_sha=args.source_sha, source_run_id=args.source_run_id,
                source_run_attempt=args.source_run_attempt, version=args.version,
                appupdateversion=args.appupdateversion, build_runs_json=args.build_runs,
            )
    except ProvenanceError as error:
        print(f"build provenance error: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
