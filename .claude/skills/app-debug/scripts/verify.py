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
        require(str(error).isascii(), f"错误信息不是英文：{name}")
        return
    raise AssertionError(f"非法输入未被拒绝：{name}")


def wait_until(query, predicate, description):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        value = query()
        if predicate(value):
            return value
        time.sleep(0.1)
    raise AssertionError(f"等待结果超时：{description}")


def verify(profile):
    pid = cli.port_owner_pid()
    require(pid is not None and cli.is_expected_process(pid), "请先启动本仓库的调试应用")
    info = command("app.info")
    expected = (cli.ROOT / "build" / "debug-profiles" / profile).resolve()
    require(Path(info["workingDir"]).resolve() == expected, "当前应用不是指定的独立配置")
    require(info["fakeSession"], "只允许在假会话验证，请先点击进入假会话")
    require(info["isolatedDebug"], "独立配置未启用测试隔离")
    require(all(not a["hasSession"] or a["fake"] for a in command("session.list")), "配置中存在真实账号")
    report = {"profile": profile, "checks": [], "scope": "独立假会话；不执行系统集成与真实账号业务"}

    def passed(label):
        report["checks"].append(label)
        print(f"通过：{label}", flush=True)

    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command")
    cli.register_commands(sub)
    server = set(command("app.help"))
    client = set(sub.choices) - {"app.start", "app.ensure", "app.restart", "app.stop"}
    require(server == client, f"客户端与服务端指令不一致：{sorted(server ^ client)}")
    passed(f"客户端与服务端指令一致，共 {len(server)} 条")

    scenarios = command("scenario.list")
    require(any(s["key"] == "message-menu" for s in scenarios), "缺少消息菜单对照场景")
    require(command("settings.get", "devFeaturesEnabled"), "假会话未自动开启开发者功能")
    for scenario in scenarios:
        command("scenario.open", scenario["key"])
    passed(f"进入假会话后 {len(scenarios)} 种场景均可直接打开")

    schema = command("settings.schema")
    keys = command("settings.keys")
    require(sorted(entry["key"] for entry in schema) == keys, "设置结构与键名不一致")
    for key in keys:
        command("settings.get", key)
    report["settingCount"] = len(keys)
    passed(f"全部 {len(keys)} 个设置可查询")

    original = {}
    filters = []

    def change(key, value):
        if key not in original:
            original[key] = command("settings.get", key)
        actual = command("settings.set", key, encoded(value))
        require(actual == value, f"设置返回值不一致：{key}")
        require(command("settings.get", key) == value, f"设置回读不一致：{key}")

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
        passed("布尔、整数、小数、字符串空格、数组、可空值和嵌套设置读写")

        experimental = command("settings.schema", "experimental.")
        require(experimental, "实验设置未注册")
        candidate = next((entry for entry in experimental
                          if entry["key"].endswith("dialogs-unread-on-top") and entry["writable"]), None)
        if candidate:
            change(candidate["key"], not candidate["value"])
        passed(f"实验设置注册表可查询，共 {len(experimental)} 项")

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
            require(command("settings.get", key) == before, f"非法写入改变了设置：{key}")
        rejected("settings.set", "missing-setting", "true")
        passed("非法类型、枚举和整数越界均拒绝且保持原值")

        change("useGlobalGhostMode", True)
        ghost_before = command("settings.get", "ghost")
        for key in ghost_before:
            if key != "enabled":
                original.setdefault("ghost." + key, ghost_before[key])
        for key in ghost_before:
            if key.endswith("Locked"):
                change("ghost." + key, False)
        command("settings.set", "ghost.enabled", "true")
        require(command("ghost.status")["enabled"], "隐身模式未启用")
        change("deletedMark", "验证")
        command("settings.set", "ghost.enabled", "false")
        require(not command("ghost.status")["enabled"], "修改其他设置后隐身模式订阅失效")
        passed("隐身模式切换和设置订阅保持有效")

        change("autoSpaceSending", True)
        spaced = command("text.process", "send", "中文Test")
        require(spaced["text"] == "中文 Test", "发送文本未自动加空格")
        change("autoSpaceSending", False)
        require(command("text.process", "send", "中文Test")["text"] == "中文Test", "关闭自动空格后仍处理")
        rejected("text.process", "send", "样本", '[{"type":0,"offset":4294967296,"length":1}]')
        passed("文本处理遵循实际开关")

        peer = next(s["peerId"] for s in scenarios if s["key"] == "private")
        command("chat.open", peer)
        change("filtersEnabled", True)
        change("filtersEnabledInChats", True)
        message = command("message.fake", "command-audit-filter", "--peer", peer, "--from", 820000001)
        msg_id = message["msgId"]
        rule = command("filter.put", json.dumps({"text": "command-audit-filter"}))
        filters.append(rule["id"])
        require(command("filter.check", peer, msg_id)["filtered"], "规则未过滤消息")
        command("filter.exclude", rule["id"], peer, "true")
        require(not command("filter.check", peer, msg_id)["filtered"], "排除项未生效")
        command("filter.exclude", rule["id"], peer, "false")
        require(command("filter.check", peer, msg_id)["filtered"], "取消排除未生效")
        command("filter.put", json.dumps({"id": rule["id"], "enabled": False}))
        require(not command("filter.check", peer, msg_id)["filtered"], "停用规则未生效")
        rejected("filter.put", '{"text":"["}')
        passed("过滤规则新增、排除、更新及非法正则校验")

        change("hideFromBlocked", True)
        blocked = command("message.fake", "已屏蔽用户的样本", "--peer", peer,
                          "--from", 820000003, "--blocked")
        require(command("filter.check", peer, blocked["msgId"])["blocked"], "屏蔽用户未被识别")
        change("shadowBanIds", [820000004])
        shadow = command("message.fake", "本地隐藏名单样本", "--peer", peer, "--from", 820000004)
        require(command("filter.check", peer, shadow["msgId"])["blocked"], "本地隐藏名单未更新缓存")
        change("shadowBanIds", [])
        require(not command("filter.check", peer, shadow["msgId"])["blocked"], "移除本地隐藏名单后状态未更新")
        passed("屏蔽用户及本地隐藏名单与过滤逻辑一致")

        change("saveMessagesHistory", True)
        change("saveDeletedMessages", True)
        stored = command("message.fake", "编辑前的样本", "--peer", peer, "--from", 820000002)
        stored_id = stored["msgId"]
        edited = command("message.edit-local", peer, stored_id, "编辑后的样本")
        require(edited["text"] == "编辑后的样本", "本地编辑未更新消息")
        wait_until(lambda: command("storage.edits", peer, stored_id),
                   lambda rows: any(row["text"] == "编辑前的样本" for row in rows), "编辑留档")
        command("message.delete-local", peer, stored_id)
        wait_until(lambda: command("storage.deleted", peer),
                   lambda rows: any(row["messageId"] == stored_id for row in rows), "删除留档")
        passed("本地编辑和删除经过原业务流程并产生留档")

        change("saveMessagesHistory", False)
        change("saveDeletedMessages", False)
        transient = command("message.fake", "不留档的样本", "--peer", peer, "--from", 820000005)["msgId"]
        command("message.edit-local", peer, transient, "不留档的编辑")
        require(not command("storage.edits", peer, transient), "关闭编辑留档后仍保存")
        command("message.delete-local", peer, transient)
        rejected("message.inspect", peer, transient)
        require(not any(row["messageId"] == transient for row in command("storage.deleted", peer)),
                "关闭删除留档后仍保存")
        passed("关闭留档开关后按原逻辑移除消息")

        pages = command("page.list")
        require(pages and len(command("action.list")) >= 60, "官方入口缺失")
        command("chat.open")
        command("page.open", "extras")
        report["pageCount"] = len(pages)
        passed(f"官方快捷动作清单与 {len(pages)} 个设置索引入口")

        require(command("privacy.get")["localOnly"], "隐私状态未标识本地缓存")
        rejected("privacy.set", "hideReadTime", "true")
        rejected("privacy.reload")
        rejected("message.send", peer, "不会发送的样本")
        passed("需要真实登录的命令在假会话拒绝执行")

        emoji = command("emoji.list")
        rejected("emoji.select", "256")
        job = command("emoji.select", emoji["current"])
        result = wait_until(lambda: command("job.status", job["jobId"]),
                            lambda state: state["state"] != "running", "本地表情切换")
        require(result["state"] == "succeeded", "本地异步表情切换失败")
        command("job.forget", job["jobId"])
        passed("本地异步任务查询与清理")
    finally:
        for filter_id in filters:
            command("filter.remove", filter_id)
        for key, value in reversed(list(original.items())):
            command("settings.set", key, encoded(value))

    output = cli.ROOT / "build" / "debug-command-verification.json"
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"验证记录：{output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, help="已启动且已经进入假会话的独立配置名称")
    args = parser.parse_args()
    require(bool(cli.re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,47}", args.profile)), "配置名称不合法")
    verify(args.profile)
