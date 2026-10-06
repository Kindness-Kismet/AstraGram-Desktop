#!/usr/bin/env python3
"""对照设置访问器、命令注册和构建清单，发现新增功能遗漏的调试入口。"""
import argparse
import json
import re

import cli


def audit():
    source = cli.ROOT / "Telegram" / "SourceFiles"
    directory = source / "extras" / "debug" / "commands"
    files = list(directory.glob("*.cpp"))
    handlers = "\n".join(path.read_text(encoding="utf-8") for path in files)
    exceptions = {
        "setLegacyEmojiVariants": "Legacy data migration; no debug compatibility entry is needed",
        "setLegacyRecentEmojiPreload": "Legacy data migration; no debug compatibility entry is needed",
        "setVideoPlaybackSpeedSerialized": "Storage encoding; covered by the playback speed setting",
        "setHiddenGroupCallTooltip": "Tooltip display marker; not a user setting",
    }
    missing = []
    coverage = {}
    for name in ("extras/extras_settings.h", "core/core_settings.h", "main/main_session_settings.h"):
        text = (source / name).read_text(encoding="utf-8")
        setters = set(re.findall(r"\bvoid\s+((?:set|update)[A-Z]\w*)\s*\(", text))
        uncovered = sorted(key for key in setters if key not in handlers and key not in exceptions)
        missing.extend(f"{name}: {key}" for key in uncovered)
        coverage[name] = {"accessors": len(setters), "missing": uncovered}

    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command")
    cli.register_commands(sub)
    client = set(sub.choices) - {"app.start", "app.ensure", "app.restart", "app.stop"}
    server = set(re.findall(r'\{\s*u"([a-z][a-z.-]+)"_q,\s*&', handlers))
    missing.extend(f"Command registration mismatch: {name}" for name in sorted(client ^ server))
    cmake = (cli.ROOT / "Telegram" / "CMakeLists.txt").read_text(encoding="utf-8")
    for path in files:
        text = path.read_text(encoding="utf-8")
        if path.relative_to(source).as_posix() not in cmake:
            missing.append(f"Missing build manifest entry: {path.name}")
        if len(text.splitlines()) > 2000:
            missing.append(f"Source file exceeds the line limit: {path.name}")
        if not text.startswith("#ifdef _DEBUG"):
            missing.append(f"Missing debug build guard: {path.name}")

    report = {"settings": coverage, "exceptions": exceptions, "commands": len(server), "issues": missing}
    output = cli.ROOT / "build" / "debug-command-audit.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return bool(missing)


if __name__ == "__main__":
    raise SystemExit(audit())
