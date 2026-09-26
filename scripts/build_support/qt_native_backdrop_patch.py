"""为 Windows Qt 平台插件加入逐窗口原生背景开关。"""

import argparse
import sys
from pathlib import Path


PATCH_SCRIPT = Path(__file__).resolve()
_SOURCE = Path("src/plugins/platforms/windows/qwindowswindow.cpp")
_INCLUDE = '#include "qwindowswindow.h"\n'
_VARIANT = '#include <QtCore/qvariant.h>'
_ADDED_VARIANT = _VARIANT + ' // 窗口原生背景能力。\n'
_CONSTRUCTOR = "    QWindowsContext::instance()->addWindow(m_data.hwnd, this);\n"
_CAPABILITY = '    aWindow->setProperty("_q_ayuNativeBackdropSupported", true);\n'
_PREFIX = "    const bool needsLayered = (flags & Qt::WindowTransparentForInput)\n        || ("
_SUFFIX = ") || opacity < 1.0;"
_ALPHA = ("hasAlpha", "hasAlpha && hasNoNativeFrame(hwnd, flags)")
_OPT_IN = '!GetPropW(hwnd, L"AyuGramNativeBackdrop") && '


def patch_command() -> str:
    return f'"{sys.executable}" "{PATCH_SCRIPT}" .'


def patch_source(source: bytes) -> bytes:
    """完整校验后返回补丁文本；不接受缺失锚点或半应用状态。"""
    newline = b"\r\n" if b"\r\n" in source else b"\n"
    text = source.decode("utf-8").replace("\r\n", "\n")
    original = [_PREFIX + alpha + _SUFFIX for alpha in _ALPHA]
    patched = [_PREFIX + _OPT_IN + alpha + _SUFFIX for alpha in _ALPHA]
    if text.count(_CONSTRUCTOR) != 1 or text.count(_INCLUDE) != 1:
        raise SystemExit("Qt native backdrop: constructor/include anchor changed.")
    before = sum(text.count(block) for block in original)
    after = sum(text.count(block) for block in patched)
    capabilities = text.count("_q_ayuNativeBackdropSupported")
    switches = text.count("AyuGramNativeBackdrop")
    added_include = text.count(_ADDED_VARIANT)
    if after == 1 and before == 0 and capabilities == 1 and switches == 1:
        if (_CONSTRUCTOR + _CAPABILITY not in text
                or _VARIANT not in text or added_include > 1):
            raise SystemExit("Qt native backdrop: incomplete existing patch.")
        return source
    if before != 1 or after or capabilities or switches or added_include:
        raise SystemExit("Qt native backdrop: unexpected source or partial patch.")
    if _VARIANT not in text:
        text = text.replace(_INCLUDE, _INCLUDE + _ADDED_VARIANT, 1)
    text = text.replace(_CONSTRUCTOR, _CONSTRUCTOR + _CAPABILITY, 1)
    for old, new in zip(original, patched):
        text = text.replace(old, new, 1)
    return text.replace("\n", newline.decode("ascii")).encode("utf-8")


def apply_patch(qtbase: Path, dry_run: bool = False) -> bool:
    path = qtbase / _SOURCE
    if not path.is_file():
        raise SystemExit(f"Qt native backdrop: missing {path}")
    source = path.read_bytes()
    patched = patch_source(source)
    changed = patched != source
    if changed and not dry_run:
        path.write_bytes(patched)
    return changed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qtbase", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    changed = apply_patch(args.qtbase, args.dry_run)
    print("Qt native backdrop: " + ("patch validated" if args.dry_run else
          "patched" if changed else "already patched"))


if __name__ == "__main__":
    main()
