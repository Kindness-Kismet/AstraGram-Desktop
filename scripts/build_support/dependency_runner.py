import subprocess
from pathlib import Path

from build_support.console import fail, paint, warn, DIM
from build_support.dependency_cache import (
    check_cache_key,
    clear_cache_key,
    compute_cache_key,
    ensure_key_directories,
    prune_obsolete,
    stage_directory,
    write_cache_key,
)
from build_support.dependency_env import build_environment
from build_support.recipe import Stage, remove_dir_command, win_fail_on_each

_COMMAND_FILE = "command.bat"


def run_stages(stages: list[Stage], base_environment: dict[str, str], only: list[str], verbose: bool) -> int:
    """按序执行阶段，返回实际构建的数量。"""
    if only:
        known = {stage.name for stage in stages}
        for name in only:
            if name not in known:
                raise SystemExit(f"Unknown stage: {name}. Use --list to see available stages.")

    ensure_key_directories()
    for path in prune_obsolete(stages):
        print(f"  removed obsolete {path}", flush=True)
    environment = build_environment(base_environment)
    selected = [stage for stage in stages if not only or stage.name in only]
    total = len(selected)
    built = 0

    for index, stage in enumerate(selected, start=1):
        version = f"#{stage.version}" if stage.version != "0" else ""
        prefix = f"[{index}/{total}]({stage.location}/{stage.name}{version})"

        key = compute_cache_key(stage)
        # 显式指定阶段时强制重建，否则按缓存键判断
        state = "Forced" if only else check_cache_key(stage, key)
        if state == "Good":
            print(f"  {prefix}: " + paint("SKIPPING", DIM), flush=True)
            continue

        reason = {"NotFound": "not found", "Stale": "changed", "Forced": "forced"}[state]
        print(f"  {prefix}: building ({reason})", flush=True)

        commands = remove_dir_command(stage.name) + "\n" + stage.commands
        if verbose:
            _print_commands(commands)

        clear_cache_key(stage)
        directory = stage_directory(stage)
        directory.mkdir(parents=True, exist_ok=True)
        if not _execute(commands, directory, environment):
            raise SystemExit(fail(f"{prefix}: FAILED"))
        write_cache_key(stage, key)
        built += 1

    return built


def _execute(commands: str, cwd: Path, environment: dict[str, str]) -> bool:
    # 配方是 cmd 语法，写成 bat 执行；逐条插入错误检查以便失败即停
    script = cwd / _COMMAND_FILE
    if script.exists():
        script.unlink()
    script.write_text(
        '@echo OFF\r\nset "NoDefaultCurrentDirectoryInExePath="\r\n' + win_fail_on_each(commands),
        encoding="utf-8",
    )
    completed = subprocess.run(str(script), shell=True, cwd=str(cwd), env=environment)
    if completed.returncode == 0 and script.exists():
        script.unlink()
    return completed.returncode == 0


def _print_commands(commands: str) -> None:
    print(paint("  ---------------- COMMANDS ----------------", DIM), flush=True)
    for line in commands.rstrip("\n").split("\n"):
        print(paint(f"  {line}", DIM), flush=True)
    print(paint("  ------------------------------------------", DIM), flush=True)
