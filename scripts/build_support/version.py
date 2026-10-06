from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from build_support.changelog import validate_changelog
from build_support.paths import ROOT, VERSION_FILE

_PATTERN = re.compile(r"^\s*(\d+)\.(\d+)\.(\d+)(?:\.(\d+|beta))?\s*$")
_PACKER_VERSION_MAX = 999_999_999

_UPSTREAM_TRACKING = ROOT / ".github" / "upstream.json"
_CHANGELOG = ROOT / ".github" / "CHANGELOG.md"


@dataclass(frozen=True)
class Version:
    original: str
    major: int
    minor: int
    patch: int
    revision: int
    beta: bool

    @property
    def full(self) -> int:
        """保留官方整数编码，供存储格式和上游迁移阈值使用。"""
        return self.major * 1_000_000 + self.minor * 1_000 + self.patch

    @property
    def update(self) -> int:
        """生成 Packer 可接受且按官方版本和本库修订号递增的更新码。"""
        return self.major * 10_000_000 + self.minor * 100_000 + self.patch * 100 + self.revision

    @property
    def storage_read(self) -> int:
        """存储读取上限跟随官方版本，不兼容未来版本写入的数据。"""
        return self.full

    @property
    def full_alpha(self) -> int:
        return 0

    @property
    def alpha(self) -> int:
        return 0

    @property
    def text(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"

    @property
    def text_small(self) -> str:
        return f"{self.text}.{self.revision}" if self.revision else self.text

    @property
    def file_version(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}.{self.revision}"

    @property
    def channel(self) -> str:
        return "beta" if self.beta else "stable"


def parse_version(text: str) -> Version:
    match = _PATTERN.fullmatch(text)
    if not match:
        raise SystemExit(f"Bad version '{text}'. Expected major.minor.patch[.revision|.beta].")

    raw_major, raw_minor, raw_patch, suffix = match.groups()
    raw_revision = "0" if suffix in (None, "beta") else suffix
    limits = (
        ("major", raw_major, 99),
        ("minor", raw_minor, 99),
        ("patch", raw_patch, 999),
        ("revision", raw_revision, 99),
    )
    values: dict[str, int] = {}
    for name, raw, limit in limits:
        value = int(raw)
        if str(value) != raw or value > limit:
            raise SystemExit(f"Bad {name} version part: {raw}; expected 0..{limit} without leading zeroes.")
        values[name] = value
    if suffix == "0":
        raise SystemExit("Bad revision version part: 0; omit .0 for the first release.")

    version = Version(
        original=text.strip(),
        major=values["major"],
        minor=values["minor"],
        patch=values["patch"],
        revision=values["revision"],
        beta=suffix == "beta",
    )
    if not 1_016 < version.update <= _PACKER_VERSION_MAX:
        raise SystemExit(f"Update version {version.update} is outside Packer range 1017..{_PACKER_VERSION_MAX}.")
    return version


def read_current_version() -> str:
    if not VERSION_FILE.is_file():
        raise SystemExit(f"{VERSION_FILE} not found.")
    for line in VERSION_FILE.read_text(encoding="utf-8").splitlines():
        match = re.match(r"\s*AppVersionOriginal\s+(\S+)", line)
        if match:
            return match.group(1)
    raise SystemExit(f"AppVersionOriginal not found in {VERSION_FILE}.")


def read_upstream_version() -> str:
    """已适配的官方 tag 版本号，由 scripts/upstream.py done 登记。"""
    try:
        data = json.loads(_UPSTREAM_TRACKING.read_text(encoding="utf-8"))
        return str(data["tdesktop"])
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise SystemExit(f"Could not read official version from {_UPSTREAM_TRACKING}: {error}") from error


def apply_version(version: Version, check_changelog: bool = True) -> list[str]:
    upstream = read_upstream_version()
    if version.text != upstream:
        raise SystemExit(f"Version {version.text_small} must use the official baseline {upstream} as its first three parts.")
    if check_changelog:
        _check_changelog()

    # 版本文件是唯一来源，代码与 Windows 资源在 CMake 配置阶段从它生成。
    return _replace(VERSION_FILE, [
        (r"(AppVersion\s+)\d+", r"\g<1>" + str(version.full)),
        (r"(AppUpdateVersion\s+)\d+", r"\g<1>" + str(version.update)),
        (r"(AppStorageReadVersion\s+)\d+", r"\g<1>" + str(version.storage_read)),
        (r"(AppVersionStrMajor\s+)\d[\d\.]*", r"\g<1>" + f"{version.major}.{version.minor}"),
        (r"(AppVersionStrOfficial\s+)\d[\d\.]*", r"\g<1>" + version.text),
        (r"(AppVersionStrSmall\s+)\d[\d\.]*", r"\g<1>" + version.text_small),
        (r"(AppVersionStrFile\s+)\d[\d\.]*", r"\g<1>" + version.file_version),
        (r"(AppVersionStr\s+)\d[\d\.]*", r"\g<1>" + version.text_small),
        (r"(BetaChannel\s+)\d", r"\g<1>" + ("1" if version.beta else "0")),
        (r"(AlphaVersion\s+)\d+", r"\g<1>0"),
        (r"(AppVersionOriginal\s+)\d[\d\.beta]*", r"\g<1>" + version.original),
    ])


def _check_changelog() -> None:
    if not _CHANGELOG.is_file():
        raise SystemExit(f"{_CHANGELOG} not found.")
    # 与 scripts/release_notes.py 使用同一套规则，发版前就拦下格式错误。
    validate_changelog(_CHANGELOG.read_text(encoding="utf-8"))


def _replace(path: Path, rules: list[tuple[str, str]]) -> list[str]:
    if not path.is_file():
        raise SystemExit(f"{path} not found.")

    original = path.read_text(encoding="utf-8")
    content = original
    for pattern, replacement in rules:
        if not re.search(pattern, content):
            raise SystemExit(f'Could not find "{pattern}" in {path}.')
        content = re.sub(pattern, replacement, content)

    if content == original:
        return []
    path.write_text(content, encoding="utf-8")
    return [str(path.relative_to(ROOT))]


def main(argv: list[str] | None = None) -> None:
    import argparse

    from build_support.console import header, print_summary
    from build_support.help import MultilineHelpFormatter

    parser = argparse.ArgumentParser(
        prog="python -m build_support.version",
        description="Show or set the AyuGram version in Telegram/build/version",
        formatter_class=MultilineHelpFormatter,
    )
    parser.add_argument(
        "version",
        nargs="?",
        metavar="VERSION",
        help="New version as major.minor.patch[.revision|.beta]; omit to show the current one",
    )
    parser.add_argument(
        "--skip-changelog",
        action="store_true",
        help="Do not validate .github/CHANGELOG.md",
    )
    args = parser.parse_args(argv)

    if not args.version:
        current = parse_version(read_current_version())
        print(header("Current version"))
        print(f"  Version         {current.original}")
        print(f"  Channel         {current.channel}")
        print(f"  Official code   {current.full}")
        print(f"  Update code     {current.update}")
        return

    version = parse_version(args.version)
    print(header(f"Setting version {version.text_small} {version.channel}"))
    touched = apply_version(version, check_changelog=not args.skip_changelog)
    print_summary(*(f"patched {path}" for path in touched) if touched else ["already up to date"])


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    main()
