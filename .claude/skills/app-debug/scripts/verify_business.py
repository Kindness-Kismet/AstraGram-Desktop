#!/usr/bin/env python3
"""在独立假会话验证业务指令，不操作窗口或发送真实消息。"""
import argparse
import json
from pathlib import Path
import tempfile
import time

import cli


def command(name, *args):
    payload = cli.send_command(" ".join([name, *(cli.quote_arg(str(arg)) for arg in args)]))
    return json.loads(payload) if payload else None


def rejected(name, *args):
    try:
        command(name, *args)
    except RuntimeError:
        return
    raise AssertionError(f"Command should have been rejected: {name}")


def completed(started):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        status = command("job.status", started["jobId"])
        if status["state"] != "running":
            command("job.forget", started["jobId"])
            return status
        time.sleep(0.1)
    raise AssertionError(f"Task did not finish: {started}")


def succeeded(started):
    result = completed(started)
    assert result["state"] == "succeeded", result
    return result["result"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True)
    args = parser.parse_args()
    cli.PROFILE_OVERRIDE = args.profile
    cli.ensure_debug_app()
    info = command("app.info")
    assert info["fakeSession"] and info["isolatedDebug"], "An isolated fake session is required"
    assert Path(info["workingDir"]).resolve() == cli.working_dir()
    directory = Path(tempfile.mkdtemp(prefix="business-", dir=cli.ROOT / "build"))
    checked = []

    def check(name):
        checked.append(name)
        print(json.dumps({"passed": name}), flush=True)

    fixtures = [
        ("archive.ZIP", "file", "archives"),
        ("archive.rar", "file", "archives"),
        ("archive.7z", "file", "archives"),
        ("track.MP3", "file", "music"),
        ("clip.MP4", "file", "videos"),
        ("note.txt", "file", "other"),
        ("voice.ogg", "voice", "other"),
        ("captions.srt", "file", "other"),
    ]
    records = []
    for suffix, kind, category in fixtures:
        path = directory / (directory.name + "-" + suffix)
        path.write_bytes(("Synthetic metadata fixture: " + suffix).encode())
        record = command("downloads.fake", str(path), kind, "done")
        assert record["type"] == category, record
        records.append(record)
    for category, count in (("all", 8), ("archives", 3), ("music", 1), ("videos", 1), ("other", 3)):
        result = command("downloads.list", category, directory.name)
        assert result["count"] == count, result
    check("download categories, uppercase extensions, voice and subtitle exclusions")
    assert command("downloads.list", "music", "archive")["count"] == 0
    assert command("downloads.list", "music", directory.name + " track")["count"] == 1
    check("search and type intersection")

    path = directory / (directory.name + "-progress.ZIP")
    path.write_bytes(b"Synthetic download progress fixture" * 20)
    item = command("downloads.fake", str(path), "file", "loading")
    peer, message = item["peerId"], item["messageId"]
    command("downloads.progress", peer, message, 12)
    loading = command("downloads.list", "archives", path.stem)["items"]
    assert len(loading) == 1 and loading[0]["state"] == "downloading" and loading[0]["ready"] == 12, loading
    command("downloads.progress", peer, message, "done")
    done = command("downloads.list", "archives", path.stem)["items"]
    assert len(done) == 1 and done[0]["state"] == "completed", done
    check("download progress and completion")

    cancelled = command("downloads.fake", str(path), "file", "loading")
    command("downloads.cancel", cancelled["peerId"], cancelled["messageId"])
    assert all(row["messageId"] != cancelled["messageId"] for row in command("downloads.list")["items"])
    check("download cancellation removes the active record")

    source = records[0]
    destination = directory / (directory.name + "-resaved.zip")
    saved = succeeded(command("downloads.start", source["peerId"], source["messageId"], str(destination)))
    assert destination.read_bytes() == Path(source["path"]).read_bytes()
    assert saved["path"] == destination.as_posix()
    rows = command("downloads.list", "archives", destination.stem)["items"]
    assert len(rows) == 1 and rows[0]["messageId"] == source["messageId"], rows
    assert command("downloads.list", "all", directory.name)["count"] == 9
    rejected("downloads.start", source["peerId"], source["messageId"], str(destination))
    check("asynchronous save, latest-path search, deduplication and overwrite rejection")

    inspected = command("message.inspect", peer, message)
    assert inspected["document"]["size"] == path.stat().st_size and not inspected["serverMessage"]
    user = command("peer.info", info["userId"])
    resolved = completed(command("mention.resolve", info["userId"]))
    assert user["self"]
    assert resolved["state"] == ("succeeded" if user["canMention"] else "failed"), resolved
    if not user["canMention"]:
        assert resolved["error"] == "unknown_user"
    rejected("mention.resolve", "18446744073709551616")
    rejected("message.fetch", "self")
    rejected("message.send-file", "self", str(path))
    rejected("mention.send", "self", info["userId"], "test", " text")
    check("64-bit message lookup, mention eligibility and fake-session send rejection")

    exported = directory / "settings.json"
    succeeded(command("settings.export", str(exported), "custom"))
    document = json.loads(exported.read_text(encoding="utf-8"))
    assert "custom" in document and "official" not in document and "account" not in document
    inspection = succeeded(command("settings.inspect-import", str(exported), "custom"))
    assert inspection["count"] > 0
    restored = succeeded(command("settings.import", str(exported), "custom"))
    assert restored["saved"]
    invalid = directory / "invalid-settings.json"
    invalid.write_text('{"formatVersion":999}', encoding="utf-8")
    assert completed(command("settings.inspect-import", str(invalid), "custom"))["state"] == "failed"
    check("settings export, inspection, import persistence and invalid format")

    report = {"passed": checked, "fixtures": str(directory), "port": cli.PORT, "profile": args.profile,
              "networkTested": False, "windowInteraction": False}
    (directory / "result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
