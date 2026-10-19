"""让 Qt 文本光标按画笔颜色绘制，不再反色。"""

import argparse
import sys
from pathlib import Path


PATCH_SCRIPT = Path(__file__).resolve()
_SOURCE = Path("src/gui/text/qtextlayout.cpp")
_ANCHOR = (
    "    QPainter::CompositionMode origCompositionMode = p->compositionMode();\n"
)
_RASTER_OP = (
    "    if (p->paintEngine()->hasFeature(QPaintEngine::RasterOpModes))\n"
    "        p->setCompositionMode(QPainter::RasterOp_NotDestination);\n"
)
# 调用方绘制光标前已把画笔设为文字色，去掉反色后直接按画笔填充。
_MARKER = "    // 光标直接用画笔颜色绘制：反色在透明底上会变成白色。\n"


def patch_command() -> str:
    return f'"{sys.executable}" "{PATCH_SCRIPT}" .'


def patch_source(source: bytes) -> bytes:
    """完整校验后返回补丁文本；不接受缺失锚点或半应用状态。"""
    newline = b"\r\n" if b"\r\n" in source else b"\n"
    text = source.decode("utf-8").replace("\r\n", "\n")
    if text.count(_ANCHOR) != 1:
        raise SystemExit("Qt text caret: composition mode anchor changed.")
    original = text.count(_ANCHOR + _RASTER_OP)
    patched = text.count(_ANCHOR + _MARKER)
    raster = text.count("RasterOp_NotDestination")
    markers = text.count(_MARKER)
    if patched == 1 and original == 0 and raster == 0 and markers == 1:
        return source
    if original != 1 or patched or raster != 1 or markers:
        raise SystemExit("Qt text caret: unexpected source or partial patch.")
    text = text.replace(_ANCHOR + _RASTER_OP, _ANCHOR + _MARKER, 1)
    return text.replace("\n", newline.decode("ascii")).encode("utf-8")


def apply_patch(qtbase: Path, dry_run: bool = False) -> bool:
    path = qtbase / _SOURCE
    if not path.is_file():
        raise SystemExit(f"Qt text caret: missing {path}")
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
    print("Qt text caret: " + ("patch validated" if args.dry_run else
          "patched" if changed else "already patched"))


if __name__ == "__main__":
    main()
