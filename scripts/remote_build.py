#!/usr/bin/env python3
"""触发并等待一个可精确追踪的跨仓 GitHub Actions 构建。"""

from __future__ import annotations

import argparse
import json
import os
from dataclasses import dataclass
from pathlib import Path
import re
import signal
import sys
import time
from typing import Callable, Mapping
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen


_REPOSITORY = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
_SHA = re.compile(r"^[0-9a-fA-F]{40}$")
_VERSION = re.compile(r"^[0-9]+(?:\.[0-9]+)+$")
_API_VERSION = "2026-03-10"


class RemoteBuildError(RuntimeError):
    """表示远程构建无法安全完成或无法证明其来源。"""


class RemoteBuildInterrupted(RemoteBuildError):
    """表示本地编排收到终止信号，需要取消远程构建。"""


class TransientGitHubApiError(RemoteBuildError):
    """表示只适合由幂等读取操作短暂重试的 GitHub API 故障。"""


@dataclass(frozen=True)
class RunDetails:
    repository: str
    run_id: int
    run_attempt: int
    status: str
    conclusion: str | None
    url: str


class GitHubApi:
    """通过固定超时的 GitHub REST 请求调度、查询和取消工作流。"""

    def __init__(
        self,
        token: str,
        timeout_seconds: float = 30,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        if not token:
            raise ValueError("GitHub token 不能为空")
        self._token = token
        self._timeout_seconds = timeout_seconds
        self._sleep = sleep

    def _request(
        self,
        method: str,
        path: str,
        *,
        payload: Mapping[str, object] | None = None,
        expected_statuses: tuple[int, ...] = (200,),
        timeout_seconds: float | None = None,
    ) -> dict[str, object]:
        data = None if payload is None else json.dumps(payload).encode("utf-8")
        request = Request(
            "https://api.github.com" + path,
            data=data,
            method=method,
            headers={
                "Accept": "application/vnd.github+json",
                "Authorization": f"Bearer {self._token}",
                "Content-Type": "application/json",
                "User-Agent": "AstraGram-remote-build",
                "X-GitHub-Api-Version": _API_VERSION,
            },
        )
        try:
            with urlopen(
                request,
                timeout=timeout_seconds or self._timeout_seconds,
            ) as response:
                status = response.status
                body = response.read()
        except HTTPError as error:
            message = error.read().decode("utf-8", errors="replace")[:1000]
            error_type = (
                TransientGitHubApiError
                if 500 <= error.code < 600
                else RemoteBuildError
            )
            raise error_type(
                f"GitHub API {method} {path} 返回 HTTP {error.code}: {message}"
            ) from error
        except (URLError, TimeoutError) as error:
            reason = getattr(error, "reason", str(error))
            raise TransientGitHubApiError(
                f"GitHub API {method} {path} 连接失败: {reason}"
            ) from error

        if status not in expected_statuses:
            raise RemoteBuildError(
                f"GitHub API {method} {path} 返回意外状态 {status}"
            )
        if not body:
            return {}
        try:
            parsed = json.loads(body)
        except json.JSONDecodeError as error:
            raise RemoteBuildError(
                f"GitHub API {method} {path} 返回了无效 JSON"
            ) from error
        if not isinstance(parsed, dict):
            raise RemoteBuildError(
                f"GitHub API {method} {path} 返回值不是对象"
            )
        return parsed

    def dispatch_workflow(
        self,
        repository: str,
        workflow: str,
        ref: str,
        inputs: Mapping[str, str],
    ) -> int:
        owner, name = _split_repository(repository)
        path = (
            f"/repos/{quote(owner)}/{quote(name)}/actions/workflows/"
            f"{quote(workflow, safe='')}/dispatches"
        )
        response = self._request(
            "POST",
            path,
            payload={
                "ref": ref,
                "inputs": dict(inputs),
                "return_run_details": True,
            },
        )
        run_id = response.get("workflow_run_id")
        if not isinstance(run_id, int) or run_id <= 0:
            raise RemoteBuildError(
                "dispatch 响应缺少有效 workflow_run_id；拒绝查询最新运行来猜测"
            )
        return run_id

    def get_run(self, repository: str, run_id: int) -> dict[str, object]:
        owner, name = _split_repository(repository)
        path = f"/repos/{quote(owner)}/{quote(name)}/actions/runs/{run_id}"
        for attempt in range(3):
            try:
                return self._request("GET", path)
            except TransientGitHubApiError:
                if attempt == 2:
                    raise
                delay = attempt + 1
                print(
                    f"transient GET failure for {repository} run {run_id}; "
                    f"retrying in {delay}s",
                    file=sys.stderr,
                    flush=True,
                )
                self._sleep(delay)
        raise AssertionError("unreachable")

    def cancel_run(self, repository: str, run_id: int) -> None:
        owner, name = _split_repository(repository)
        self._request(
            "POST",
            f"/repos/{quote(owner)}/{quote(name)}/actions/runs/{run_id}/cancel",
            expected_statuses=(202,),
            timeout_seconds=5,
        )


def _split_repository(repository: str) -> tuple[str, str]:
    if not _REPOSITORY.fullmatch(repository):
        raise ValueError(f"无效仓库名: {repository}")
    owner, name = repository.split("/", 1)
    return owner, name


def _checked_input(name: str, value: str) -> str:
    if not value or "\n" in value or "\r" in value or "\0" in value:
        raise ValueError(f"无效输入 {name}")
    return value


def parse_run_details(
    payload: Mapping[str, object],
    *,
    repository: str,
    workflow: str,
    ref: str,
    run_id: int,
) -> RunDetails:
    """核对返回运行确属本次调度，避免接受其他仓库或工作流的结果。"""
    if payload.get("id") != run_id:
        raise RemoteBuildError(f"远程运行 ID 与 dispatch 响应不一致: {run_id}")

    repository_data = payload.get("repository")
    actual_repository = (
        repository_data.get("full_name")
        if isinstance(repository_data, Mapping)
        else None
    )
    if actual_repository != repository:
        raise RemoteBuildError(
            f"远程运行仓库不一致: 期望 {repository}，实际 {actual_repository}"
        )
    if payload.get("event") != "workflow_dispatch":
        raise RemoteBuildError("远程运行不是 workflow_dispatch")

    expected_path = (
        workflow
        if workflow.startswith(".github/workflows/")
        else f".github/workflows/{workflow}"
    )
    actual_path = payload.get("path")
    if not isinstance(actual_path, str) or actual_path.split("@", 1)[0] != expected_path:
        raise RemoteBuildError(
            f"远程工作流不一致: 期望 {expected_path}，实际 {actual_path}"
        )
    if payload.get("head_branch") != ref:
        raise RemoteBuildError(
            f"远程运行分支不一致: 期望 {ref}，实际 {payload.get('head_branch')}"
        )

    attempt = payload.get("run_attempt")
    status = payload.get("status")
    conclusion = payload.get("conclusion")
    if not isinstance(attempt, int) or attempt <= 0:
        raise RemoteBuildError("远程运行缺少有效 run_attempt")
    if not isinstance(status, str) or not status:
        raise RemoteBuildError("远程运行缺少有效 status")
    if conclusion is not None and not isinstance(conclusion, str):
        raise RemoteBuildError("远程运行 conclusion 类型无效")

    url = f"https://github.com/{repository}/actions/runs/{run_id}"
    return RunDetails(
        repository=repository,
        run_id=run_id,
        run_attempt=attempt,
        status=status,
        conclusion=conclusion,
        url=url,
    )


def run_remote_build(
    api: GitHubApi,
    *,
    repository: str,
    workflow: str,
    ref: str,
    inputs: Mapping[str, str],
    timeout_seconds: float,
    poll_interval_seconds: float,
    sleep: Callable[[float], None] = time.sleep,
    monotonic: Callable[[], float] = time.monotonic,
    on_dispatched: Callable[[str, int, str], None] | None = None,
) -> RunDetails:
    """等待精确 dispatch 的终态，并在异常退出时尽力取消同一运行。"""
    if timeout_seconds <= 0 or poll_interval_seconds <= 0:
        raise ValueError("超时和轮询间隔必须大于零")
    deadline = monotonic() + timeout_seconds
    run_id: int | None = None
    terminal = False
    last_state: tuple[int, str] | None = None
    highest_attempt = 0
    try:
        run_id = api.dispatch_workflow(repository, workflow, ref, inputs)
        run_url = f"https://github.com/{repository}/actions/runs/{run_id}"
        print(f"dispatched {repository} run {run_id}: {run_url}", flush=True)
        if on_dispatched is not None:
            on_dispatched(repository, run_id, run_url)

        while True:
            remaining = deadline - monotonic()
            if remaining <= 0:
                raise RemoteBuildError(
                    f"等待远程构建超时: {timeout_seconds:g}s; {run_url}"
                )
            details = parse_run_details(
                api.get_run(repository, run_id),
                repository=repository,
                workflow=workflow,
                ref=ref,
                run_id=run_id,
            )
            if details.run_attempt < highest_attempt:
                raise RemoteBuildError("远程 run_attempt 出现回退")
            highest_attempt = details.run_attempt
            state = (details.run_attempt, details.status)
            if state != last_state:
                print(
                    f"{repository} run {run_id} attempt {details.run_attempt}: "
                    f"{details.status}",
                    flush=True,
                )
                last_state = state

            if details.status == "completed":
                terminal = True
                if details.conclusion != "success":
                    raise RemoteBuildError(
                        f"远程构建失败: {details.conclusion}; {details.url}"
                    )
                return details

            remaining = deadline - monotonic()
            if remaining <= 0:
                raise RemoteBuildError(
                    f"等待远程构建超时: {timeout_seconds:g}s; {details.url}"
                )
            sleep(min(poll_interval_seconds, remaining))
    finally:
        if run_id is not None and not terminal:
            cancel_remote_run(api, repository, run_id)


def cancel_remote_run(api: GitHubApi, repository: str, run_id: int) -> bool:
    """按已记录的精确 ID 尽力取消远程运行，不用查询结果猜测目标。"""
    if run_id <= 0:
        raise ValueError("远程 run ID 必须是正整数")
    try:
        api.cancel_run(repository, run_id)
        print(f"cancel requested for {repository} run {run_id}", flush=True)
        return True
    except Exception as error:  # 取消是尽力清理，不能覆盖原始失败。
        print(
            f"warning: unable to cancel {repository} run {run_id}: {error}",
            file=sys.stderr,
            flush=True,
        )
        return False


def _append_dispatched_outputs(
    path: Path | None,
    repository: str,
    run_id: int,
    url: str,
) -> None:
    if path is None:
        return
    with path.open("a", encoding="utf-8") as output:
        output.write(f"repository={repository}\n")
        output.write(f"run_id={run_id}\n")
        output.write(f"run_url={url}\n")


def _append_success_outputs(path: Path | None, details: RunDetails) -> None:
    if path is None:
        return
    with path.open("a", encoding="utf-8") as output:
        output.write(f"run_attempt={details.run_attempt}\n")


def _append_dispatched_summary(
    path: Path | None,
    label: str,
    repository: str,
    run_id: int,
    url: str,
) -> None:
    if path is None:
        return
    with path.open("a", encoding="utf-8") as summary:
        summary.write(f"### {label}\n\n")
        summary.write(f"[{repository} run {run_id}]({url})\n")


def _append_success_summary(path: Path | None, details: RunDetails) -> None:
    if path is None:
        return
    with path.open("a", encoding="utf-8") as summary:
        summary.write(f"\nResult: success (attempt {details.run_attempt})\n")


def _path_from_argument(value: str | None, environment_name: str) -> Path | None:
    selected = value or os.environ.get(environment_name)
    return Path(selected) if selected else None


def _install_signal_handlers() -> dict[int, object]:
    previous: dict[int, object] = {}

    def interrupt(signum: int, _frame: object) -> None:
        name = signal.Signals(signum).name
        raise RemoteBuildInterrupted(f"收到 {name}，正在取消远程构建")

    for signum in (signal.SIGINT, signal.SIGTERM):
        previous[signum] = signal.getsignal(signum)
        signal.signal(signum, interrupt)
    return previous


def _restore_signal_handlers(previous: Mapping[int, object]) -> None:
    for signum, handler in previous.items():
        signal.signal(signum, handler)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--workflow", default="build.yml")
    parser.add_argument("--ref", default="main")
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--source-ref", required=True)
    parser.add_argument("--source-run-id", required=True)
    parser.add_argument("--source-run-attempt", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--appupdateversion", required=True)
    parser.add_argument("--timeout-seconds", type=float, default=21_300)
    parser.add_argument("--poll-interval-seconds", type=float, default=30)
    parser.add_argument("--github-output")
    parser.add_argument("--step-summary")
    args = parser.parse_args()

    _split_repository(args.repository)
    inputs = {
        "source_sha": _checked_input("source_sha", args.source_sha),
        "source_ref": _checked_input("source_ref", args.source_ref),
        "source_run_id": _checked_input("source_run_id", args.source_run_id),
        "source_run_attempt": _checked_input(
            "source_run_attempt", args.source_run_attempt
        ),
        "version": _checked_input("version", args.version),
        "appupdateversion": _checked_input(
            "appupdateversion", args.appupdateversion
        ),
    }
    if not _SHA.fullmatch(inputs["source_sha"]):
        parser.error("--source-sha 必须是 40 位十六进制提交")
    if not _VERSION.fullmatch(inputs["version"]):
        parser.error("--version 格式无效")
    for name in ("source_run_id", "source_run_attempt", "appupdateversion"):
        if not inputs[name].isdigit() or int(inputs[name]) <= 0:
            parser.error(f"--{name.replace('_', '-')} 必须是正整数")

    try:
        token = os.environ.get("REMOTE_BUILD_TOKEN", "")
        api = GitHubApi(token)
        output_path = _path_from_argument(args.github_output, "GITHUB_OUTPUT")
        summary_path = _path_from_argument(args.step_summary, "GITHUB_STEP_SUMMARY")

        def record_dispatch(repository: str, run_id: int, url: str) -> None:
            _append_dispatched_outputs(output_path, repository, run_id, url)
            _append_dispatched_summary(
                summary_path,
                args.label,
                repository,
                run_id,
                url,
            )

        previous_handlers = _install_signal_handlers()
        try:
            details = run_remote_build(
                api,
                repository=args.repository,
                workflow=args.workflow,
                ref=args.ref,
                inputs=inputs,
                timeout_seconds=args.timeout_seconds,
                poll_interval_seconds=args.poll_interval_seconds,
                on_dispatched=record_dispatch,
            )
        finally:
            _restore_signal_handlers(previous_handlers)
    except (RemoteBuildError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    _append_success_outputs(output_path, details)
    _append_success_summary(summary_path, details)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
