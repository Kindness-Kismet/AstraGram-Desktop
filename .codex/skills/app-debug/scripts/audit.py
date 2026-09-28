#!/usr/bin/env python3
"""对照设置访问器、命令注册和构建清单，发现新增功能遗漏的调试入口。"""
import argparse
import json
import re

import cli


def audit():
    source = cli.ROOT / "Telegram" / "SourceFiles"
    directory = source / "ayu" / "debug" / "commands"
    files = list(directory.glob("*.cpp"))
    handlers = "\n".join(path.read_text(encoding="utf-8") for path in files)
    exceptions = {
        "setLegacyEmojiVariants": "旧数据迁移，不增加调试兼容入口",
        "setLegacyRecentEmojiPreload": "旧数据迁移，不增加调试兼容入口",
        "setVideoPlaybackSpeedSerialized": "存储编码，已通过实际播放速度设置覆盖",
        "setHiddenGroupCallTooltip": "提示已展示标记，不是用户设置选项",
    }
    missing = []
    coverage = {}
    for name in ("ayu/ayu_settings.h", "core/core_settings.h", "main/main_session_settings.h"):
        text = (source / name).read_text(encoding="utf-8")
        setters = set(re.findall(r"\bvoid\s+((?:set|update)[A-Z]\w*)\s*\(", text))
        uncovered = sorted(key for key in setters if key not in handlers and key not in exceptions)
        missing.extend(f"{name}: {key}" for key in uncovered)
        coverage[name] = {"accessors": len(setters), "missing": uncovered}

    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command")
    cli.register_commands(sub)
    client = set(sub.choices) - {"app.ensure", "app.restart", "app.stop"}
    server = set(re.findall(r'\{\s*u"([a-z][a-z.-]+)"_q,\s*&', handlers))
    missing.extend(f"命令注册不一致：{name}" for name in sorted(client ^ server))
    cmake = (cli.ROOT / "Telegram" / "CMakeLists.txt").read_text(encoding="utf-8")
    for path in files:
        text = path.read_text(encoding="utf-8")
        if path.relative_to(source).as_posix() not in cmake:
            missing.append(f"未登记构建清单：{path.name}")
        if len(text.splitlines()) > 2000:
            missing.append(f"源码超过行数限制：{path.name}")
        if not text.startswith("#ifdef _DEBUG"):
            missing.append(f"缺少调试构建保护：{path.name}")

    report = {"settings": coverage, "exceptions": exceptions, "commands": len(server), "issues": missing}
    output = cli.ROOT / "build" / "debug-command-audit.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return bool(missing)


if __name__ == "__main__":
    raise SystemExit(audit())
