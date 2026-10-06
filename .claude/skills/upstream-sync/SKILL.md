---
name: upstream-sync
description: "同步官方 Telegram Desktop 最新 tag，包含稳定版与测试版：检查官方新版本，生成已适配版本到目标版本之间 tdesktop 与各子模块的改动报告，适配完成后登记新的官方版本。Use when the user asks whether Telegram Desktop has a new stable or beta release, how far behind upstream the fork is, what changed upstream, wants to sync or adapt upstream, or asks to record a completed upstream sync, or says phrases like \"同步上游\", \"官方更新\", \"适配官方\", \"上游有什么变化\"."
---

# 同步官方最新 tag

`.github/upstream.json` 的 `tdesktop` 只记录已适配的官方 tag 版本号。
项目以 main 为主线；上游是否为 beta 不改变本项目的正常发版方式。
其余基准都由它推导，不需要登记：

- tdesktop 基线：官方 `v<版本>` 标签。
- 子模块基线：官方该版本记录的子模块指针。
- 本地定制：用 git diff 对比基线计算。

默认选取版本号最高的官方 `vX.Y.Z` tag，正式版和 pre-release 一同参与比较，
不使用仅返回稳定版的 Latest 接口；`--to` 仍可固定目标。脚本按官方地址拉取标签，
保存在 `refs/upstream-tags/`，不依赖远程名，也不会和本仓库同名的发布标签混在一起。
`upstream.json` 另有三类规则：`skip` 长期不跟进，`notes` 跟进时要注意，`deferred` 暂缓、以后要补。

## 同步范围

只取官方 tag，不追踪 tag 之间的开发分支提交。上游正式版与测试版标签都可作为基线，
完成适配与验证后按本项目原有 main 发版流程发布，不因上游 beta 增加版本后缀。

## 检查与报告

```bash
python scripts/upstream.py check                          # 官方有没有更新的 tag，包含测试版
python scripts/upstream.py report                         # 从已登记版本到官方最新 tag
python scripts/upstream.py report --to 7.2.10             # 固定目标，正式版和测试版均可
python scripts/upstream.py report --base 7.2.7 --to 7.2.9 # 指定基线，用于演练或复查
```

报告按**已提交的 HEAD** 计算，工作区未提交的改动不计入；同步过程中分批提交后重新生成，
“已与目标一致”会随之增加，可以当作进度。报告保存在 `build/upstream-sync/<基线>-<目标>/`：

- `README.md`：各仓库状态与数量、需要留意的文件、暂缓事项、子模块增删与地址变化、构建补丁锚点。
- `<仓库>.md`：开头是该仓库的处理方式和可直接执行的命令，然后是文件分类和官方提交。
  tdesktop 叫 `tdesktop.md`，子模块把路径里的 `/` 换成 `-`，如 `Telegram-lib_ui.md`。
- `<仓库>-take.txt`、`-merge.txt`、`-added.txt`、`-deleted.txt`：纯路径清单，供批量命令使用。
- `<仓库>.diff`：官方完整改动；`<仓库>-conflicts.txt`：三方合并预演的冲突片段。

| 类别 | 含义 |
|---|---|
| 需要合并 | 本地改过，已预演三方合并并标出冲突块数；“需要人工处理”的单独看说明 |
| 直接采用 | 本地没改过，可以直接检出官方版本 |
| 官方新增 / 官方删除 | 逐个决定是否跟随，删除项注明本地是否改过 |
| 已与目标一致 | 本地内容已经和目标版本相同，不用处理 |
| 按 skip 跳过 | 命中 `skip`，附原因 |

## 适配

报告生成后先交给用户阅读，按用户决定的范围合并；生成报告本身不改动代码。
从最新 main 创建临时 `feature/upstream-<版本>` 分支进行，PR 目标为 main。

- **tdesktop**：本仓库与官方没有共同的 git 历史，不能 `git merge`，按报告开头的命令逐类处理：
  直接采用和官方新增用 `git checkout`，需要合并的用 `git diff <基线> <目标> | git apply -3`。
  标注“可能已按职责拆分”的文件，要把官方改动手动分发到本地拆分后的文件；
  标注了 `notes` 的文件按说明处理，例如 `prepare.py` 的改动要同步到 `scripts/build_support/recipes.py`。
- **fork 子模块**：与官方历史相通，在子模块里 `git merge` 官方目标提交，编译通过后推送到 fork，
  再在主仓库 `git add <子模块路径>`。推送前向用户确认。

  | 子模块 | fork 分支 |
  |---|---|
  | `Telegram/lib_ui` | `master-ui` |
  | `Telegram/lib_tl` | `master-tl` |
  | `Telegram/codegen` | `master-codegen` |

- **未定制的子模块**：在子模块里拉取并检出目标提交，再在主仓库 `git add`。
- **cmake**：官方子模块，定制由 `scripts/build_support/cmake_patch.py` 构建时打补丁；报告提示锚点失效时更新补丁。
- `Telegram/lib_icu` 是本项目独有的子模块，官方没有，不参与同步。

本机子模块没有登记在 `.git/config`，`git submodule update` 用不了；子模块目录的所有者也和当前用户不同。
手动操作子模块时每条命令都加 `-c safe.directory='*'`，不要改全局 git 配置，报告里给出的命令已经带上。

子模块指针或依赖配方有变化时，先运行 `python scripts/prebuild.py`，再编译验证。
暂时不跟进的改动写进 `deferred`（路径与原因），之后每次报告都会列出，补上后删除。

## 登记

适配完成且编译验证通过后：

```bash
python scripts/upstream.py done <已适配的官方版本>
```

`done` 会检查每个子模块已提交的指针等于或包含官方该版本的指针，不满足时拒绝登记；只能往更新的版本登记。
随后用 version-bump 准备 `<官方版本>[.<修订号>]`，按原有 main 发布流程处理。
更新说明写明所适配的 Telegram Desktop 版本；不要把上游 beta 标记复制成我们的版本后缀或发布通道。

## 提交

按 commit skill 逐个路径暂存，标题用英文。建议拆成：tdesktop 的适配（改动多时按领域拆开）、
每个子模块指针的更新、`done` 登记、版本发布，各自单独提交。
