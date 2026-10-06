#!/usr/bin/env python3
"""在独立假会话验证命令协议、设置和本地业务，不启动应用或切换账号。"""
import argparse
import json
import time
from pathlib import Path

import cli


def command(name, *args):
    payload = cli.send_command(" ".join([name, *(cli.quote_arg(str(arg)) for arg in args)]))
    return json.loads(payload) if payload else None


def encoded(value):
    return value if isinstance(value, str) else json.dumps(value, ensure_ascii=False)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def rejected(name, *args):
    try:
        command(name, *args)
    except RuntimeError as error:
        require(str(error).isascii(), f"Error message is not in English: {name}")
        return
    raise AssertionError(f"Invalid input was not rejected: {name}")


def wait_until(query, predicate, description):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        value = query()
        if predicate(value):
            return value
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for result: {description}")


def verify(profile):
    pid = cli.port_owner_pid()
    require(pid is not None and cli.is_expected_process(pid), "Start the debug application from this repository first")
    info = command("app.info")
    expected = (cli.ROOT / "build" / "debug-profiles" / profile).resolve()
    require(Path(info["workingDir"]).resolve() == expected, "The application is not using the requested isolated profile")
    require(info["fakeSession"], "Verification requires a fake session; enter one first")
    require(info["isolatedDebug"], "Test isolation is not enabled for this profile")
    require(all(not a["hasSession"] or a["fake"] for a in command("session.list")), "The profile contains a real account")
    report = {"profile": profile, "checks": [], "scope": "Isolated fake session; excludes system integration and real-account operations"}

    def passed(label):
        report["checks"].append(label)
        print(f"Passed: {label}", flush=True)

    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command")
    cli.register_commands(sub)
    server = set(command("app.help"))
    client = set(sub.choices) - {"app.start", "app.ensure", "app.restart", "app.stop"}
    require(server == client, f"Client and server command sets differ: {sorted(server ^ client)}")
    passed(f"Client and server command sets match: {len(server)} commands")

    scenarios = command("scenario.list")
    require(any(s["key"] == "message-menu" for s in scenarios), "Message menu comparison scenario is missing")
    require(command("settings.get", "devFeaturesEnabled"), "Fake session did not enable developer features automatically")
    for scenario in scenarios:
        command("scenario.open", scenario["key"])
    passed(f"All {len(scenarios)} scenarios can be opened from the fake session")

    schema = command("settings.schema")
    keys = command("settings.keys")
    require(sorted(entry["key"] for entry in schema) == keys, "Setting schema and keys differ")
    for key in keys:
        command("settings.get", key)
    report["settingCount"] = len(keys)
    passed(f"All {len(keys)} settings can be queried")

    original = {}
    filters = []

    def change(key, value):
        if key not in original:
            original[key] = command("settings.get", key)
        actual = command("settings.set", key, encoded(value))
        require(actual == value, f"Setting returned an unexpected value: {key}")
        require(command("settings.get", key) == value, f"Setting readback mismatch: {key}")

    try:
        for key, value in {
            "deletedMark": "  验证标记  ", "wideMultiplier": 1.25,
            "messageShotSettings.showDate": False,
            "messageShotSettings.embeddedThemeAccentColor": 12345,
            "core.songVolume": 0.35, "core.notificationsCount": 3,
            "core.notifyView": 1, "core.includeMutedCounter": False,
            "core.noWarningExtensions": ["verify"], "core.systemDarkMode": None,
            "session.phoneNumberHidden": True,
            "session.autoDownload.0.0": 1024,
            "session.defaultRingtoneVolume.0": 50,
        }.items():
            change(key, value)
        passed("Read and write booleans, integers, decimals, string whitespace, arrays, nullable and nested settings")

        experimental = command("settings.schema", "experimental.")
        require(experimental, "Experimental settings are not registered")
        candidate = next((entry for entry in experimental
                          if entry["key"].endswith("dialogs-unread-on-top") and entry["writable"]), None)
        if candidate:
            change(candidate["key"], not candidate["value"])
        passed(f"Experimental settings registry can be queried: {len(experimental)} entries")

        for key, value in [
            ("saveDeletedMessages", "1"), ("core.notifyView", "4294967296"),
            ("avatarCorners", "2147483648"),
            ("shadowBanIds", "[9223372036854775808]"),
            ("messageShotSettings.embeddedThemeAccentColor", "4294967296"),
            ("windowMaterial", "4294967296"), ("core.songVolume", "-1"),
            ("core.videoPipGeometry", "%%%"),
            ("proxy.selected", '{"type":99,"host":"127.0.0.1","port":1080,"user":"","password":""}'),
            ("core.videoQuality", '{"manual":true,"height":4294967296,"original":false}'),
        ]:
            before = command("settings.get", key)
            rejected("settings.set", key, value)
            require(command("settings.get", key) == before, f"Invalid write changed a setting: {key}")
        rejected("settings.set", "missing-setting", "true")
        passed("Invalid types, enum values and integer ranges are rejected without changing existing values")

        change("useGlobalGhostMode", True)
        ghost_before = command("settings.get", "ghost")
        for key in ghost_before:
            if key != "enabled":
                original.setdefault("ghost." + key, ghost_before[key])
        for key in ghost_before:
            if key.endswith("Locked"):
                change("ghost." + key, False)
        command("settings.set", "ghost.enabled", "true")
        require(command("ghost.status")["enabled"], "Ghost mode was not enabled")
        change("deletedMark", "验证")
        command("settings.set", "ghost.enabled", "false")
        require(not command("ghost.status")["enabled"], "Ghost mode subscription stopped working after another setting changed")
        passed("Ghost mode toggles and settings subscriptions remain active")

        change("autoSpaceSending", True)
        spaced = command("text.process", "send", "中文Test")
        require(spaced["text"] == "中文 Test", "Outgoing text was not spaced automatically")
        change("autoSpaceSending", False)
        require(command("text.process", "send", "中文Test")["text"] == "中文Test", "Automatic spacing still runs after being disabled")
        rejected("text.process", "send", "样本", '[{"type":0,"offset":4294967296,"length":1}]')
        passed("Text processing respects its settings")

        peer = next(s["peerId"] for s in scenarios if s["key"] == "private")
        command("chat.open", peer)
        change("filtersEnabled", True)
        change("filtersEnabledInChats", True)
        message = command("message.fake", "command-audit-filter", "--peer", peer, "--from", 820000001)
        msg_id = message["msgId"]
        rule = command("filter.put", json.dumps({"text": "command-audit-filter"}))
        filters.append(rule["id"])
        require(command("filter.check", peer, msg_id)["filtered"], "Rule did not filter the message")
        command("filter.exclude", rule["id"], peer, "true")
        require(not command("filter.check", peer, msg_id)["filtered"], "Exclusion did not take effect")
        command("filter.exclude", rule["id"], peer, "false")
        require(command("filter.check", peer, msg_id)["filtered"], "Removing the exclusion did not take effect")
        command("filter.put", json.dumps({"id": rule["id"], "enabled": False}))
        require(not command("filter.check", peer, msg_id)["filtered"], "Disabling the rule did not take effect")
        rejected("filter.put", '{"text":"["}')
        passed("Filter rule creation, exclusions, updates and invalid regex validation")

        change("hideFromBlocked", True)
        blocked = command("message.fake", "已屏蔽用户的样本", "--peer", peer,
                          "--from", 820000003, "--blocked")
        require(command("filter.check", peer, blocked["msgId"])["blocked"], "Blocked user was not recognized")
        change("shadowBanIds", [820000004])
        shadow = command("message.fake", "本地隐藏名单样本", "--peer", peer, "--from", 820000004)
        require(command("filter.check", peer, shadow["msgId"])["blocked"], "Shadow ban list did not update the cache")
        change("shadowBanIds", [])
        require(not command("filter.check", peer, shadow["msgId"])["blocked"], "State did not update after removing the shadow ban")
        passed("Blocked users and shadow bans match filter behavior")

        change("saveMessagesHistory", True)
        change("saveDeletedMessages", True)
        stored = command("message.fake", "编辑前的样本", "--peer", peer, "--from", 820000002)
        stored_id = stored["msgId"]
        edited = command("message.edit-local", peer, stored_id, "编辑后的样本")
        require(edited["text"] == "编辑后的样本", "Local edit did not update the message")
        wait_until(lambda: command("storage.edits", peer, stored_id),
                   lambda rows: any(row["text"] == "编辑前的样本" for row in rows), "edit archive")
        command("message.delete-local", peer, stored_id)
        wait_until(lambda: command("storage.deleted", peer),
                   lambda rows: any(row["messageId"] == stored_id for row in rows), "deleted-message archive")
        passed("Local edits and deletions use the normal application flow and produce archive records")

        change("saveMessagesHistory", False)
        change("saveDeletedMessages", False)
        transient = command("message.fake", "不留档的样本", "--peer", peer, "--from", 820000005)["msgId"]
        command("message.edit-local", peer, transient, "不留档的编辑")
        require(not command("storage.edits", peer, transient), "Edit history was saved after archiving was disabled")
        command("message.delete-local", peer, transient)
        rejected("message.inspect", peer, transient)
        require(not any(row["messageId"] == transient for row in command("storage.deleted", peer)),
                "Deleted message was saved after archiving was disabled")
        passed("Disabling archiving preserves normal message removal behavior")

        pages = command("page.list")
        require(pages and len(command("action.list")) >= 60, "Upstream actions are missing")
        command("chat.open")
        command("page.open", "extras")
        report["pageCount"] = len(pages)
        passed(f"Upstream action list and {len(pages)} indexed settings entries")

        require(command("privacy.get")["localOnly"], "Privacy state does not identify the local cache")
        rejected("privacy.set", "hideReadTime", "true")
        rejected("privacy.reload")
        rejected("message.send", peer, "不会发送的样本")
        passed("Commands requiring a real login are rejected in fake sessions")

        emoji = command("emoji.list")
        rejected("emoji.select", "256")
        job = command("emoji.select", emoji["current"])
        result = wait_until(lambda: command("job.status", job["jobId"]),
                            lambda state: state["state"] != "running", "local emoji switch")
        require(result["state"] == "succeeded", "Local asynchronous emoji switch failed")
        command("job.forget", job["jobId"])
        passed("Local asynchronous job queries and cleanup")
    finally:
        for filter_id in filters:
            command("filter.remove", filter_id)
        for key, value in reversed(list(original.items())):
            command("settings.set", key, encoded(value))

    output = cli.ROOT / "build" / "debug-command-verification.json"
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Verification report: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, help="已启动且已经进入假会话的独立配置名称")
    args = parser.parse_args()
    require(bool(cli.re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,47}", args.profile)), "Invalid profile name")
    verify(args.profile)
