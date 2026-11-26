"""生成或签署 AstraGram V2 更新密钥清单。"""

from __future__ import annotations

import sys

sys.dont_write_bytecode = True

import os

os.environ["PYTHONDONTWRITEBYTECODE"] = "1"

import argparse
import base64
import json
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PUBLIC_DIRECTORY = ROOT / "Telegram/Resources/update"
RELEASE_KEY_ID = "astragram-release-1"


def openssl_path(explicit: str | None) -> str:
    if explicit:
        return explicit
    installed = shutil.which("openssl")
    if installed:
        return installed
    bundled = ROOT / "build/tmp/ThirdParty/msys64/usr/bin/openssl.exe"
    if bundled.is_file():
        return str(bundled)
    raise SystemExit("OpenSSL is required; pass --openssl with its executable path.")


def run(openssl: str, *arguments: str) -> bytes:
    result = subprocess.run([openssl, *arguments], capture_output=True)
    if result.returncode:
        raise SystemExit("OpenSSL command failed: " + result.stderr.decode("utf-8", errors="replace"))
    return result.stdout


def initialize(private_directory: Path, public_directory: Path, openssl: str) -> None:
    root_key = private_directory / "root-private.pem"
    release_key = private_directory / "release-private.pem"
    if root_key.exists() or release_key.exists():
        raise SystemExit("Private keys already exist; refusing to replace the update trust root.")
    private_directory.mkdir(parents=True, exist_ok=True)
    public_directory.mkdir(parents=True, exist_ok=True)
    for path in (root_key, release_key):
        with path.open("xb"):
            pass
        path.chmod(0o600)
        run(openssl, "genpkey", "-algorithm", "Ed25519", "-out", str(path))
    public = run(openssl, "pkey", "-in", str(release_key), "-pubout", "-outform", "DER")
    # Ed25519 的 SubjectPublicKeyInfo 固定包含 32 字节公钥。
    if len(public) != 44 or public[:12] != bytes.fromhex("302a300506032b6570032100"):
        raise SystemExit("Unexpected Ed25519 public key encoding.")
    coordinate = base64.urlsafe_b64encode(public[12:]).rstrip(b"=").decode("ascii")
    manifest = {
        "format": 1,
        "manifest_version": 1,
        "issued": int(time.time()),
        "keys": [{"id": RELEASE_KEY_ID, "alg": "Ed25519", "x": coordinate}],
        "channels": {name: [[RELEASE_KEY_ID]] for name in ("stable", "beta")},
        "revoked": [],
    }
    manifest_path = public_directory / "manifest.min.json"
    manifest_path.write_bytes(json.dumps(manifest, separators=(",", ":")).encode("utf-8"))
    root_public = run(openssl, "pkey", "-in", str(root_key), "-pubout")
    (public_directory / "root-public.pem").write_bytes(root_public.replace(b"\r\n", b"\n"))
    sign_manifest(private_directory, public_directory, openssl)
    print("Generated independent root and release keys; keep the root private key offline.")
    print("CI signing secret: UPDATE_SIGNING_KEY; key id: " + RELEASE_KEY_ID)


def sign_manifest(private_directory: Path, public_directory: Path, openssl: str) -> None:
    root_key = private_directory / "root-private.pem"
    public_key = public_directory / "root-public.pem"
    actual_public = run(openssl, "pkey", "-in", str(root_key), "-pubout")
    if actual_public.replace(b"\r\n", b"\n") != public_key.read_bytes().replace(b"\r\n", b"\n"):
        raise SystemExit("The private root does not match the committed public root.")
    manifest = public_directory / "manifest.min.json"
    signature = public_directory / "manifest.sig"
    run(openssl, "pkeyutl", "-sign", "-rawin", "-inkey", str(root_key), "-in", str(manifest), "-out", str(signature))
    run(openssl, "pkeyutl", "-verify", "-rawin", "-pubin", "-inkey", str(public_key), "-in", str(manifest), "-sigfile", str(signature))
    print("Manifest root signature verified.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("init", "sign-manifest"))
    parser.add_argument("--private-dir", type=Path, required=True)
    parser.add_argument("--public-dir", type=Path, default=PUBLIC_DIRECTORY)
    parser.add_argument("--openssl")
    arguments = parser.parse_args()
    tool = openssl_path(arguments.openssl)
    if arguments.action == "init":
        initialize(arguments.private_dir.resolve(), arguments.public_dir.resolve(), tool)
        return
    sign_manifest(arguments.private_dir.resolve(), arguments.public_dir.resolve(), tool)


if __name__ == "__main__":
    main()
