---
name: worktree
description: 在 build/worktrees/ 下用 git worktree 隔离推进任务，用记录文件交接完成状态，并在主仓库合并验证。用户指定使用 worktree、主仓库已有其他助手或成员在工作、或要求在不影响他人的前提下推进时，按分支作业流程执行；在主仓库工作时，用户提到有人或其他助手在编写其他功能、或要求合入 worktree 成果时，按主仓库合并流程检查记录并合并。
---

# worktree 并行开发

同一技能分两种角色：

| 角色 | 工作位置 | 负责 | 不做 |
|---|---|---|---|
| 分支作业 | `build/worktrees/<名称>/` | 写代码、`check_worktree.py` 检查、分支上提交、维护记录 | 完整编译、运行、碰主仓库工作区和其他 worktree |
| 主仓库合并 | 主仓库根目录 | 读记录、合并已完成的分支、完整编译和运行验证、移除 worktree | 合并未完成的任务 |

## 约定

| 项 | 规则 |
|---|---|
| 名称 | 小写字母、数字、连字符，例如 `theme-editor-narrowing`；目录、记录、分支共用 |
| 目录 | `build/worktrees/<名称>/`，只用 `git worktree add` 创建、`git worktree remove` 移除 |
| 记录 | `build/worktrees/<名称>.md`，放在目录外，移除 worktree 后保留 |
| 分支 | `feature/<名称>` 或 `fix/<名称>` |
| 基线 | 默认本地 `main`；用户要求基于远程时先 `git fetch origin`，改用 `origin/main` |

`build/` 已被忽略，目录和记录都不进提交。worktree 只检出源码，不初始化子模块，也没有自己的构建目录。

## 分支作业

### 创建或接手

1. 在主仓库根目录确认名称未被占用：`git worktree list`、`git branch --list <分支>`，记录文件不存在。
2. `git worktree add -b <分支> build/worktrees/<名称> <基线>`。
3. 立即按模板写记录，状态“进行中”。
4. 之后的命令都在 worktree 里执行，不再改动主仓库工作区。

接手已有任务时先阅读记录，核对分支、提交和 `git status`，从“下一步”继续。

### 编写与检查

- 任务需要改 `lib_ui` 等子模块时停下，记录标“受阻”并写明原因，交给主仓库处理。
- 不运行 `build.py`、`prebuild.py` 和 app-debug：构建缓存、调试端口 20100、产物目录都只有一份，在主仓库。
- 提交前运行检查，失败就修，直到通过：

  ```bash
  python .claude/skills/worktree/scripts/check_worktree.py <名称>
  ```

  脚本借用主仓库最近一次构建的编译命令，对改动做 `/Zs` 单文件编译检查，并检查 CMake 登记、2000 行上限、`git diff --check` 和新文件是否被忽略。改了头文件会反查引用它的源文件，超过 `--max-files` 时抽查。最后输出的 Markdown 摘要原样贴进记录。
- 脚本查不出：链接错误、只在 Qt 6 平台（Windows ARM64、macOS）出现的错误、`.style` / 翻译 / `.tl` 改动后的生成代码。这些写进记录的“未验证项”。
- 脚本报主仓库没有编译日志时，记录标“受阻”，请用户先在主仓库构建一次，不在 worktree 里构建。
- 其余审查清单项（目录位置、`_DEBUG` 包裹、注释、卫语句、rpl 生命周期）人工自查。
- 按 commit 技能在分支上提交，默认不推送。
- stash 栈所有 worktree 共用，不用不带标签的 `git stash`；临时搁置用 WIP 提交。

### 完成

全部改动已提交、`git status` 干净、检查通过后，记录改为“已完成”，填写提交列表、检查摘要和未验证项。worktree 保留，等主仓库合并后移除。
做不下去时标“受阻”或“已终止”并写明原因，不写成已完成。

## 主仓库合并

1. `git worktree list` 并阅读 `build/worktrees/*.md`，向用户列出各任务的状态、分支和提交。
2. 只合并“已完成”的任务；其余状态告知用户，不碰对应目录。
3. 核对记录与现场一致：记录里最后一个提交等于 `git rev-parse <分支>`，`git -C build/worktrees/<名称> status --short` 为空。不一致时停下说明。
4. 主仓库有未提交改动时先告知用户，不擅自暂存。
5. `git merge --no-ff <分支>`，合并提交按 commit 技能编写，例如 `feat: Merge <功能> from <分支>`。冲突逐个解决，拿不准时询问用户。
6. `python scripts/build.py --dev` 完整编译（用户指定过 `--jobs` 时沿用）。失败在主仓库修复并单独提交，不回 worktree 改。
7. 按记录里的“未验证项”和功能性质，用 app-debug 做运行验证。
8. 在记录中写明合并提交、编译和验证结果，状态改为“已合并”。
9. 在主仓库根目录执行 `git worktree remove build/worktrees/<名称>`（不加 `--force`），再 `git branch -d <分支>`。当前 shell 不能停在要移除的目录里，否则 Windows 会拒绝删除。

推送照常：先说明内容，等用户确认。

## 记录模板

最多 100 行，只保留当前有效内容。

```markdown
# <名称>

- 任务目标：
- 状态：进行中 / 受阻 / 已完成 / 已终止 / 已合并
- 更新时间：YYYY-MM-DD HH:MM
- 分支与基线：feature/<名称>，基于 main@<提交号>
- 提交：<提交号> <标题>（一行一个，按时间顺序）
- 未提交改动：无
- 下一步：

## 改动范围
- `<路径>`：一句话说明

## 检查结果
（粘贴 check_worktree.py 输出的摘要）

## 未验证项
- 需要主仓库完整编译或运行后确认的行为

## 合并结果
- 合并提交、编译、运行验证；未合并写“无”
```
