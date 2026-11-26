#!/usr/bin/env python3
"""使用原生 Packer 和发布密钥生成 TDUP v2 更新包。"""

from __future__ import annotations

import sys

sys.dont_write_bytecode = True

import os

os.environ["PYTHONDONTWRITEBYTECODE"] = "1"

import argparse
from contextlib import contextmanager
from pathlib import Path
import subprocess
import tempfile
from typing import Iterator


ROOT = Path(__file__).resolve().parents[1]
KEY_ID = "astragram-release-1"
UPDATE_OS = {"windows": "win", "linux": "linux", "macos": "mac"}
UPDATE_ARCH = {"x64": "x64", "arm64": "arm"}
NATIVE_PLATFORMS = {"win32": "windows", "linux": "linux", "darwin": "macos"}


def read_version(path: Path) -> tuple[int, str]:
    fields = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if not parts or parts[0] not in ("AppUpdateVersion", "BetaChannel"):
            continue
        if len(parts) != 2 or parts[0] in fields:
            raise ValueError(f"Invalid or duplicate version field: {parts[0]}")
        fields[parts[0]] = parts[1]
    if set(fields) != {"AppUpdateVersion", "BetaChannel"}:
        raise ValueError("Version file must contain AppUpdateVersion and BetaChannel")
    if fields["BetaChannel"] not in ("0", "1"):
        raise ValueError("BetaChannel must be 0 or 1")
    version = int(fields["AppUpdateVersion"])
    validate_version(version)
    return version, "beta" if fields["BetaChannel"] == "1" else "stable"


def validate_version(version: int) -> None:
    if not 1_016 < version <= 999_999_999:
        raise ValueError("AppUpdateVersion must be within 1017..999999999")


def update_filename(platform: str, arch: str, version: int, channel: str) -> str:
    suffix = "-beta" if channel == "beta" else ""
    return f"td-update-{UPDATE_OS[platform]}-{UPDATE_ARCH[arch]}-{version}{suffix}"


@contextmanager
def signing_key(private_key: Path | None) -> Iterator[Path]:
    # CI 密钥只写入进程临时目录，子进程环境不再携带 PEM 内容。
    secret = os.environ.pop("UPDATE_SIGNING_KEY", "")
    if private_key is not None:
        if not private_key.is_file():
            raise ValueError(f"Release private key does not exist: {private_key}")
        yield private_key.resolve()
        return
    if not secret.strip():
        raise ValueError("Provide --private-key or the UPDATE_SIGNING_KEY environment variable")
    with tempfile.TemporaryDirectory(prefix="astragram-update-key-") as directory:
        key = Path(directory) / "release-private.pem"
        with key.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(secret)
        key.chmod(0o600)
        yield key


def package_update(args: argparse.Namespace) -> Path:
    if NATIVE_PLATFORMS.get(sys.platform) != args.platform:
        raise ValueError(f"Run the {args.platform} Packer on its native platform")
    file_version, file_channel = read_version(args.version_file)
    version = file_version if args.version is None else args.version
    channel = file_channel if args.channel is None else args.channel
    validate_version(version)
    packer = args.packer.resolve()
    if not packer.is_file():
        raise ValueError(f"Packer executable does not exist: {packer}")
    input_dir = args.input_dir.resolve()
    if not input_dir.is_dir():
        raise ValueError(f"Input directory does not exist: {input_dir}")
    paths = []
    for relative in args.path:
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"Package paths must be relative to --input-dir: {relative}")
        path = (input_dir / relative).resolve(strict=True)
        if not path.is_relative_to(input_dir):
            raise ValueError(f"Package path is outside --input-dir: {relative}")
        paths.append(path)
    keys_dir = args.keys_dir.resolve()
    for name in ("root-public.pem", "manifest.min.json", "manifest.sig"):
        if not (keys_dir / name).is_file():
            raise ValueError(f"Update verification resource does not exist: {keys_dir / name}")

    output = args.output_dir / update_filename(args.platform, args.arch, version, channel)
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(packer), "-version", str(version), "-channel", channel,
        "-keys-loc", str(keys_dir), "-local-key-id", KEY_ID,
    ]
    if args.platform == "windows":
        command += ["-target", "winarm" if args.arch == "arm64" else "win64"]
    else:
        command += ["-arch", "arm64" if args.arch == "arm64" else "x86_64"]
    for path in paths:
        command += ["-path", str(path)]
    with signing_key(args.private_key) as key:
        command += ["-local-key", str(key)]
        subprocess.run(command, cwd=output.parent.resolve(), stdout=sys.stderr, check=True)
    if not output.is_file() or not output.stat().st_size:
        raise ValueError(f"Packer did not produce the expected update: {output}")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packer", type=Path, required=True, help="Native Packer executable")
    parser.add_argument("--platform", choices=tuple(UPDATE_OS), required=True)
    parser.add_argument("--arch", choices=tuple(UPDATE_ARCH), required=True)
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--path", type=Path, action="append", required=True,
                        help="File or directory relative to --input-dir; repeat for each input")
    parser.add_argument("--version-file", type=Path, default=ROOT / "Telegram/build/version")
    parser.add_argument("--version", type=int, help="Override AppUpdateVersion for local validation")
    parser.add_argument("--channel", choices=("stable", "beta"), help="Override BetaChannel")
    parser.add_argument("--keys-dir", type=Path, default=ROOT / "Telegram/Resources/update")
    parser.add_argument("--private-key", type=Path, help="Release PEM file; otherwise use UPDATE_SIGNING_KEY")
    args = parser.parse_args()
    try:
        output = package_update(args)
    except (ValueError, UnicodeError) as error:
        parser.exit(1, f"Update packaging failed: {error}\n")
    except subprocess.CalledProcessError as error:
        parser.exit(1, f"Packer failed with exit code {error.returncode}\n")
    except OSError as error:
        parser.exit(1, f"Update packaging I/O failed: {type(error).__name__}, "
                       f"errno={error.errno}, winerror={getattr(error, 'winerror', None)}, "
                       f"filename={error.filename!r}, filename2={error.filename2!r}\n")
    print(output.as_posix())


if __name__ == "__main__":
    main()
