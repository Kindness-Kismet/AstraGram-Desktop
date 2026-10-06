#!/usr/bin/env python3
"""同步官方 Telegram Desktop 稳定版与测试版。

.github/upstream.json 只登记已适配的官方版本号与通道；子模块基线取官方该版本记录的
子模块指针，本地定制一律用 git diff 计算。
"""
from __future__ import annotations

import argparse
import datetime
import json
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_support.cmake_patch import patch_anchors  # noqa: E402
from build_support.paths import ROOT  # noqa: E402
from build_support.upstream_releases import Release, parse_releases, pick, select_target, targets  # noqa: E402

TRACKING = ROOT / ".github" / "upstream.json"
OUT_DIR = ROOT / "build" / "upstream-sync"
OFFICIAL_URL = "https://github.com/telegramdesktop/tdesktop.git"
# 官方标签与 AyuGram 自己的发布标签同名，放进独立命名空间避免互相覆盖。
TAG_REFS = "refs/upstream-tags/"
# 本地文件不到官方一半且官方超过这个大小时，按“可能已拆分”提示。
_SPLIT_MIN_SIZE = 40_000


@dataclass
class Entry:
    path: str
    note: str = ""
    blocks: int = 0


@dataclass
class Report:
    name: str
    base: str
    head: str
    commits: list[str] = field(default_factory=list)
    take: list[Entry] = field(default_factory=list)
    merge: list[Entry] = field(default_factory=list)
    added: list[Entry] = field(default_factory=list)
    deleted: list[Entry] = field(default_factory=list)
    synced: list[Entry] = field(default_factory=list)
    skipped: list[Entry] = field(default_factory=list)
    conflicts: list[str] = field(default_factory=list)
    numstat: dict[str, str] = field(default_factory=dict)
    how: list[str] = field(default_factory=list)
    finished: bool = False

    @property
    def files(self) -> int:
        return sum(len(items) for items in (
            self.take, self.merge, self.added, self.deleted, self.synced, self.skipped))

    @property
    def manual(self) -> int:
        return sum(1 for entry in self.merge if entry.blocks != 0)


def run(args: list[str], cwd: Path) -> subprocess.CompletedProcess:
    # 子模块目录的所有者可能与当前用户不同，只对本次调用放行，不改全局配置。
    return subprocess.run(
        ["git", "-c", "safe.directory=*", *args], cwd=cwd, capture_output=True,
    )


def git(*args: str, cwd: Path = ROOT) -> str:
    done = run(list(args), cwd)
    if done.returncode != 0:
        error = done.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(f"git {' '.join(args)} failed:\n{error}")
    return done.stdout.decode("utf-8", errors="replace")


def blob(cwd: Path, revision: str, path: str) -> bytes | None:
    done = run(["show", f"{revision}:{path}"], cwd)
    return done.stdout if done.returncode == 0 else None


def has_commit(cwd: Path, sha: str) -> bool:
    return run(["cat-file", "-e", f"{sha}^{{commit}}"], cwd).returncode == 0


def contains(cwd: Path, ancestor: str, commit: str) -> bool:
    return run(["merge-base", "--is-ancestor", ancestor, commit], cwd).returncode == 0


def tree(cwd: Path, revision: str) -> dict[str, tuple[str, int]]:
    """返回 {路径: (blob, 字节数)}，只含普通文件。"""
    result = {}
    for line in git("ls-tree", "-r", "-l", "--full-tree", revision, cwd=cwd).splitlines():
        meta, _, path = line.partition("\t")
        _, kind, sha, size = meta.split()
        if kind == "blob":
            result[path] = (sha, int(size))
    return result


def gitlink(path: str) -> str | None:
    fields = git("ls-tree", "HEAD", "--", path).split()
    return fields[2] if len(fields) > 2 and fields[1] == "commit" else None


def load() -> dict:
    return json.loads(TRACKING.read_text(encoding="utf-8"))


def lookup(rules: dict[str, str], path: str) -> str | None:
    for key, text in rules.items():
        prefix = key.rstrip("/")
        if path == prefix or path.startswith(prefix + "/"):
            return text
    return None


def fetch_releases() -> list[Release]:
    # 直接按官方地址拉取，不依赖远程名，避免把同名远程里的其它仓库当成官方。
    print("Fetching upstream tags...")
    git("fetch", "--no-tags", "--force", OFFICIAL_URL, f"+refs/tags/v*:{TAG_REFS}v*")
    return releases()


def releases() -> list[Release]:
    """保留两种通道，基线查找不受目标通道限制。"""
    raw = git(
        "for-each-ref", TAG_REFS,
        "--format=%(refname:lstrip=2)\t%(if)%(*objectname)%(then)"
        "%(*objectname)\t%(*subject)\t%(*committerdate:short)%(else)"
        "%(objectname)\t%(subject)\t%(committerdate:short)%(end)",
    )
    return parse_releases(raw)


def submodules(revision: str) -> dict[str, tuple[str, str]]:
    """返回 {路径: (子模块提交, 地址)}，取自该提交的 .gitmodules 与子模块指针。"""
    config = git(
        "config", "--blob", f"{revision}:.gitmodules",
        "--get-regexp", r"^submodule\..*\.(path|url)$",
    )
    paths: dict[str, str] = {}
    urls: dict[str, str] = {}
    for line in config.splitlines():
        key, _, value = line.partition(" ")
        name, _, kind = key.removeprefix("submodule.").rpartition(".")
        (paths if kind == "path" else urls)[name] = value
    pins = {}
    for line in git("ls-tree", revision, "--", *paths.values()).splitlines():
        meta, _, path = line.partition("\t")
        _, kind, sha = meta.split()
        if kind == "commit":
            pins[path] = sha
    return {
        path: (pins[path], urls[name])
        for name, path in paths.items() if path in pins
    }


def rehearse(cwd: Path, path: str, base: str, ours: str, theirs: str) -> tuple[int, str]:
    """三方合并预演，返回 (冲突块数, 冲突片段)；二进制等无法合并时返回 -1。"""
    parts = [blob(cwd, revision, path) for revision in (ours, base, theirs)]
    names = []
    try:
        for part in parts:
            with tempfile.NamedTemporaryFile(delete=False) as file:
                file.write(part or b"")
            names.append(file.name)
        done = subprocess.run(
            ["git", "merge-file", "-p", "--diff3",
             "-L", "ours", "-L", "base", "-L", "theirs", *names],
            capture_output=True,
        )
    finally:
        for name in names:
            Path(name).unlink()
    if done.returncode < 0 or done.returncode > 127:
        return -1, ""
    blocks, keep, inside = 0, [], False
    for line in done.stdout.decode("utf-8", errors="replace").splitlines():
        if line.startswith("<<<<<<<"):
            inside = True
            blocks += 1
        if inside:
            keep.append(line)
        if line.startswith(">>>>>>>"):
            inside = False
    return blocks, "\n".join(keep)


def commits(cwd: Path, base: str, head: str) -> list[str]:
    return git("log", "--no-merges", "--format=%h %cs %s", f"{base}..{head}", cwd=cwd).splitlines()


def analyze(
        name: str,
        cwd: Path,
        prefix: str,
        base: str,
        head: str,
        ours: str,
        config: dict,
        exclude: set[str]) -> Report:
    """按本地状态分类官方改动；exclude 里的路径由调用方单独处理。"""
    skip, notes = config.get("skip", {}), config.get("notes", {})
    deferred = config.get("deferred", {})
    report = Report(name, base, head, commits(cwd, base, head))
    for line in git("diff", "--numstat", "--no-renames", base, head, cwd=cwd).splitlines():
        added, removed, path = line.split("\t", 2)
        report.numstat[path] = f"+{added}/-{removed}"
    old, new, mine = tree(cwd, base), tree(cwd, head), tree(cwd, ours)
    status = git("diff", "--name-status", "--no-renames", base, head, cwd=cwd)
    for line in status.splitlines():
        kind, path = line.split("\t", 1)
        if path in exclude:
            continue
        full = prefix + path
        reason = lookup(skip, full)
        if reason:
            report.skipped.append(Entry(path, reason))
            continue
        postponed = lookup(deferred, full)
        remark = f"暂缓项：{postponed}" if postponed else (lookup(notes, full) or "")
        local, target, origin = mine.get(path), new.get(path), old.get(path)
        if kind == "D":
            if local is None:
                report.synced.append(Entry(path, "本地也已删除"))
            else:
                changed = "本地改过" if local[0] != origin[0] else "本地未改"
                report.deleted.append(Entry(path, "；".join(filter(None, (changed, remark)))))
        elif local and target and local[0] == target[0]:
            report.synced.append(Entry(path, "与目标一致"))
        elif kind == "A" and local is None:
            report.added.append(Entry(path, remark))
        elif kind == "A":
            report.merge.append(Entry(path, "双方都新增了这个文件，需要人工处理", -1))
        elif local is None:
            report.merge.append(Entry(path, "本地已删除，官方有修改，需要人工处理", -1))
        elif local[0] == origin[0]:
            report.take.append(Entry(path, remark))
        else:
            blocks, preview = rehearse(cwd, path, base, ours, head)
            note = remark
            if origin[1] > _SPLIT_MIN_SIZE and local[1] * 2 < origin[1]:
                note = "本地文件不到官方一半，可能已按职责拆分，需要把官方改动手动分发到拆分后的文件"
                blocks = -1
            elif blocks < 0:
                note = "二进制文件，需要人工处理"
            report.merge.append(Entry(path, note, blocks))
            if preview:
                report.conflicts.append(f"{'=' * 70}\n{path}（{blocks} 块）\n{'=' * 70}\n{preview}")
    return report


def label(entry: Entry, numstat: dict[str, str], merging: bool) -> str:
    parts = [f"`{entry.path}`", numstat.get(entry.path, "")]
    text = entry.note
    if merging and not text:
        text = f"冲突 {entry.blocks} 块" if entry.blocks else "可自动合并"
    elif merging and entry.blocks > 0:
        text = f"冲突 {entry.blocks} 块；{text}"
    return " ".join(filter(None, parts)) + (f"：{text}" if text else "")


def render(report: Report) -> str:
    lines = [
        f"# {report.name}\n",
        f"基线 `{report.base[:10]}` → 目标 `{report.head[:10]}`，"
        f"官方新增 {len(report.commits)} 个提交，改动 {report.files} 个文件。\n",
    ]
    if report.how:
        lines += ["## 处理方式\n", *report.how, ""]
    if not report.finished:
        lines += [
            "| 类别 | 数量 |",
            "|---|---|",
            f"| 需要合并（本地改过） | {len(report.merge)}（冲突或人工 {report.manual}） |",
            f"| 直接采用（本地没改过） | {len(report.take)} |",
            f"| 官方新增 | {len(report.added)} |",
            f"| 官方删除 | {len(report.deleted)} |",
            f"| 已与目标一致 | {len(report.synced)} |",
            f"| 按 skip 跳过 | {len(report.skipped)} |",
        ]
        merge = sorted(report.merge, key=lambda entry: (-(entry.blocks != 0), -entry.blocks, entry.path))
        sections = (
            ("需要合并", [label(entry, report.numstat, True) for entry in merge]),
            ("直接采用", [label(entry, report.numstat, False) for entry in report.take]),
            ("官方新增", [label(entry, report.numstat, False) for entry in report.added]),
            ("官方删除", [label(entry, report.numstat, False) for entry in report.deleted]),
            ("按 skip 跳过", [label(entry, report.numstat, False) for entry in report.skipped]),
            ("已与目标一致", [label(entry, report.numstat, False) for entry in report.synced]),
        )
        for title, items in sections:
            if items:
                lines.append(f"\n## {title}（{len(items)}）\n")
                lines += [f"- {item}" for item in items]
    lines.append(f"\n## 官方提交（{len(report.commits)}）\n")
    lines += [f"- {commit}" for commit in report.commits]
    return "\n".join(lines) + "\n"


def write(out: Path, name: str, text: str) -> None:
    (out / name).write_text(text, encoding="utf-8", newline="\n")


def save(out: Path, stem: str, report: Report, cwd: Path) -> None:
    # 清单与冲突文件总是写出，内容为空也覆盖，避免重新生成后残留旧结果。
    for kind, entries in (
            ("take", report.take), ("merge", report.merge),
            ("added", report.added), ("deleted", report.deleted)):
        write(out, f"{stem}-{kind}.txt", "".join(f"{entry.path}\n" for entry in entries))
    write(out, f"{stem}-conflicts.txt", "\n\n".join(report.conflicts) + "\n")
    write(out, f"{stem}.md", render(report))
    diff = run(["diff", "--no-renames", report.base, report.head], cwd).stdout
    (out / f"{stem}.diff").write_bytes(diff)


def anchor_failures(head: str, cmake_pin: str | None) -> list[str]:
    failures = set()
    for path, original in patch_anchors():
        if path.startswith("cmake/"):
            content = blob(ROOT / "cmake", cmake_pin, path[6:]) if cmake_pin else None
        else:
            content = blob(ROOT, head, path)
        text = (content or b"").decode("utf-8", errors="replace").replace("\r\n", "\n")
        if original not in text:
            failures.add(path)
    return sorted(failures)


def fork_branch(cwd: Path) -> str:
    done = run(["symbolic-ref", "--short", "refs/remotes/origin/HEAD"], cwd)
    branch = done.stdout.decode().strip().removeprefix("origin/")
    return branch or "fork 的默认分支"


def check(args: argparse.Namespace) -> int:
    current = load()["tdesktop"]
    items = fetch_releases()
    adapted = pick(items, current)
    newer = [release for release in targets(items, args.channel) if release.key > adapted.key]
    print(f"Adapted upstream release: {current} ({adapted.channel}, {adapted.date})")
    if not newer:
        print(f"No newer upstream release in the {args.channel} channel; baseline stays at {current}.")
        return 0
    listed = ", ".join(f"{release.version} ({release.date})" for release in newer)
    print(f"Upstream has {len(newer)} newer releases in the {args.channel} channel: {listed}")
    target = newer[-1]
    count = len(git("rev-list", f"{adapted.commit}..{target.commit}").splitlines())
    old, new = submodules(adapted.commit), submodules(target.commit)
    moved = sorted(path for path in old.keys() | new.keys() if old.get(path) != new.get(path))
    print(f"{current} -> {target.version}: {count} new upstream commits, "
          f"{len(moved)} changed submodules{(': ' + ', '.join(moved)) if moved else ''}")
    print(f"Generate a synchronization report: python scripts/upstream.py report --channel {args.channel}")
    return 0


def report(args: argparse.Namespace) -> int:
    config = load()
    items = fetch_releases()
    base = pick(items, args.base or config["tdesktop"])
    head = select_target(items, args.to, args.channel)
    if head.key <= base.key:
        print(f"Target {head.version} is not newer than baseline {base.version}; nothing to synchronize.")
        return 0
    dirty = bool(git("status", "--porcelain", "--untracked-files=no").strip())
    if dirty:
        print("Warning: the working tree has uncommitted changes; the report only uses committed HEAD.")

    out = OUT_DIR / f"{base.version}-{head.version}"
    out.mkdir(parents=True, exist_ok=True)
    old, new = submodules(base.commit), submodules(head.commit)

    print(f"Analyzing tdesktop {base.version} -> {head.version}...")
    tdesktop = analyze(
        "tdesktop", ROOT, "", base.commit, head.commit, "HEAD", config,
        exclude=set(old) | set(new) | {".gitmodules"},
    )
    short = head.commit[:10]
    tdesktop.how = [
        "本仓库与官方没有共同的 git 历史，不能 `git merge`，按文件处理：",
        f"- 直接采用与官方新增：`git checkout {short} -- $(cat tdesktop-take.txt tdesktop-added.txt)`",
        f"- 需要合并：`git diff {base.commit[:10]} {short} -- $(cat tdesktop-merge.txt) | git apply -3`，"
        "冲突对照 `tdesktop-conflicts.txt` 解决；标注需要人工处理的文件单独处理。",
        "- 官方删除：逐个决定，跟随时 `git rm <路径>`。",
    ]
    save(out, "tdesktop", tdesktop, ROOT)
    rows = [(tdesktop, "tdesktop.md", "按文件合并")]

    unchanged, changes = [], []
    for path in sorted(old.keys() | new.keys()):
        before, after = old.get(path), new.get(path)
        if before == after:
            unchanged.append(path)
            continue
        if not before or not after:
            changes.append(f"- `{path}`：" + (f"官方新增，指向 `{after[0][:10]}`（{after[1]}）" if after else "官方已删除"))
            continue
        if before[1] != after[1]:
            changes.append(f"- `{path}`：官方地址从 {before[1]} 改为 {after[1]}")
        cwd, ours = ROOT / path, gitlink(path)
        if not (cwd / ".git").exists() or not ours:
            changes.append(f"- `{path}`：本地没有这个子模块的仓库，无法分析")
            continue
        print(f"Analyzing {path}...")
        missing = [sha for sha in (before[0], after[0]) if not has_commit(cwd, sha)]
        if missing:
            git("fetch", "--no-tags", after[1], *missing, cwd=cwd)
        finished = contains(cwd, after[0], ours)
        custom = not finished and ours != before[0]
        target = after[0][:10]
        git_sub = f"git -c safe.directory='*' -C {path}"
        fetch = f"`{git_sub} fetch {after[1]} {target}`"
        if finished:
            module = Report(path, before[0], after[0], commits(cwd, before[0], after[0]), finished=True)
            module.how = [f"本地指针 `{ours[:10]}` 已包含官方目标提交，不需要处理。"]
            state = "已包含目标"
        elif custom:
            module = analyze(path, cwd, path + "/", before[0], after[0], ours, config, exclude=set())
            state = "fork，需要合并"
            module.how = [
                f"本地 fork 指向 `{ours[:10]}`，与官方历史相通，在子模块里合并：",
                f"1. {fetch}，然后 `{git_sub} merge {target}`，冲突对照 `{path.replace('/', '-')}-conflicts.txt`。",
                f"2. 编译通过后推送到 fork 的 `{fork_branch(cwd)}` 分支，再在主仓库 `git add {path}`。",
            ]
        else:
            module = analyze(path, cwd, path + "/", before[0], after[0], before[0], config, exclude=set())
            state = "未定制，更新指针"
            module.how = [
                "本地没有定制，把子模块指针更新到目标提交即可：",
                f"{fetch}，然后 `{git_sub} checkout {target}`，再在主仓库 `git add {path}`。",
            ]
        stem = path.replace("/", "-")
        save(out, stem, module, cwd)
        rows.append((module, f"{stem}.md", state))

    local_only = sorted(set(submodules("HEAD")) - set(new))
    anchors = anchor_failures(head.commit, new.get("cmake", (None,))[0])
    flagged = []
    for item, _, _ in rows:
        for entry in item.merge + item.take + item.added + item.deleted:
            if entry.note and entry.note not in ("本地改过", "本地未改"):
                flagged.append(f"- {item.name}：`{entry.path}`：{entry.note}")
    regenerate = f"python scripts/upstream.py report --channel {args.channel} --base {base.version} --to {head.version}"
    lines = [
        f"# 官方同步报告：{base.version} → {head.version}\n",
        f"生成于 {datetime.date.today().isoformat()}，按本地已提交的 HEAD "
        f"`{git('rev-parse', '--short', 'HEAD').strip()}` 计算，工作区未提交的改动不计入。"
        f"重新生成：`{regenerate}`。\n",
    ]
    if dirty:
        lines.append("> 生成时工作区有未提交的改动，它们没有反映在报告里。\n")
    lines += [
        "| 仓库 | 状态 | 新提交 | 改动文件 | 已同步 | 需要合并 | 冲突或人工 | 详情 |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for item, link, state in rows:
        lines.append(
            f"| {item.name} | {state} | {len(item.commits)} | {item.files} | "
            f"{len(item.synced)} | {len(item.merge)} | {item.manual} | [{link}]({link}) |")
    if flagged:
        lines += ["\n## 需要留意\n", *flagged]
    deferred = config.get("deferred", {})
    if deferred:
        lines += ["\n## 暂缓事项\n", "以下是此前登记的暂缓项，补上后从 `upstream.json` 的 `deferred` 删除：\n"]
        lines += [f"- `{path}`：{reason}" for path, reason in deferred.items()]
    if changes:
        lines += ["\n## 子模块增删与地址变化\n", *changes]
    if local_only:
        lines.append(f"\n本地独有的子模块（官方没有，不参与同步）：{'、'.join(local_only)}")
    if unchanged:
        lines.append(f"\n未变化的子模块：{'、'.join(unchanged)}")
    lines.append("\n## 构建补丁锚点\n")
    if anchors:
        lines.append("以下文件里 `scripts/build_support/cmake_patch.py` 依赖的官方原文已经变化，需要更新补丁：")
        lines += [f"- `{path}`" for path in anchors]
    else:
        lines.append("`scripts/build_support/cmake_patch.py` 依赖的官方原文在新版本里都还在。")
    lines += [
        "\n## 接下来\n",
        "1. 阅读各仓库的报告，按用户决定的范围合并；每个仓库的处理方式写在它的报告开头。",
        "2. 子模块指针或依赖配方有变化时先运行 `python scripts/prebuild.py`，再编译验证。",
        f"3. 暂时不跟进的改动写进 `upstream.json` 的 `deferred`，然后登记：`python scripts/upstream.py done {head.version} --channel {args.channel}`。",
        f"4. 用 version-bump 把本应用版本改为 {head.version}，按目标发布分支选择 beta 或 stable 通道。",
    ]
    write(out, "README.md", "\n".join(lines) + "\n")
    print(f"Report saved to {out.relative_to(ROOT).as_posix()}/README.md")
    return 0


def lagging(target: Release) -> list[str]:
    """已提交的子模块指针必须等于或包含官方该版本的指针。"""
    problems = []
    for path, (sha, url) in sorted(submodules(target.commit).items()):
        ours, cwd = gitlink(path), ROOT / path
        if ours == sha:
            continue
        if not ours or not (cwd / ".git").exists():
            problems.append(f"{path}: local submodule is missing; cannot verify")
            continue
        if not has_commit(cwd, sha):
            git("fetch", "--no-tags", url, sha, cwd=cwd)
        if not contains(cwd, sha, ours):
            problems.append(f"{path}: committed revision {ours[:10]} does not contain upstream {sha[:10]}")
    return problems


def done(args: argparse.Namespace) -> int:
    config = load()
    items = fetch_releases()
    target = select_target(items, args.version, args.channel)
    current = pick(items, config["tdesktop"])
    if target.channel == "beta" and git("branch", "--show-current").strip() == "main":
        raise SystemExit("Beta upstream adaptation must be recorded on dev or its feature branch, not main.")
    if target.key <= current.key:
        print(f"Upstream {current.version} is already recorded; {target.version} is not newer.")
        return 0 if target == current else 1
    problems = lagging(target)
    if problems:
        print(f"Cannot record {target.version}; these submodules are behind upstream:")
        print("\n".join(f"  {problem}" for problem in problems))
        return 1
    config["tdesktop"] = target.version
    config["tdesktop_channel"] = target.channel
    TRACKING.write_text(
        json.dumps(config, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Recorded upstream {target.version} ({target.date}).")
    for path, reason in config.get("deferred", {}).items():
        print(f"  Still deferred: {path}: {reason}")
    print(f"Next, use version-bump to update the application version to {target.version}.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    command = sub.add_parser("check", help="查看官方是否有更新版本")
    command.set_defaults(run=check)

    command = sub.add_parser("report", help="生成基线到目标版本之间每个仓库的改动报告")
    command.add_argument("--to", help="目标版本，默认所选通道的最新版本")
    command.add_argument("--base", help="基线版本，默认 upstream.json 登记的版本")
    command.set_defaults(run=report)

    command = sub.add_parser("done", help="适配完成后登记新的官方版本")
    command.add_argument("version", help="已适配的官方版本，如 7.3.0")
    command.set_defaults(run=done)

    for command in sub.choices.values():
        command.add_argument("--channel", choices=("stable", "beta"), default="stable",
                             help="目标通道；beta 同时包含测试版与稳定版，默认 stable")

    args = parser.parse_args()
    return args.run(args)


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())
