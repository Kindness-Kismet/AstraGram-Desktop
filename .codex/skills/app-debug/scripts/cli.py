#!/usr/bin/env python3
"""app-debug CLI，用 domain.action 指令控制正在运行的 AstraGram Debug 构建。"""
import argparse
import base64
import json
import os
import re
import shlex
import signal
import socket
import subprocess
import sys
import time
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path

sys.dont_write_bytecode = True
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"

# 应用回传的 payload 含中文和表情，cp936 控制台会抛 UnicodeEncodeError。
for _stream in (sys.stdout, sys.stderr):
    _stream.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(os.environ.get("AYUGRAM_DEBUG_ROOT", Path(__file__).resolve().parents[4])).resolve()

# 产物名以构建脚本为准，改名时只需要改 scripts/build_support/paths.py。
sys.path.insert(0, str(ROOT / "scripts"))
from build_support.paths import APP_NAME

PORT = 20100
COMMAND_TIMEOUT_SECONDS = 180
SCREENSHOT_DIR = ROOT / "build" / "screenshots"
COMMAND_LOCK_PATH = ROOT / "build" / "app-debug-cli.lock"
COMMAND_SEPARATOR = "+"
BOOLEAN_CHOICES = ("true", "false")

VERSION_FILE = ROOT / "Telegram" / "build" / "version"
PROFILE_FILE = ROOT / "build" / "app-debug-profile.json"
PROFILE_OVERRIDE = None

GENERIC_COMMANDS = {
    "session.thread-settings": "查询或修改话题与子会话配置：<会话编号> <话题编号> <子会话编号> [键 值]",
    "control.mouse": "在应用控件内部点击、打开菜单或分开按下松开：<目标> <横坐标> <纵坐标> [left|right|double|press|release]",
    "crash.log": "读取当前调试配置的崩溃日志",
    "session.list": "列出本地账号与当前激活账号",
    "session.activate": "切换本地账号：<索引>",
    "session.peer-settings": "查询或修改会话级设置：<会话编号> [键 值]",
    "privacy.get": "查询已加载的服务端隐私配置",
    "privacy.set": "提交服务端隐私配置：<键> <值>，需要真实登录",
    "privacy.reload": "重新请求服务端隐私配置，需要真实登录",
    "settings.schema": "列出设置值、类型与完整键名：[前缀]",
    "page.list": "查询全部官方与定制设置入口：[关键词]",
    "action.list": "列出官方快捷动作",
    "action.run": "按绑定按键触发官方快捷动作，应用窗口须在前台：<名称>",
    "control.get": "查询控件当前值、状态与动作：<目标>",
    "control.set": "修改控件值并触发业务回调：<目标> <值>",
    "control.action": "执行控件公开的动作：<目标> <动作>",
    "filter.list": "列出全部过滤规则",
    "filter.put": "添加或更新过滤规则：<规则 JSON>",
    "filter.remove": "移除指定过滤规则：<编号>",
    "filter.exclusions": "列出全局规则的会话排除项",
    "filter.exclude": "修改全局规则排除项：<规则编号> <会话编号> <true|false>",
    "filter.check": "检查消息实际过滤结果：<会话编号> <消息编号>",
    "filter.visible": "查询或修改过滤消息显示状态：<会话编号> [true|false]",
    "storage.deleted": "查询本地已删除消息：<会话编号> [条数] [关键词]",
    "storage.edits": "查询本地编辑历史：<会话编号> <消息编号> [条数]",
    "message.inspect": "查询消息正文与删除、过滤、视图状态：<会话编号> <消息编号>",
    "message.edit-local": "在假会话触发原生编辑流程：<会话编号> <消息编号> <文字>",
    "message.delete-local": "在假会话触发原生删除流程：<会话编号> <消息编号>",
    "message.hide": "按右键菜单流程隐藏消息及所在相册：<会话编号> <消息编号>",
    "text.process": "验证文本处理与实体偏移：<send|edit|receive|auto-space|zalgo> <文字> [实体 JSON]",
    "translate.start": "按当前翻译服务发起请求：<语言> <文字>",
    "translate.clear-cache": "清空应用翻译缓存",
    "emoji.list": "查询当前表情包、已安装包与预设",
    "emoji.import": "导入字体表情包：<字体路径>",
    "emoji.install": "下载并安装预设表情包：<预设编号>",
    "emoji.select": "切换已安装表情包：<编号>",
    "emoji.cancel": "取消预设表情包下载：<预设编号>",
    "feature.status": "查询窗口材质、隐私遮挡、翻译与表情包状态",
    "forward.status": "查询转发任务进度：<会话编号>",
    "forward.cancel": "取消正在运行的转发任务：<会话编号>",
    "job.status": "查询异步任务结果：<任务编号>",
    "job.forget": "移除已结束任务的查询记录：<任务编号>",
    "app.quit": "正常退出调试应用",
}


def working_dir() -> Path:
    profile = PROFILE_OVERRIDE
    if profile is None:
        profile = json.loads(PROFILE_FILE.read_text(encoding="utf-8"))["profile"] if PROFILE_FILE.exists() else ""
    if not profile:
        return debug_dir()
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,47}", profile):
        raise ValueError("调试配置名须为 1 至 48 个小写字母、数字、下划线或连字符")
    return ROOT / "build" / "debug-profiles" / profile


def read_app_version() -> str:
    for line in VERSION_FILE.read_text(encoding="utf-8").splitlines():
        match = re.search(r"AppVersionOriginal\s+(\S+)", line)
        if match:
            return match.group(1)
    raise SystemExit("AppVersionOriginal not found in Telegram/build/version")


def current_platform() -> str:
    if sys.platform == "win32":
        return "win-x64"
    return "mac" if sys.platform == "darwin" else "linux"


def debug_dir() -> Path:
    # 产物目录带版本号，与 scripts/build.py 的 output_dir 保持一致
    return ROOT / "build" / f"{APP_NAME}-v{read_app_version()}-{current_platform()}-dev"


def app_exe() -> Path:
    return debug_dir() / (f"{APP_NAME}.exe" if sys.platform == "win32" else APP_NAME)


def is_debug_app_exe(path: Path) -> bool:
    # 前后缀匹配而非精确路径：版本升级后旧目录里的进程同样能被识别
    suffix = f"-{current_platform()}-dev"
    resolved = path.resolve()
    directory = resolved.parent.name
    return (
        resolved.name == (f"{APP_NAME}.exe" if sys.platform == "win32" else APP_NAME)
        and resolved.parent.parent.resolve() == (ROOT / "build").resolve()
        and directory.startswith(f"{APP_NAME}-v")
        and directory.endswith(suffix)
        and len(directory) > len(f"{APP_NAME}-v") + len(suffix)
    )

# 启动到监听之间要过 Qt 初始化、账号加载和主窗口构造，冷启动比较慢。
PORT_WAIT_SECONDS = 60

COMMAND_CATEGORY_LABELS = {
    "action": "官方快捷动作",
    "crash": "崩溃信息",
    "emoji": "字体表情包",
    "feature": "定制功能状态",
    "filter": "消息过滤规则",
    "forward": "转发任务",
    "job": "异步任务",
    "privacy": "服务端隐私设置",
    "text": "文本处理",
    "translate": "文本翻译",
    "app": "应用生命周期",
    "session": "会话与测试环境",
    "chat": "会话导航与消息统计",
    "message": "消息样本与发送",
    "notification": "通知触发",
    "window": "窗口尺寸与状态",
    "theme": "主题与聊天背景",
    "page": "页面导航",
    "settings": "设置读写",
    "ghost": "幽灵模式",
    "storage": "已删除消息与编辑历史",
    "screenshot": "截图",
    "control": "控件树与合成交互",
    "scenario": "固定假会话场景",
}


def print_command_help(subparsers) -> None:
    # argparse 只在子命令 action 上保留注册时的帮助文本。
    categories = {}
    for action in subparsers._choices_actions:
        category = action.dest.partition(".")[0]
        categories.setdefault(category, []).append((action.dest, action.help))

    print("用法：app-debug <命令> [参数]")
    print("查看某个命令的参数：app-debug <命令> --help")
    print("多条指令用裸 + 分隔，按顺序执行，任一条失败即中止。")
    print("\n可用命令（按功能分组）：")
    for category in sorted(categories):
        label = COMMAND_CATEGORY_LABELS.get(category, category)
        print(f"\n{label}（{category}）")
        for command, description in sorted(categories[category]):
            print(f"  {command:<44} {description}")


def register_commands(sub) -> None:
    for name, description in GENERIC_COMMANDS.items():
        command = sub.add_parser(name, help=description, description=description)
        command.add_argument("params", nargs=argparse.REMAINDER)
    command = sub.add_parser("app.start", help="启动 Debug 应用：未运行时拉起并等到端口就绪，已在运行则保持不变")
    command.add_argument("--profile", help="选择并记住独立调试配置，default 使用原默认目录")
    command = sub.add_parser("app.ensure", help="app.start 的等价写法")
    command.add_argument("--profile", help="选择并记住独立调试配置，default 使用原默认目录")
    command = sub.add_parser("app.restart", help="先停止再启动 Debug 应用，重新编译后用它启动新产物")
    command.add_argument("--profile", help="选择并记住独立调试配置，default 使用原默认目录")
    sub.add_parser("app.stop", help="停止 Debug 应用，只认端口 PID 或本仓库 dev 产物路径")
    sub.add_parser("app.ping", help="探活，返回 pong")
    sub.add_parser("app.info", help="查询版本、配置目录、会话和窗口状态")
    sub.add_parser("app.check-update", help="触发一次更新检查，结果看 tupdates 目录与日志")
    sub.add_parser("app.update-info", help="查询更新源前缀：文件内容与内存里解析出的地址")
    sub.add_parser("app.help", help="列出服务端已注册的全部指令名")
    command = sub.add_parser("player.state", help="查询当前媒体的播放状态")
    command.add_argument("kind", nargs="?", choices=("song", "voice"))
    command = sub.add_parser("player.fake", help="在假会话中播放本地音乐、语音或圆形视频")
    command.add_argument("kind", choices=("song", "voice", "video"))
    command.add_argument("path")
    command.add_argument("seconds", type=int)

    command = sub.add_parser("settings.keys", help="列出全部设置键名")
    command.add_argument("prefix", nargs="?")
    command = sub.add_parser("settings.dump", help="导出全部设置为 JSON")
    command.add_argument("prefix", nargs="?")
    command = sub.add_parser("settings.get", help="查询单个设置值")
    command.add_argument("key")
    command = sub.add_parser("settings.set", help="修改设置，按当前类型解析取值")
    command.add_argument("key")
    command.add_argument("value")
    command = sub.add_parser("page.open", help="打开设置页：settings / extras / search")
    command.add_argument("section")
    sub.add_parser("theme.reset-background", help="重置聊天背景到默认壁纸")
    command = sub.add_parser("theme.set", help="切换浅色或暗色主题")
    command.add_argument("state", choices=["dark", "light"])

    command = sub.add_parser("session.fake", help="造本地假会话绕过登录，直接进主界面")
    command.add_argument("userId", nargs="?")
    sub.add_parser("session.test-mode", help="在生产环境与官方测试数据中心之间切换")

    sub.add_parser("scenario.list", help="列出固定场景的名称、键名与会话编号")
    command = sub.add_parser("scenario.open", help="打开假会话自动生成的固定场景")
    command.add_argument("key")
    command.add_argument("--view", choices=("main", "alternate", "scheduled", "shortcuts", "pinned", "actions"), default="main", help="主聊天、另一套聊天、计划消息、快捷回复、置顶消息列表或最近操作")

    command.add_argument("--input", choices=("keep", "empty", "reply", "edit"), default="keep", help="保留、清空、回复或编辑输入状态，仅用于普通私聊和话题")

    command = sub.add_parser("message.fake", help="往假会话插入本地文本消息，默认收藏夹，验证渲染与隐藏逻辑")
    command.add_argument("text", help="消息文本")
    command.add_argument("--from", dest="from_user", metavar="USER_ID", help="指定另一个假用户作为发送者")
    command.add_argument("--blocked", action="store_true", help="把发送者标记为已拉黑（真拉黑）")
    command.add_argument("--shadow-ban", action="store_true", help="把发送者加入 AstraGram 影子拉黑名单")
    command.add_argument("--peer", help="插入到指定假会话，默认收藏夹")
    command.add_argument("--sticker", metavar="IMAGE_PATH", help="用本地图片生成静态贴纸，各边不超过 512 像素")
    command = sub.add_parser("notification.test", help="让假用户发一条消息，触发系统或应用自带通知；原生通知需默认配置")
    command.add_argument("text", nargs="?", help="消息文本，默认 Debug 通知测试")
    command.add_argument("--peer", metavar="USER_ID", help="发送通知的假用户编号，默认 830000001")
    command = sub.add_parser("notification.hover", help="给自绘通知窗口发合成的进入／离开事件，模拟鼠标悬停")
    command.add_argument("state", choices=["on", "off"], help="on 进入，off 离开")
    command = sub.add_parser("notification.click", help="点击自绘通知中可见的按钮：reply 展开回复输入框，close 关闭")
    command.add_argument("button", choices=["reply", "close"], help="要点击的按钮")
    command = sub.add_parser("chat.open", help="打开指定对话并清空导航栈；参数取 chat.list 的 peerId，缺省 Saved Messages")
    command.add_argument("peerId", nargs="?")
    command = sub.add_parser("chat.open-archive", help="打开归档文件夹")
    command = sub.add_parser("chat.list", help="列出已加载对话的 peerId 与名称，filter 为名称子串")
    command.add_argument("filter", nargs="?")
    command = sub.add_parser("message.send", help="真实发送文本消息到指定对话，需已登录，仅限本人测试群")
    command.add_argument("peerId")
    command.add_argument("text", nargs="?", help="消息文本，多个参数以空格拼接")
    command.add_argument("extraText", nargs="*", help=argparse.SUPPRESS)
    command.add_argument("--file", dest="text_file", help="发送 UTF-8 文件内容，保留换行与引号")
    command = sub.add_parser("chat.history-stats", help="查询指定消息的存在、隐藏与视图状态，默认收藏夹")
    command.add_argument("msgIds", nargs="+", metavar="MSG_ID")
    command.add_argument("--peer", help="查询指定对话，默认收藏夹")
    command = sub.add_parser("window.resize", help="读或设窗口尺寸（Qt 逻辑像素）；最大化的窗口先还原再设尺寸")
    command.add_argument("size", nargs="*", type=int, metavar="WIDTH HEIGHT", help="省略则只报告当前尺寸，给出时须成对")
    command = sub.add_parser("window.maximize", help="最大化或还原窗口")
    command.add_argument("maximized", choices=["true", "false"], help="true 最大化，false 还原")

    sub.add_parser("ghost.status", help="读全局与当前账号的幽灵模式状态")

    sub.add_parser("storage.stats", help="读保存开关与数据库文件大小")

    command = sub.add_parser("screenshot.take", help="截图当前活动窗口到 build/screenshots/")
    mode = command.add_mutually_exclusive_group()
    mode.add_argument("--popup", action="store_true", help="改为截取活动浮动菜单")
    mode.add_argument("--notification", action="store_true", help="改为截取应用自绘通知，合成到底色上保存为 PNG")
    command.add_argument("--bg", default="#5b6b7f", help="--notification 的底色，格式 #RRGGBB")

    command = sub.add_parser("control.list", help="列出活动窗口的控件树：标识、类名、几何、可见性")
    command.add_argument("filter", nargs="?", help="子串过滤，匹配 objectName/类名/accessibleName/文本")
    command.add_argument("--all", action="store_true", help="扫全部顶层窗口（含菜单、弹层）")
    command = sub.add_parser("control.click", help="点击控件：按钮直接执行控件动作，其余控件投递鼠标事件；按 objectName/accessibleName 或 #序号 寻址")
    command.add_argument("target", help="objectName（如 mainMenuButton）、accessibleName 或 #序号")
    command.add_argument("--all", action="store_true", help="#序号 按 control.list --all 的全顶层序号寻址")
    command.add_argument("--mouse", action="store_true", help="从窗口命中测试后投递鼠标事件，检查按钮是否被遮挡")
    command = sub.add_parser("control.scroll", help="读取或设置可见滚动区的位置")
    command.add_argument("target", help="滚动区名称或 control.list 默认模式的 #序号")
    command.add_argument("top", nargs="?", type=int, help="滚动位置，不传时只读取")
    command = sub.add_parser("control.hover", help="设置按钮悬停绘制状态，不触发点击")
    command.add_argument("target", help="按钮名称或 control.list 默认模式的 #序号")
    command.add_argument("state", choices=("on", "off"), help="开启或关闭悬停状态")
    command = sub.add_parser("control.key", help="向控件合成按键，用于菜单导航和关闭")
    command.add_argument("target", help="控件名称、#序号或 @menu")
    command.add_argument("key", choices=("escape", "up", "down", "left", "right", "enter", "tab"))
    command = sub.add_parser("control.pointer", help="向控件内部位置合成移动事件，不移动系统鼠标；省略坐标时离开")
    command.add_argument("target", help="控件名称或 control.list 默认模式的 #序号")
    command.add_argument("point", nargs="*", type=int, metavar="X Y", help="控件内的坐标，必须成对；省略则清除上一次合成悬停")
    command = sub.add_parser("control.input", help="修改可见输入框的文字，验证输入布局，不触发发送")
    command.add_argument("target", help="输入框的 objectName，如 messageInput")
    command.add_argument("text", nargs="?", help="待输入文字，空字符串用于清空")
    command.add_argument("--file", dest="text_file", help="按 UTF-8 读取文字，保留换行和引号")


def main() -> int:
    parser = argparse.ArgumentParser(prog="app-debug", add_help=False)
    parser.add_argument("-h", "--help", action="store_true", dest="show_help")
    sub = parser.add_subparsers(dest="command")
    register_commands(sub)

    argv = sys.argv[1:]
    if not argv or argv[0] in ("-h", "--help"):
        print_command_help(sub)
        return 0

    # 先整体解析所有分段，拼写错误不会让前几条已经生效。
    try:
        segments = [parser.parse_args(segment) for segment in split_command_segments(argv)]
    except SystemExit:
        return 2

    for args in segments:
        try:
            execute_command(args)
        except Exception as exception:
            print(f"错误: {exception}", file=sys.stderr)
            return 1
    return 0


def split_command_segments(argv: list[str]) -> list[list[str]]:
    segments: list[list[str]] = [[]]
    for token in argv:
        if token == COMMAND_SEPARATOR:
            segments.append([])
        else:
            segments[-1].append(token)
    if any(not segment for segment in segments):
        raise SystemExit(f"{COMMAND_SEPARATOR} 两侧都必须有指令。")
    return segments


def execute_command(args: argparse.Namespace) -> None:
    global PROFILE_OVERRIDE
    command = args.command
    profile = getattr(args, "profile", None)
    if profile is not None:
        PROFILE_OVERRIDE = "" if profile == "default" else profile
        working_dir()

    # 生命周期指令由 CLI 自己完成，不进服务端。
    if command in ("app.start", "app.ensure"):
        ensure_debug_app()
        if profile is not None:
            PROFILE_FILE.write_text(json.dumps({"profile": PROFILE_OVERRIDE}), encoding="utf-8")
        print("应用已就绪。")
        return
    if command == "app.restart":
        restart_debug_app()
        if profile is not None:
            PROFILE_FILE.write_text(json.dumps({"profile": PROFILE_OVERRIDE}), encoding="utf-8")
        print("应用已重启。")
        return
    if command == "app.stop":
        stop_debug_app_if_running()
        print("应用已停止。")
        return

    if command == "screenshot.take":
        ensure_debug_app()
        SCREENSHOT_DIR.mkdir(parents=True, exist_ok=True)
        extension = "png" if args.notification else "jpg"
        target = SCREENSHOT_DIR / datetime.now().strftime(f"shot-%Y%m%d-%H%M%S-%f.{extension}")
        suffix = ""
        if args.popup:
            suffix = " popup"
        elif args.notification:
            suffix = f" notification {quote_arg(args.bg)}"
        print(send_command(f"screenshot.take {quote_arg(str(target))}{suffix}"))
        return

    ensure_debug_app()
    payload = send_command(build_server_command(args))
    if payload:
        print(payload)


def build_server_command(args: argparse.Namespace) -> str:
    command = args.command
    if command in GENERIC_COMMANDS:
        return " ".join([command, *map(quote_arg, args.params)])
    if command in ("settings.keys", "settings.dump"):
        return command + (" " + quote_arg(args.prefix) if args.prefix is not None else "")
    if command == "player.state":
        return command + (" " + args.kind if args.kind else "")
    if command == "player.fake":
        return f"{command} {args.kind} {quote_arg(str(Path(args.path).resolve()))} {args.seconds}"
    if command == "scenario.open":
        return f"scenario.open {quote_arg(args.key)} --view {args.view} --input {args.input}"
    if command == "control.hover":
        return f"control.hover {quote_arg(args.target)} {args.state}"
    if command == "control.key":
        return f"control.key {quote_arg(args.target)} {args.key}"
    if command == "control.pointer":
        if len(args.point) not in (0, 2):
            raise ValueError("control.pointer 的 x 与 y 必须成对给出")
        return " ".join([command, quote_arg(args.target), *map(str, args.point)])
    if command == "control.scroll":
        return f"control.scroll {quote_arg(args.target)}" + (f" {args.top}" if args.top is not None else "")
    if command == "control.input":
        if (args.text is None) == (args.text_file is None):
            raise SystemExit("文字和 --file 必须且只能提供一项")
        value = Path(args.text_file).read_text(encoding="utf-8") if args.text_file is not None else args.text
        encoded = base64.b64encode(value.encode("utf-8")).decode("ascii")
        return f"control.input {quote_arg(args.target)} b64:{encoded}"
    if command == "settings.get":
        return f"settings.get {quote_arg(args.key)}"
    if command == "settings.set":
        return f"settings.set {quote_arg(args.key)} {quote_arg(args.value)}"
    if command == "page.open":
        return f"page.open {quote_arg(args.section)}"
    if command == "theme.reset-background":
        return "theme.reset-background"
    if command == "theme.set":
        return f"theme.set {quote_arg(args.state)}"
    if command == "control.list":
        parts = ["control.list"]
        if args.filter:
            parts.append(quote_arg(args.filter))
        if args.all:
            parts.append("--all")
        return " ".join(parts)
    if command == "control.click":
        parts = ["control.click", quote_arg(args.target)]
        if args.all:
            parts.append("--all")
        if args.mouse:
            parts.append("--mouse")
        return " ".join(parts)
    if command == "session.fake":
        return ("session.fake" if args.userId is None
                else f"session.fake {quote_arg(args.userId)}")
    if command == "message.fake":
        parts = ["message.fake", quote_arg(args.text)]
        if args.from_user:
            parts.extend(["--from", args.from_user])
        if args.blocked:
            parts.append("--blocked")
        if args.shadow_ban:
            parts.append("--shadow-ban")
        if args.peer:
            parts.extend(["--peer", args.peer])
        if args.sticker:
            parts.extend(["--sticker", quote_arg(str(Path(args.sticker).resolve()))])
        return " ".join(parts)
    if command == "notification.test":
        parts = ["notification.test"]
        if args.text:
            parts.append(quote_arg(args.text))
        if args.peer:
            parts.extend(["--peer", args.peer])
        return " ".join(parts)
    if command == "notification.hover":
        return f"notification.hover {args.state}"
    if command == "notification.click":
        return f"notification.click {args.button}"
    if command == "chat.open":
        return ("chat.open" if args.peerId is None
                else f"chat.open {quote_arg(args.peerId)}")
    if command == "chat.open-archive":
        return "chat.open-archive"
    if command == "chat.list":
        return ("chat.list" if args.filter is None
                else f"chat.list {quote_arg(args.filter)}")
    if command == "message.send":
        if args.text_file is not None:
            if args.text is not None or args.extraText:
                raise ValueError("文字和 --file 只能提供一项")
            path = Path(args.text_file).resolve()
            return f"message.send {args.peerId} --file {quote_arg(str(path))}"
        if args.text is None:
            raise ValueError("请提供消息文字或 --file")
        text = " ".join([args.text, *args.extraText])
        return f"message.send {args.peerId} {quote_arg(text)}"
    if command == "chat.history-stats":
        result = "chat.history-stats " + " ".join(args.msgIds)
        return result + (f" --peer {quote_arg(args.peer)}" if args.peer else "")
    if command == "window.resize":
        if not args.size:
            return "window.resize"
        if len(args.size) != 2:
            raise ValueError("window.resize 的 width 与 height 必须成对给出")
        return f"window.resize {args.size[0]} {args.size[1]}"
    if command == "window.maximize":
        return f"window.maximize {args.maximized}"
    # 其余都是无参指令，名字与服务端一一对应。
    return command


def quote_arg(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def ensure_debug_app() -> None:
    if (pid := port_owner_pid()) is not None:
        if not is_expected_process(pid):
            raise RuntimeError(f"端口 {PORT} 属于其它应用，PID 为 {pid}")
        info = json.loads(send_command("app.info"))
        actual = Path(info["workingDir"]).resolve()
        if actual != working_dir().resolve():
            raise RuntimeError(f"当前数据目录为 {actual}，目标为 {working_dir()}。请先用 app.stop 退出当前调试应用。")
        if actual != debug_dir().resolve() and not info.get("isolatedDebug"):
            raise RuntimeError("独立配置未启用测试隔离，请重新构建并重启调试应用。")
        return
    if not app_exe().is_file():
        raise SystemExit(
            f"Debug 产物不存在：{app_exe()}\n"
            f"先运行 python scripts/build.py --dev"
        )
    launch_app()
    wait_for_port()
    ensure_debug_app()


def launch_app() -> None:
    directory = working_dir()
    directory.mkdir(parents=True, exist_ok=True)
    command = [str(app_exe()), "-workdir", str(directory)]
    if directory.resolve() != debug_dir().resolve():
        command.append("-testagent")
    if sys.platform == "win32":
        subprocess.Popen(command, cwd=directory, creationflags=subprocess.DETACHED_PROCESS)
    else:
        subprocess.Popen(command, cwd=directory, start_new_session=True)
    print(f"已启动 {app_exe().name}，等待调试端口就绪...", flush=True)


def restart_debug_app() -> None:
    stop_debug_app_if_running()
    launch_app()
    wait_for_port()
    ensure_debug_app()


def stop_debug_app_if_running() -> None:
    pid = port_owner_pid()
    if pid is not None:
        if not is_expected_process(pid):
            raise RuntimeError(
                f"端口 {PORT} 被意外 PID {pid} 占用，拒绝停止。\n"
                f"手动核对路径：wmic process where \"ProcessId={pid}\" get ExecutablePath"
            )
        try:
            send_command("app.quit")
            wait_for_port_closed()
            wait_for_process_exit(pid)
        except Exception:
            if process_exists(pid):
                kill_pid(pid)
                wait_for_port_closed()
                wait_for_process_exit(pid)

    # 端口尚未建立时，仅按当前 Debug 可执行文件绝对路径清理启动中的进程。
    for remaining_pid in debug_app_pids():
        kill_pid(remaining_pid)
        wait_for_process_exit(remaining_pid)


def debug_app_pids() -> list[int]:
    expected_path = app_exe().resolve()
    if sys.platform == "win32":
        escaped_path = str(expected_path).replace("'", "''")
        command = [
            "powershell",
            "-NoProfile",
            "-Command",
            f"$expected = '{escaped_path}'; "
            "Get-CimInstance Win32_Process | "
            "Where-Object { $_.ExecutablePath -eq $expected } | "
            "ForEach-Object { $_.ProcessId }",
        ]
        result = subprocess.run(command, cwd=ROOT, text=True, errors="replace", capture_output=True, check=False)
        return [int(line.strip()) for line in result.stdout.splitlines() if line.strip().isdigit()]

    proc_root = Path("/proc")
    if proc_root.is_dir():
        pids = []
        for entry in proc_root.iterdir():
            if not entry.name.isdigit():
                continue
            try:
                process_path = Path(os.readlink(entry / "exe")).resolve()
            except OSError:
                continue
            if process_path == expected_path:
                pids.append(int(entry.name))
        return pids

    result = subprocess.run(
        ["ps", "-axo", "pid=,command="],
        cwd=ROOT,
        text=True,
        errors="replace",
        capture_output=True,
        check=False,
    )
    pids = []
    for line in result.stdout.splitlines():
        parts = line.strip().split(maxsplit=1)
        if len(parts) != 2 or not parts[0].isdigit():
            continue
        try:
            executable = shlex.split(parts[1])[0]
        except (ValueError, IndexError):
            continue
        process_path = Path(executable)
        if not process_path.is_absolute():
            process_path = debug_dir() / process_path
        if process_path.resolve() == expected_path:
            pids.append(int(parts[0]))
    return pids


def port_owner_pid() -> int | None:
    if sys.platform == "win32":
        # netstat 输出窄且不需要 admin，只要解析 127.0.0.1:PORT 那一行的 PID。
        result = subprocess.run(
            ["netstat", "-ano", "-p", "TCP"],
            text=True,
            errors="replace",
            capture_output=True,
            check=False,
        )
        for line in result.stdout.splitlines():
            if f"127.0.0.1:{PORT}" in line and "LISTENING" in line:
                parts = line.split()
                if len(parts) >= 5 and parts[-1].isdigit():
                    return int(parts[-1])
        return None

    # POSIX: lsof / ss / netstat，按可用性顺序试探。
    for cmd in (
        ["lsof", "-iTCP", "-sTCP:LISTEN", "-n", "-P"],
        ["ss", "-tlnp"],
        ["netstat", "-tlnp"],
    ):
        result = subprocess.run(cmd, text=True, errors="replace", capture_output=True, check=False)
        if result.returncode != 0:
            continue
        for line in result.stdout.splitlines():
            if f":{PORT}" not in line or "LISTEN" not in line:
                continue
            # lsof: COMMAND PID USER FD TYPE ... NAME
            # ss/netstat: ... 127.0.0.1:PORT ... users:(("name",pid=<pid>,...))
            parts = line.split()
            if len(parts) > 1 and parts[1].isdigit():
                return int(parts[1])
            match = __import__("re").search(r"pid=(\d+)", line)
            if match:
                return int(match.group(1))
    return None


def is_expected_process(pid: int) -> bool:
    if sys.platform == "win32":
        result = subprocess.run(
            ["powershell", "-NoProfile", "-Command", f"(Get-Process -Id {pid} -ErrorAction Ignore).Path"],
            text=True,
            errors="replace",
            capture_output=True,
            check=False,
        )
        path = result.stdout.strip()
        return bool(path) and is_debug_app_exe(Path(path))

    proc_root = Path("/proc")
    if proc_root.is_dir():
        try:
            process_path = Path(os.readlink(proc_root / str(pid) / "exe")).resolve()
        except OSError:
            return False
        return is_debug_app_exe(process_path)

    result = subprocess.run(
        ["ps", "-p", str(pid), "-o", "command="],
        text=True,
        errors="replace",
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        return False
    try:
        executable = shlex.split(result.stdout.strip())[0]
    except (ValueError, IndexError):
        return False
    process_path = Path(executable)
    if not process_path.is_absolute():
        process_path = debug_dir() / process_path
    return is_debug_app_exe(process_path.resolve())


def kill_pid(pid: int) -> None:
    if sys.platform == "win32":
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], check=False, capture_output=True)
    else:
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass


def wait_for_process_exit(pid: int) -> None:
    for _ in range(50):
        if not process_exists(pid):
            return
        time.sleep(0.1)


def process_exists(pid: int) -> bool:
    if sys.platform == "win32":
        result = subprocess.run(
            ["powershell", "-NoProfile", "-Command", f"Get-Process -Id {pid} -ErrorAction Ignore"],
            capture_output=True,
            check=False,
        )
        return result.returncode == 0

    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def wait_for_port() -> None:
    deadline = time.time() + PORT_WAIT_SECONDS
    while time.time() < deadline:
        if port_owner_pid() is not None:
            print(f"调试端口 {PORT} 已就绪。")
            return
        time.sleep(0.2)
    raise TimeoutError(f"调试端口 {PORT_WAIT_SECONDS} 秒内未就绪，应用可能启动失败。")


def wait_for_port_closed() -> None:
    for _ in range(50):
        if port_owner_pid() is None:
            return
        time.sleep(0.1)
    raise TimeoutError("调试端口在停止后仍被占用。")


@contextmanager
def serialized_debug_command():
    COMMAND_LOCK_PATH.parent.mkdir(parents=True, exist_ok=True)
    with COMMAND_LOCK_PATH.open("a+b") as lock_file:
        if sys.platform == "win32":
            import msvcrt

            deadline = time.time() + COMMAND_TIMEOUT_SECONDS + 30
            while True:
                try:
                    lock_file.seek(0)
                    msvcrt.locking(lock_file.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError as exception:
                    if time.time() >= deadline:
                        raise TimeoutError("等待调试 CLI 串行锁超时。") from exception
                    time.sleep(0.1)

            try:
                yield
            finally:
                lock_file.seek(0)
                msvcrt.locking(lock_file.fileno(), msvcrt.LK_UNLCK, 1)
            return

        import fcntl

        fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)


# 服务端在收到完整一行后关闭连接，payload 可能多行。
def send_command(line: str) -> str:
    with serialized_debug_command():
        with socket.create_connection(("127.0.0.1", PORT), timeout=10) as client:
            client.settimeout(COMMAND_TIMEOUT_SECONDS)
            client.sendall((line + "\n").encode("utf-8"))
            response = client.makefile("r", encoding="utf-8").read().rstrip("\n")

    if response == "OK":
        return ""

    if response.startswith("OK "):
        return response[3:]

    if response.startswith("ERR "):
        raise RuntimeError(response[4:])

    raise RuntimeError(f"未知响应格式: {response}")


if __name__ == "__main__":
    sys.exit(main())
