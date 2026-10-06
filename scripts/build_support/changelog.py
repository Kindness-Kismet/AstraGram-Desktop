"""校验双语更新日志，整个文件即当前版本的发布正文。"""

from __future__ import annotations

# 英文条目在上，简体中文条目在下，中间只隔一行 ---。
_SEPARATOR = "\n\n---\n\n"
_TRAILING_PUNCTUATION = tuple(".,;:!?。，；：！？")


def _entries(block: str, language: str) -> list[str]:
    lines = block.split("\n")
    for line in lines:
        if not line.startswith("- ") or not line[2:].strip():
            raise SystemExit(f"{language} entries must start with '- ' and must not be empty: {line!r}")
        if line.rstrip().endswith(_TRAILING_PUNCTUATION):
            raise SystemExit(f"{language} entries must not end with punctuation: {line!r}")
    return lines


def validate_changelog(text: str) -> str:
    """格式不符时直接退出，返回去掉首尾空白的发布正文。"""
    notes = text.replace("\r\n", "\n").strip()
    blocks = notes.split(_SEPARATOR)
    if len(blocks) != 2:
        raise SystemExit("Changelog must contain English entries, a blank line, ---, a blank line, and Simplified Chinese entries")

    english = _entries(blocks[0], "English")
    chinese = _entries(blocks[1], "Chinese")
    if len(english) != len(chinese):
        raise SystemExit(f"Changelog entry counts differ: {len(english)} English, {len(chinese)} Chinese")
    return notes + "\n"
