<div align="center">

# AstraGram

<br>

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white&style=flat-square)](https://en.cppreference.com/w/cpp/20)
[![Qt](https://img.shields.io/badge/Qt-41CD52?logo=qt&logoColor=white&style=flat-square)](https://www.qt.io)
[![Windows](https://img.shields.io/badge/Windows-0078D6?logo=windows&logoColor=white&style=flat-square)](#-download)
[![Linux](https://img.shields.io/badge/Linux-FCC624?logo=linux&logoColor=black&style=flat-square)](#-download)
[![macOS](https://img.shields.io/badge/macOS-000000?logo=apple&logoColor=white&style=flat-square)](#-download)
[![GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.html)

</div>

<br>

AstraGram is a desktop client for Windows, Linux, and macOS, built on
[Telegram Desktop](https://github.com/telegramdesktop/tdesktop) by way of
[AyuGram](https://github.com/AyuGram/AyuGramDesktop) and
[re-zero001/AyuGramDesktop](https://github.com/re-zero001/AyuGramDesktop).

The whole point of this fork is making the client look and behave the way *you*
want. Corner radii, switch styles, bubble shapes, which buttons appear in the
compose area, which entries live in the side drawer, how emoji are rendered,
how Chinese and English text sit next to each other — all of it is a toggle
away. The night theme uses flat surface colours with distinct panels, controls
and borders.

It is also built to be worked on quickly. The repository carries a specification
written for AI coding assistants, a debug server inside Debug builds, and build
scripts that turn "edit a line" into "verified running build" in a couple of
minutes.

<br>

---

## Navigation

- [Customization](#-customization)
- [Download](#-download)
- [Building from Source](#-building-from-source)
  - [Windows](#windows)
  - [Linux (Docker)](#linux-docker)
  - [macOS](#macos)
- [AI-Assisted Development](#-ai-assisted-development)
- [Repository Layout](#-repository-layout)
- [发布与版本号](#-发布与版本号)
- [License](#-license)
- [Acknowledgements](#-acknowledgements)

<br>

---

## ✨ Customization

<sub>[↑ Back to Navigation](#navigation)</sub>

Everything below lives under **Settings → AyuGram Preferences**. Most options
apply the moment you flip them; the few that cannot (avatar corners, bubble
radius, wide-message multiplier, stories, the Zalgo filter and spacing on
receive) offer to restart for you. No config file editing either way.

### Appearance

| What you can change | Details |
|---|---|
| Avatar corners | Free slider from circle to square, applied everywhere avatars appear |
| Message bubbles | Corner radius, tail on/off, sticker scale |
| Switch style | The MD3 switch style, or the stock one |
| Night theme | Flat surfaces with distinct panel, control and border colours |
| Chat background | Turn custom backgrounds off for a flat, uniform chat area |
| App icon | Twelve icons to choose from for the desktop and taskbar |
| Monospace font | Pick the font used for code blocks and monospace text |
| Window | Optional wider default window and wide-message multipliers |

### Chat list and side drawer

| What you can change | Details |
|---|---|
| Folder tabs | Hide the "All chats" tab, show or hide per-tab unread counters |
| Taskbar badge | Hide the unread counter drawn on the app icon in the taskbar and tray |
| Drawer entries | Choose which of My Profile, Saved Messages, Archive, Contacts, Calls, Bots, New Group, New Channel and Night Mode appear in the main menu |

### Messages and compose area

| What you can change | Details |
|---|---|
| Compose buttons | Show or hide the attach, emoji, commands, microphone, gift and auto-delete buttons individually |
| Popups | Turn the attach and emoji hover popups on or off |
| Reply blocks | Simple quotes and replies, or the full colourful style |
| Edited marker | Keep the text, swap it for an icon, or write your own marker |
| Bottom info | Replace the text row under a message with compact icons |
| Timestamps | Show seconds, show message IDs, show peer IDs and DC numbers |
| Context menu | Add the entries you want: message details, view list, reactions panel, repeat message, add to folder |
| Stickers | Recent sticker count, panel scale, hide the greeting sticker |

### Text handling

| What you can change | Details |
|---|---|
| CJK–Latin spacing | Insert spaces between Chinese/Japanese/Korean text and Latin words or digits — on send, on receive, or while editing. Mentions, links, e-mail addresses and code blocks are left untouched |
| Zalgo filter | Strip abusive stacks of combining characters from incoming text |
| Link previews | Improved preview handling, optional warning before opening external links |
| Translation | Choose the translation provider used by the built-in translator |

### Emoji packs

Emoji fonts are downloaded on demand instead of being baked into the binary, so
the installer stays small and you only fetch what you pick:

| Pack | Size |
|---|---|
| Apple | ~111 MB |
| JoyPixels | ~13 MB |
| Samsung One UI | ~20 MB |

Each download is checked against a pinned SHA-256 while it streams, so a
truncated or tampered file never gets installed. You can also point the picker
at any colour emoji font on disk and import that instead. Telegram Desktop's own
sets stay where they were, and the built-in set remains the default.

### Languages

English and Simplified Chinese ship with the binary; the rest of Telegram
Desktop's cloud language packs keep working as usual.

<br>

---

## 📦 Download

<sub>[↑ Back to Navigation](#navigation)</sub>

Grab the archive for your platform from the
**[Releases page](https://github.com/Kindness-Kismet/AyuGramDesktop-Plus/releases/latest)**:

| Platform | File |
|---|---|
| Windows · x64 | `AstraGram-v<version>-win-x64.zip` |
| Windows · arm64 | `AstraGram-v<version>-win-arm64.zip` |
| Linux · x64 | `AstraGram-v<version>-linux-x64.zip` |
| Linux · arm64 | `AstraGram-v<version>-linux-arm64.zip` |
| macOS · Intel | `AstraGram-v<version>-macos-x64.zip` |
| macOS · Apple Silicon | `AstraGram-v<version>-macos-arm64.zip` |

The archives are portable — unpack anywhere and run. Releases also carry the
update packages the built-in updater uses, so an installed copy can upgrade
itself.

<br>

---

## 🛠 Building from Source

<sub>[↑ Back to Navigation](#navigation)</sub>

Two Python scripts do all the work: `prebuild.py` compiles the third-party
stack once, `build.py` configures and builds the app every time after that.

### Windows

**Prerequisites**

- Visual Studio 2026 (Community is fine) with the **Desktop development with
  C++** workload — that brings the v145 toolset (MSVC 14.51), ATL/MFC headers
  and a bundled CMake. Older Visual Studio versions will not work.
- Windows 10 SDK 10.0.26100.0 or later (tick it in the installer if missing).
- [Python](https://www.python.org/downloads/) 3.10+ and
  [Git](https://git-scm.com/download/win).
- About 60 GB of free disk space: roughly 10 GB for dependencies, the rest for
  build intermediates and output.

```bash
git clone --recursive https://github.com/Kindness-Kismet/AyuGramDesktop-Plus
cd AyuGramDesktop-Plus

# One-time: build the third-party stack (~30 stages, cached under build/tmp)
python scripts/prebuild.py
python scripts/prebuild.py --list    # see the stage list first

# Build the app
python scripts/build.py              # Release
python scripts/build.py --dev        # Debug, also collects the PDB
python scripts/build.py --jobs 8     # fewer parallel jobs, see the memory note
```

Output lands in `build/AstraGram-v<version>-win-x64-{release|dev}/`. Adding
`--pack` also produces `build/AstraGram-v<version>-win-x64.zip` with just the
executables; `--clean-pack` clears runtime leftovers (tdata, logs) from the
output directory first.

Worth knowing:

- Dependencies only need rebuilding when submodule pointers or
  `Telegram/build/version` change. Day-to-day edits just run `build.py`.
- Builds use the public test API credentials by default. Pass
  `--api-id` / `--api-hash` to use your own.
- The build defaults to 32 parallel jobs and accepts `--jobs 1` through
  `--jobs 128`. Memory use depends on the files being compiled, other running
  applications and available commit memory; lower the job count when needed.
- The `cmake` submodule is upstream `desktop-app/cmake_helpers`; local patches
  are applied at configure time by `scripts/build_support/cmake_patch.py`, so
  git reporting "modified content" on that submodule is expected.

### Linux (Docker)

The reference Linux build runs in the same Rocky Linux 8 container as CI, so
your host distribution does not matter.

**Prerequisites**: [Docker Engine](https://docs.docker.com/engine/install/) 20+,
[Poetry](https://python-poetry.org/docs/#installation), Python 3.8+, and about
60 GB of free space (the dependency image alone is roughly 20 GB).

```bash
git clone --recursive https://github.com/Kindness-Kismet/AyuGramDesktop-Plus
cd AyuGramDesktop-Plus/Telegram/build/docker/centos_env

# Generate a Release Dockerfile (empty DEBUG= and LTO= disable those flags)
poetry install
DEBUG= LTO= poetry run gen_dockerfile > Dockerfile

# Build the dependency image (first run takes 1-2 hours)
docker build -t ayugram:centos_env .

cd ../../../..   # back to the repository root

docker run --rm \
  -v "$PWD":/usr/src/tdesktop \
  -e CONFIG=Release \
  ayugram:centos_env \
  /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
  -D CMAKE_CONFIGURATION_TYPES=Release \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
```

The binary lands in `out/Release/AyuGram` inside the mounted volume. Rerun the
same `docker run` for incremental builds — objects live in the mounted `out/`
directory, so only what changed is recompiled.

### macOS

Dependencies are built natively (Qt 6.11.2, Release) into `Libraries/` next to
the checkout.

**Prerequisites**: Xcode 14+, [Homebrew](https://brew.sh), Python 3 (from
`xcode-select --install`), and about 40 GB of free space.

```bash
brew install automake libtool meson nasm ninja pkg-config
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer

git clone --recursive https://github.com/Kindness-Kismet/AyuGramDesktop-Plus
cd AyuGramDesktop-Plus

# One-time: build dependencies (drop 'silent' to see all output)
./Telegram/build/prepare/mac.sh silent

cd Telegram
./configure.sh \
  -D CMAKE_CONFIGURATION_TYPES=Release \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627 \
  -D CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
cmake --build ../out --config Release --parallel
```

The bundle lands in `out/Release/AyuGram.app`. As on Windows, dependencies only
need rebuilding when submodule pointers or `Telegram/build/version` change.

<br>

---

## 🤖 AI-Assisted Development

<sub>[↑ Back to Navigation](#navigation)</sub>

This repository is set up so an assistant — or a human who would rather not
click through a debugger — can get from a code change to a verified running
build without leaving the terminal.

### One specification

[`AGENTS.md`](AGENTS.md) is the first thing to read, and `CLAUDE.md` simply
imports it. It covers where each kind of change belongs, naming rules, the
reactive (rpl) and threading conventions, build knowledge, and a review
checklist. Keeping it accurate is part of every change.

### Skills

`.claude/skills/` and `.codex/skills/` hold byte-identical instruction packages
that Claude Code, Codex CLI and similar harnesses pick up automatically:

| Skill | What it does |
|---|---|
| `app-debug` | Drives a running Debug build: read and write settings, send real messages, take screenshots, click widgets, inject a local session |
| `upstream-sync` | Checks for new official stable releases, reports what changed in tdesktop and every submodule since the adapted version, and records the new version once adapted |
| `version-bump` | Updates the app version in its single source file and writes the release notes |
| `commit` | Atomic, scoped commits with path-by-path staging |
| `pull-request` | PR structure, review checklist, scope control |

### A debug server inside Debug builds

Debug builds (guarded by `_DEBUG`, absent from Release binaries) listen on
`127.0.0.1:20100` and accept one-line text commands over TCP. Each connection
handles one command and replies with `OK` plus an optional payload, or `ERR`
plus a reason. Structured payloads use JSON.

- **Lifecycle** — start, restart and stop the app from the command line. The
  build script stops matching project executables before collecting output.
- **Settings** — list keys, dump everything, read and write any value through
  the same code path the settings UI uses.
- **Observation** — screenshot the active window, dump the widget tree,
  synthesize clicks that travel the real event path.
- **Diagnostics** — local storage usage, message rendering statistics and
  update-channel info. Unhandled crashes are written to `crash.log` in the
  working directory.

### The loop

```bash
python scripts/build.py --dev
python .claude/skills/app-debug/scripts/cli.py app.ensure
python .claude/skills/app-debug/scripts/cli.py settings.set avatarCorners 8
python .claude/skills/app-debug/scripts/cli.py screenshot.take
python .claude/skills/app-debug/scripts/cli.py app.stop
```

A single `.cpp` change relinks in about two minutes, so this loop is fast
enough to run after every edit — which is how the customization work in this
fork was verified.

<br>

---

## 📁 Repository Layout

<sub>[↑ Back to Navigation](#navigation)</sub>

```
scripts/                   prebuild.py, build.py and their support package
Telegram/
  SourceFiles/extras/         everything this fork adds, one directory per
                           feature under features/
  SourceFiles/…            upstream Telegram Desktop source
  lib_ui, lib_tl, codegen  forked submodules; the other lib_* modules are
                           upstream and treated as read-only
.github/
  upstream.json            the official stable version this fork is
                           adapted to, and paths the sync report skips
  CHANGELOG.md             release notes for the current version
  workflows/               release pipeline and version tagging
AGENTS.md                  the specification (CLAUDE.md imports it)
.claude/, .codex/          skill packages for AI assistants
```

<br>

---

## 🚀 发布与版本号

<sub>[↑ 返回导航](#navigation)</sub>

版本号采用 `7.2.9.5` 这样的格式。前三段对应本项目已适配的官方 Telegram Desktop
版本，第四段是基于该版本的本项目修订号。

发布完全由版本文件驱动：

1. 更新唯一的版本文件 `Telegram/build/version`，CMake 会据此生成代码和 Windows
   资源中的版本值。同时重写 `.github/CHANGELOG.md`，先写英文条目，再写独立的
   `---` 分隔行，最后写一一对应的简体中文条目。
2. 推送到 `main` 后，**Build-release** 工作流校验更新说明，并行调度各平台构建仓库，
   按本次推送的提交编号获取源码。
3. 构建完成后，发布任务校验产物的来源运行、构建运行、版本、文件集合、大小和哈希值。
   所有校验通过后，在上传发布资产的前一步创建指向该提交的 `v<版本号>` 标签。

更新说明不符合双语格式时，工作流会停止；整个文件会作为发布正文。
重跑时，已指向同一提交的标签可以复用；标签指向其他提交时拒绝发布，不移动已有标签。

<br>

---

## 📄 License

<sub>[↑ Back to Navigation](#navigation)</sub>

Licensed under [GNU GPL v3](https://www.gnu.org/licenses/gpl-3.0.html) or
later, the same as Telegram Desktop and AyuGram. See [`LICENSE`](LICENSE) for
the full text and [`LEGAL`](LEGAL) for third-party notices.

<br>

---

## 🙏 Acknowledgements

<sub>[↑ Back to Navigation](#navigation)</sub>

This fork stands on other people's work:

- **[re-zero001/AyuGramDesktop](https://github.com/re-zero001/AyuGramDesktop)** —
  the direct upstream of this repository, and the foundation everything here
  builds on.
- **[AyuGram](https://github.com/AyuGram/AyuGramDesktop)** — the AyuGram line
  of forks and its customization work.
- **[Telegram Desktop](https://github.com/telegramdesktop/tdesktop)** — the
  official client the whole family is built on.
