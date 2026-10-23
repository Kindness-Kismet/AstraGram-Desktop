"""Check a worktree's changes with the main checkout's compile commands, without building."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path, PureWindowsPath
import re
import shutil
import subprocess
import sys
import tempfile


LINE_LIMIT = 2000
SOURCE_ROOT = "Telegram/SourceFiles/"
# 原样引入的第三方源码不参与行数与登记检查
VENDORED_ROOTS = ("Telegram/SourceFiles/extras/libs/",)
SOURCE_SUFFIXES = {".cpp", ".c", ".cc", ".cxx"}
HEADER_SUFFIXES = {".h", ".hpp", ".hxx"}
REGISTERED_SUFFIXES = SOURCE_SUFFIXES | HEADER_SUFFIXES | {".mm"}
# 这些输入经 codegen 生成头文件，主仓库的生成结果是旧的，只能交给完整编译
GENERATED_SUFFIXES = {".style", ".palette", ".strings", ".tl", ".qrc"}
PLATFORM_ONLY = re.compile(r"(?i)/platform/(?:linux|mac)/|_(?:linux|mac|macos)\.[a-z]+$|\.mm$")
# 去掉产物、预编译头与调试信息参数；/FI 保留，用来代替预编译头
DROPPED_FLAGS = ("/Fo", "/Fd", "/Fp", "/Fa", "/FR", "/Fr", "/Yu", "/Yc", "/Gm", "/Zi", "/ZI", "/Z7", "/MP", "/FS", "/errorReport")
TOKEN = re.compile(r'(?:[^\s"]+|"(?:\\.|[^"\\])*")+')
DIAGNOSTIC = re.compile(r"\b(?:fatal )?error [A-Z]+\d+\b|\bwarning [A-Z]+\d+\b")
DEFAULT_JOBS = 4
DEFAULT_MAX_FILES = 40
MAX_DIAGNOSTICS = 12


@dataclass(frozen=True)
class Change:
    path: str
    status: str


@dataclass
class Classified:
    sources: list[str] = field(default_factory=list)
    headers: list[str] = field(default_factory=list)
    added: list[str] = field(default_factory=list)
    removed: list[str] = field(default_factory=list)
    generated: list[str] = field(default_factory=list)
    submodules: list[str] = field(default_factory=list)


@dataclass
class CompileDatabase:
    config: str
    # 键为主仓库文件的大写反斜杠绝对路径，与 tlog 写法一致
    commands: dict[str, tuple[str, Path]] = field(default_factory=dict)
    readers: dict[str, set[str]] = field(default_factory=dict)
    pch_headers: set[str] = field(default_factory=set)


@dataclass(frozen=True)
class Target:
    path: str
    command_key: str
    reason: str


@dataclass
class CompileResult:
    target: Target
    ok: bool
    diagnostics: list[str]


@dataclass
class Report:
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)
    summary: list[str] = field(default_factory=list)


def fail(message):
    print(f"error: {message}", file=sys.stderr)
    sys.exit(2)


def git(cwd, *args, check=True):
    result = subprocess.run(
        ["git", "--no-optional-locks", "-C", str(cwd), *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if check and result.returncode != 0:
        fail(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def tlog_key(path):
    return str(PureWindowsPath(path)).upper()


def locate(target):
    """返回主仓库与待检查 worktree 的根目录；target 可以是名称或路径。"""
    common = git(Path.cwd(), "rev-parse", "--path-format=absolute", "--git-common-dir").strip()
    main_root = Path(common).parent.resolve()
    if target is None:
        return main_root, Path(git(Path.cwd(), "rev-parse", "--show-toplevel").strip()).resolve()
    candidate = Path(target)
    if not candidate.is_dir():
        candidate = main_root / "build" / "worktrees" / target
    if not candidate.is_dir():
        fail(f"worktree not found: {target}")
    return main_root, Path(git(candidate, "rev-parse", "--show-toplevel").strip()).resolve()


def parse_tlog(text):
    """解析 MSBuild tlog：'^' 行列出源文件，批量编译时用 '|' 分隔，其后各行属于该组。"""
    entries = {}
    current = None
    for line in text.splitlines():
        if line.startswith("^"):
            current = []
            for source in line[1:].split("|"):
                entries[source] = current
            continue
        if current is not None and line:
            current.append(line)
    return entries


def load_database(main_root, config):
    out = main_root / "build" / "tmp" / "out" / "Telegram"
    configs = [config] if config else ["Debug", "Release"]
    found = {}
    for name in configs:
        logs = sorted(out.glob(f"*.dir/{name}/*.tlog/CL.command.*.tlog"))
        if logs:
            found[name] = max(log.stat().st_mtime for log in logs)
    if not found:
        wanted = config or "Debug or Release"
        fail(f"no compile logs ({wanted}) under {out}; build the main checkout once with scripts/build.py")
    # 未指定配置时取最近构建过的那套
    chosen = max(found, key=found.get)
    database = CompileDatabase(config=chosen)
    for tlog_dir in sorted(out.glob(f"*.dir/{chosen}/*.tlog")):
        project_dir = tlog_dir.parents[2]
        for log in sorted(tlog_dir.glob("CL.command.*.tlog")):
            for source, lines in parse_tlog(log.read_text(encoding="utf-16")).items():
                database.commands[source] = (" ".join(lines), project_dir)
        for log in sorted(tlog_dir.glob("CL.read.*.tlog")):
            for source, lines in parse_tlog(log.read_text(encoding="utf-16")).items():
                if source.endswith("CMAKE_PCH.CXX"):
                    database.pch_headers.update(lines)
                    continue
                for header in lines:
                    database.readers.setdefault(header, set()).add(source)
    return database


def submodule_paths(worktree):
    output = git(worktree, "config", "-f", ".gitmodules", "--get-regexp", r"^submodule\..*\.path$", check=False)
    return sorted(line.split(" ", 1)[1].strip() for line in output.splitlines() if " " in line)


def collect_changes(worktree, base):
    merge_base = git(worktree, "merge-base", base, "HEAD").strip()
    parts = git(worktree, "diff", "--name-status", "--no-renames", "-z", merge_base).split("\0")
    changes = [Change(parts[i + 1], parts[i][0]) for i in range(0, len(parts) - 1, 2)]
    untracked = git(worktree, "ls-files", "--others", "--exclude-standard", "-z")
    changes += [Change(path, "A") for path in untracked.split("\0") if path]
    return merge_base, changes


def under(path, root):
    return path == root or path.startswith(root.rstrip("/") + "/")


def classify(changes, submodules):
    result = Classified()
    for change in changes:
        path = change.path
        suffix = Path(path).suffix.lower()
        if any(under(path, sub) for sub in submodules):
            result.submodules.append(path)
            continue
        if suffix in GENERATED_SUFFIXES:
            result.generated.append(path)
            continue
        if not path.startswith(SOURCE_ROOT) or path.startswith(VENDORED_ROOTS):
            continue
        if change.status == "D":
            if suffix in REGISTERED_SUFFIXES:
                result.removed.append(path)
            continue
        if change.status == "A" and suffix in REGISTERED_SUFFIXES:
            result.added.append(path)
        if suffix in SOURCE_SUFFIXES:
            result.sources.append(path)
        elif suffix in HEADER_SUFFIXES:
            result.headers.append(path)
    return result


class PathMapper:
    """把命令里指向主仓库源码的路径换成 worktree；构建目录与子模块仍用主仓库。"""

    def __init__(self, main_root, worktree, submodules, cases=None):
        parts = PureWindowsPath(main_root).parts
        root = re.escape(parts[0].rstrip("\\/")) + "".join(r"[\\/]+" + re.escape(part) for part in parts[1:])
        self.pattern = re.compile(root + r'(?P<rest>[\\/][^"\s]*)?(?![^\\/"\s])', re.IGNORECASE)
        self.worktree = str(PureWindowsPath(worktree))
        self.kept = ["build"] + [sub.lower() for sub in submodules]
        # tlog 里的路径全是大写，按仓库实际大小写还原，诊断信息更好读
        self.cases = cases or {}

    def keeps(self, relative):
        relative = relative.lower()
        return any(under(relative, root) for root in self.kept)

    def map(self, text):
        def replace(match):
            rest = match.group("rest") or ""
            relative = rest.replace("\\", "/").strip("/")
            if relative and self.keeps(relative):
                return match.group(0)
            real = self.cases.get(relative.upper())
            return self.worktree + ("\\" + real.replace("/", "\\") if real else rest)
        return self.pattern.sub(replace, text)


def rewrite_command(command, mapper, stripped_sources):
    args = []
    for token in TOKEN.findall(command):
        if token.startswith(DROPPED_FLAGS):
            continue
        if token.strip('"').upper() in stripped_sources:
            continue
        args.append(mapper.map(token))
    return args


def case_map(paths):
    """仓库相对路径及其各级目录的大写形式到实际写法的映射。"""
    cases = {}
    for path in paths:
        parts = path.split("/")
        for depth in range(1, len(parts) + 1):
            real = "/".join(parts[:depth])
            cases[real.upper()] = real
    return cases


def remap_forced_includes(args, mapper, directory):
    """CMake 的预编译头包装文件写死了主仓库 stdafx.h，换成指向 worktree 的副本。"""
    for index, arg in enumerate(args):
        if not arg.startswith("/FI"):
            continue
        path = Path(arg[3:].strip('"'))
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        mapped = mapper.map(text)
        if mapped == text:
            continue
        copy = Path(directory) / f"forced_{index}{path.suffix}"
        copy.write_text(mapped, encoding="utf-8")
        args[index] = f'/FI"{copy}"'
    return args


def tidy(line, prefix):
    if not line.lower().startswith(prefix.lower()):
        return line
    location, separator, message = line[len(prefix):].partition(": ")
    return location.replace("\\", "/") + separator + message


def sample(items, limit):
    """均匀抽取，保证结果稳定且覆盖不同目录。"""
    items = sorted(items)
    if len(items) <= limit:
        return items
    step = len(items) / limit
    return [items[int(index * step)] for index in range(limit)]


def select_targets(classified, database, main_root, tracked, max_files, report):
    targets = {}
    changed = set(classified.sources)
    in_source_root = tlog_key(main_root / SOURCE_ROOT) + "\\"
    for path in classified.sources:
        key = tlog_key(main_root / path)
        if key in database.commands:
            targets[path] = Target(path, key, "changed")
            continue
        if path not in classified.added or PLATFORM_ONLY.search(path):
            report.notes.append(f"{path}: not part of this Windows build, not compiled")
            continue
        # 新文件借用同目录文件的命令，没有同目录文件时退回任一业务源文件
        folder = tlog_key(main_root / Path(path).parent) + "\\"
        donors = sorted(k for k in database.commands if k.startswith(folder)) or sorted(
            k for k in database.commands if k.startswith(in_source_root)
        )
        if not donors:
            report.notes.append(f"{path}: no compile command to borrow, not compiled")
            continue
        targets[path] = Target(path, donors[0], "new file")

    dependents = {}
    for header in classified.headers:
        key = tlog_key(main_root / header)
        if key in database.pch_headers:
            report.notes.append(f"{header}: reached through the precompiled header, affects every source")
            readers = {k for k in database.commands if k.startswith(in_source_root)}
        else:
            readers = database.readers.get(key, set())
        for reader in readers:
            path = tracked.get(reader)
            if path and path not in changed:
                dependents.setdefault(path, header)
    picked = sample(dependents, max_files)
    if len(picked) < len(dependents):
        report.notes.append(f"sampled {len(picked)} of {len(dependents)} sources that include changed headers")
    for path in picked:
        key = tlog_key(main_root / path)
        if key in database.commands:
            targets[path] = Target(path, key, f"includes {dependents[path]}")
    return sorted(targets.values(), key=lambda target: target.path)


def compile_target(target, database, mapper, worktree, compiler, environment):
    command, project_dir = database.commands[target.command_key]
    file = PureWindowsPath(worktree / target.path)
    args = rewrite_command(command, mapper, {target.command_key}) + ["/Zs", f'"{file}"']
    with tempfile.TemporaryDirectory(prefix="check-worktree-") as directory:
        args = remap_forced_includes(args, mapper, directory)
        response = Path(directory) / "args.rsp"
        # 选项与参数可能是分开的两个词，必须写在同一行
        response.write_text(" ".join(args), encoding="utf-16")
        result = subprocess.run(
            [compiler, f"@{response}"],
            cwd=project_dir,
            env=environment,
            capture_output=True,
            # cl 按控制台 OEM 代码页输出，不受 Python UTF-8 模式影响
            encoding="oem",
            errors="replace",
        )
    output = (result.stdout + result.stderr).splitlines()
    prefix = str(PureWindowsPath(worktree)) + "\\"
    diagnostics = [tidy(line, prefix) for line in output if DIAGNOSTIC.search(line)]
    if result.returncode != 0 and not diagnostics:
        diagnostics = output[-MAX_DIAGNOSTICS:]
    return CompileResult(target, result.returncode == 0, diagnostics)


def compiler_environment(main_root):
    sys.path.insert(0, str(main_root / "scripts"))
    from build_support.toolchain import msvc_environment

    environment = msvc_environment()
    # 装了英文语言包时输出英文诊断，没装时保持中文
    environment["VSLANG"] = "1033"
    compiler = shutil.which("cl", path=environment.get("PATH"))
    if not compiler:
        fail("cl.exe not found in the MSVC environment")
    return compiler, environment


def cmake_lists(worktree):
    files = [worktree / "Telegram" / "CMakeLists.txt", *sorted((worktree / "Telegram" / "cmake").rglob("*.cmake"))]
    return "\n".join(file.read_text(encoding="utf-8", errors="replace") for file in files if file.is_file())


def registered(text, path):
    relative = path[len(SOURCE_ROOT):]
    return re.search(r"(?<![\w/.-])" + re.escape(relative) + r"(?![\w/.-])", text) is not None


def registration_problems(classified, text):
    problems = [f"{path}: not listed in the CMake source lists" for path in classified.added if not registered(text, path)]
    problems += [f"{path}: deleted but still listed in the CMake source lists" for path in classified.removed if registered(text, path)]
    return problems


def count_lines(text):
    return len(text.splitlines())


def line_limit_verdict(path, now, before):
    """返回 (级别, 说明)；新增越限或首次越限算错误，原本就超限的只提醒。"""
    if now <= LINE_LIMIT:
        return None
    if before is None:
        return "error", f"{path}: {now} lines, new file over the {LINE_LIMIT}-line limit"
    if before <= LINE_LIMIT:
        return "error", f"{path}: {now} lines, crossed the {LINE_LIMIT}-line limit (was {before})"
    return "warning", f"{path}: {now} lines, already over the limit at base (was {before})"


def check_line_limits(worktree, merge_base, classified, report):
    for path in classified.sources:
        if Path(path).suffix.lower() != ".cpp":
            continue
        now = count_lines((worktree / path).read_text(encoding="utf-8", errors="replace"))
        before = None
        if path not in classified.added:
            before = count_lines(git(worktree, "show", f"{merge_base}:{path}"))
        verdict = line_limit_verdict(path, now, before)
        if verdict:
            level, message = verdict
            (report.errors if level == "error" else report.warnings).append(message)


def check_whitespace(worktree, merge_base, report):
    output = git(worktree, "diff", "--check", merge_base, check=False).strip()
    lines = [line for line in output.splitlines() if not line.startswith(("+", "-"))]
    report.errors += [f"whitespace: {line}" for line in lines]
    return len(lines)


def check_ignored(worktree, report):
    output = git(worktree, "ls-files", "--others", "--ignored", "--exclude-standard", "-z", "--", SOURCE_ROOT)
    paths = [path for path in output.split("\0") if path]
    report.errors += [f"{path}: ignored by .gitignore, would not be committed" for path in paths]
    return len(paths)


def run(args):
    main_root, worktree = locate(args.worktree)
    merge_base, changes = collect_changes(worktree, args.base)
    submodules = submodule_paths(worktree)
    classified = classify(changes, submodules)
    report = Report()
    print(f"Worktree: {worktree}")
    print(f"Base:     {args.base} @ {merge_base[:10]}  ({len(changes)} changed paths)")

    if not changes:
        print("Nothing to check.")
        return 0

    tracked_paths = git(worktree, "ls-files", "-z").split("\0")
    tracked = {tlog_key(main_root / path): path for path in tracked_paths if path}
    tracked.update({tlog_key(main_root / path): path for path in classified.added})
    for path in classified.removed:
        tracked.pop(tlog_key(main_root / path), None)

    targets = []
    if classified.sources or classified.headers:
        database = load_database(main_root, args.config)
        print(f"Commands: {database.config} compile logs, {len(database.commands)} sources")
        targets = select_targets(classified, database, main_root, tracked, args.max_files, report)

    if args.dry_run:
        for target in targets:
            print(f"[plan] {target.path}  ({target.reason})")
    elif targets:
        compiler, environment = compiler_environment(main_root)
        mapper = PathMapper(main_root, worktree, submodules, case_map(tracked.values()))
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(
                lambda target: compile_target(target, database, mapper, worktree, compiler, environment),
                targets,
            ))
        for result in results:
            state = "PASS" if result.ok else "FAIL"
            print(f"[compile] {state}  {result.target.path}  ({result.target.reason})")
            for line in result.diagnostics[:MAX_DIAGNOSTICS]:
                print(f"    {line}")
            if not result.ok:
                report.errors.append(f"compile: {result.target.path}")
        failed = sum(not result.ok for result in results)
        state = "fail" if failed else "pass"
        report.summary.append(f"- [{state}] Compile check (/Zs, {len(results)} files, {failed} failed)")

    registration = registration_problems(classified, cmake_lists(worktree))
    report.errors += registration
    report.summary.append(f"- [{'fail' if registration else 'pass'}] CMake registration ({len(classified.added)} added, {len(classified.removed)} removed)")

    before = len(report.errors)
    check_line_limits(worktree, merge_base, classified, report)
    report.summary.append(f"- [{'fail' if len(report.errors) > before else 'pass'}] Line limit ({LINE_LIMIT})")
    whitespace = check_whitespace(worktree, merge_base, report)
    report.summary.append(f"- [{'fail' if whitespace else 'pass'}] git diff --check")
    ignored = check_ignored(worktree, report)
    report.summary.append(f"- [{'fail' if ignored else 'pass'}] New sources tracked by git")

    if classified.generated:
        report.summary.append(f"- [not run] Generated inputs: {', '.join(classified.generated)}")
    if classified.submodules:
        report.summary.append(f"- [not run] Submodule changes: {', '.join(classified.submodules)}")
    report.summary.append("- [not run] Full build, link and runtime: done when merging in the main checkout")

    for title, lines in (("Errors", report.errors), ("Warnings", report.warnings), ("Notes", report.notes)):
        if lines:
            print(f"\n{title}:")
            for line in lines:
                print(f"  {line}")
    stamp = datetime.now().strftime("%Y-%m-%d %H:%M")
    print(f"\n## Check results ({stamp}, base {merge_base[:10]})")
    print("\n".join(report.summary))
    return 1 if report.errors else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worktree", nargs="?", help="name under build/worktrees/, or a path; defaults to the current checkout")
    parser.add_argument("--base", default="main", help="branch the work started from (default: main)")
    parser.add_argument("--config", choices=["Debug", "Release"], help="compile logs to borrow; defaults to the latest built")
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS, help=f"parallel compiler processes (default: {DEFAULT_JOBS})")
    parser.add_argument("--max-files", type=int, default=DEFAULT_MAX_FILES, help="cap on sources checked through changed headers")
    parser.add_argument("--dry-run", action="store_true", help="list the files that would be compiled")
    args = parser.parse_args()
    if args.jobs < 1 or args.max_files < 1:
        parser.error("--jobs and --max-files must be positive")
    sys.exit(run(args))


if __name__ == "__main__":
    main()
