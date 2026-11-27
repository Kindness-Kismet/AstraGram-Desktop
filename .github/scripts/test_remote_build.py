from __future__ import annotations

import io
import sys
import unittest
from email.message import Message
from pathlib import Path
from unittest.mock import patch
from urllib.error import HTTPError

sys.path.insert(0, str(Path(__file__).resolve().parent))

from remote_build import (
    GitHubApi,
    RemoteBuildError,
    RemoteRunNotFoundError,
    TransientGitHubApiError,
    run_remote_build,
)


class _FakeClock:
    def __init__(self) -> None:
        self.now = 0.0
        self.sleeps: list[float] = []

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.sleeps.append(seconds)
        self.now += seconds


class _FakeApi:
    def __init__(self, responses: list[object]) -> None:
        self._responses = list(responses)
        self.cancelled: list[tuple[str, int]] = []
        self.calls = 0

    def dispatch_workflow(self, repository, workflow, ref, inputs) -> int:
        return 45819923795

    def get_run(self, repository: str, run_id: int) -> dict[str, object]:
        self.calls += 1
        item = self._responses.pop(0)
        if isinstance(item, Exception):
            raise item
        return item

    def cancel_run(self, repository: str, run_id: int) -> None:
        self.cancelled.append((repository, run_id))


def _run_payload(status: str, conclusion: str | None = None) -> dict[str, object]:
    return {
        "id": 45819923795,
        "repository": {"full_name": "Kindness-Net/AstraGram-Desktop-macOS-Build"},
        "event": "workflow_dispatch",
        "path": ".github/workflows/build.yml@refs/heads/main",
        "head_branch": "main",
        "run_attempt": 1,
        "status": status,
        "conclusion": conclusion,
    }


def _http_error(code: int, headers: dict[str, str] | None = None) -> HTTPError:
    message = Message()
    for key, value in (headers or {}).items():
        message[key] = value
    return HTTPError(
        url="https://api.github.com/test",
        code=code,
        msg="err",
        hdrs=message,
        fp=io.BytesIO(b'{"message":"err"}'),
    )


class RemoteBuildTests(unittest.TestCase):
    def test_retries_initial_404_until_run_appears(self) -> None:
        clock = _FakeClock()
        api = _FakeApi([
            RemoteRunNotFoundError("404"),
            RemoteRunNotFoundError("404"),
            _run_payload("queued"),
            _run_payload("completed", "success"),
        ])
        details = run_remote_build(
            api,
            repository="Kindness-Net/AstraGram-Desktop-macOS-Build",
            workflow="build.yml",
            ref="main",
            inputs={},
            timeout_seconds=300,
            poll_interval_seconds=30,
            not_found_grace_seconds=120,
            sleep=clock.sleep,
            monotonic=clock.monotonic,
        )
        self.assertEqual(details.conclusion, "success")
        self.assertEqual(api.cancelled, [])
        self.assertEqual(clock.sleeps, [1.0, 2.0, 30.0])

    def test_persistent_404_past_grace_window_fails_and_cancels(self) -> None:
        clock = _FakeClock()
        api = _FakeApi([RemoteRunNotFoundError("404")] * 10)
        with self.assertRaises(RemoteRunNotFoundError):
            run_remote_build(
                api,
                repository="Kindness-Net/AstraGram-Desktop-macOS-Build",
                workflow="build.yml",
                ref="main",
                inputs={},
                timeout_seconds=300,
                poll_interval_seconds=30,
                not_found_grace_seconds=5,
                sleep=clock.sleep,
                monotonic=clock.monotonic,
            )
        self.assertEqual(clock.sleeps, [1.0, 2.0, 2.0])
        self.assertEqual(
            api.cancelled,
            [("Kindness-Net/AstraGram-Desktop-macOS-Build", 45819923795)],
        )

    def test_404_after_run_was_seen_is_not_retried(self) -> None:
        clock = _FakeClock()
        api = _FakeApi([
            _run_payload("in_progress"),
            RemoteRunNotFoundError("404"),
        ])
        with self.assertRaises(RemoteRunNotFoundError):
            run_remote_build(
                api,
                repository="Kindness-Net/AstraGram-Desktop-macOS-Build",
                workflow="build.yml",
                ref="main",
                inputs={},
                timeout_seconds=300,
                poll_interval_seconds=30,
                not_found_grace_seconds=120,
                sleep=clock.sleep,
                monotonic=clock.monotonic,
            )
        self.assertEqual(clock.sleeps, [30.0])

    def test_http_error_classification(self) -> None:
        api = GitHubApi("token")
        with patch("remote_build.urlopen", side_effect=_http_error(404)):
            with self.assertRaises(RemoteRunNotFoundError):
                api._request("GET", "/repos/a/b/actions/runs/1")
        with patch("remote_build.urlopen", side_effect=_http_error(429)):
            with self.assertRaises(TransientGitHubApiError):
                api._request("GET", "/repos/a/b/actions/runs/1")
        with patch(
            "remote_build.urlopen",
            side_effect=_http_error(403, {"X-RateLimit-Remaining": "0"}),
        ):
            with self.assertRaises(TransientGitHubApiError):
                api._request("GET", "/repos/a/b/actions/runs/1")
        with patch("remote_build.urlopen", side_effect=_http_error(403)):
            with self.assertRaises(RemoteBuildError) as ctx:
                api._request("GET", "/repos/a/b/actions/runs/1")
            self.assertNotIsInstance(ctx.exception, TransientGitHubApiError)


if __name__ == "__main__":
    unittest.main()
