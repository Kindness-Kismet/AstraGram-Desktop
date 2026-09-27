"""校验双语更新日志，整个文件即当前版本的发布正文。"""

from __future__ import annotations

# 英文条目在上，简体中文条目在下，中间只隔一行 ---。
_SEPARATOR = "\n\n---\n\n"
_TRAILING_PUNCTUATION = tuple(".,;:!?。，；：！？")


def _entries(block: str, language: str) -> list[str]:
    lines = block.split("\n")
    for line in lines:
        if not line.startswith("- ") or not line[2:].strip():
            raise SystemExit(f"{language}条目必须以“- ”开头且不能为空：{line!r}")
        if line.rstrip().endswith(_TRAILING_PUNCTUATION):
            raise SystemExit(f"{language}条目末尾不加标点：{line!r}")
    return lines


def validate_changelog(text: str) -> str:
    """格式不符时直接退出，返回去掉首尾空白的发布正文。"""
    notes = text.replace("\r\n", "\n").strip()
    blocks = notes.split(_SEPARATOR)
    if len(blocks) != 2:
        raise SystemExit("更新日志必须是英文条目、空行、---、空行、简体中文条目")

    english = _entries(blocks[0], "英文")
    chinese = _entries(blocks[1], "中文")
    if len(english) != len(chinese):
        raise SystemExit(f"中英文条目数量不一致：英文 {len(english)} 条，中文 {len(chinese)} 条")
    return notes + "\n"
