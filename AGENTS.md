# AstraGram 项目约定

## 项目结构与架构

AstraGram 是 Telegram Desktop 的 fork，前身为 AyuGram Desktop，在上游能力上增加隐身模式、消息留档、正则过滤、文本处理和界面定制。采用 C++20、Qt、rpl、CMake 与 nlohmann::json，保留现有模块和按职责拆分的源码布局。

| 位置 | 职责与边界 |
|---|---|
| `Telegram/SourceFiles/main`、`core`、`platform`、`_other` | 账号与会话生命周期、应用入口、平台适配、打包与更新 |
| `mtproto`、`api`、`data`、`storage`、`tde2e` | 协议、请求封装、内存数据、本地持久化与端到端加密；UI 和会话数据由主线程持有 |
| `window`、`history`、`dialogs`、`chat_helpers` | 窗口与导航、消息列表、会话列表及输入辅助；`HistoryItem` 数据与消息视图分离 |
| `media`、`editor`、`iv`、`overview`、`layout` | 媒体播放与查看、编辑、即时预览和媒体布局 |
| `boxes`、`info`、`settings`、`calls` 等领域目录 | 通用弹窗、资料、官方设置、通话及各自业务 |
| `ui`、`lang`、`tests` | 应用层控件、语言消费和已有测试；基础控件位于 `Telegram/lib_ui` |
| `Telegram/SourceFiles/extras/features/<功能>` | 定制业务功能；界面放 `extras/ui`，持久化放 `extras/data`，转换与共用工具放 `extras/utils` |
| `Telegram/SourceFiles/extras/debug` | Debug 调试服务和模拟数据；命令按领域放 `commands`，仅在 `_DEBUG` 编译 |
| `Telegram/CMakeLists.txt` | 逐文件登记主应用源码，定制文件登记到 `extras_files`；拆分与新增文件同步维护此清单 |
| `scripts`、`scripts/build_support` | 准备与构建入口、共享工具链、缓存、产物及工程检查；不反向依赖技能执行脚本 |
| `.github/scripts`、`.github/workflows` | 云端构建、打包和发布编排 |

上表未写前缀的源码目录均位于 `Telegram/SourceFiles/`。新增功能沿现有状态持有方和依赖方向实现，不为目录形式迁移架构。

- `lib_ui`、`lib_tl`、`codegen` 是 fork 子模块；其余 `lib_*` 通常作为只读依赖。`cmake` 使用官方子模块，定制由构建补丁维护。
- `extras/extras_ui_settings.h` 由 `Telegram/lib_ui/extras/` 提供，主应用和生成样式共同使用；保留样式生成器固定的包含路径。
- UI、`Data::Session`、`History`、`PeerData` 由主线程持有；工作线程经 `extras/utils/telegram_helpers.h` 的 `dispatchToMainThread` 访问，不直接调用 `ExtrasSettings::getInstance()` 或 `Core::App()`。
- 调试服务在主线程创建 `QTcpServer`，监听 `127.0.0.1:20100`，同步处理命令；服务和模拟模式受 `_DEBUG` 编译边界约束。
- 应用实例身份按当前用户与 Debug / Release 类型区分，同一身份单实例；普通重复启动转交给已有实例。数据目录另有占用保护，具体实现见 `core/sandbox.cpp`。
- 主窗口支持独立聊天等子窗口，关闭与常驻遵循托盘、`workMode`、`closeBehavior` 和平台策略；窗口位置、尺寸和最大化状态沿用 `Core::Settings` 的保存恢复。
- 并行任务明确各自编辑范围，共享文件、Git 索引、构建产物和调试实例由单一负责人协调。

## 配置来源

| 来源 | 管理内容 |
|---|---|
| [应用配置](Telegram/SourceFiles/config.h)、[构建路径](scripts/build_support/paths.py) | 应用身份、稳定产物名、目标架构、输出路径和共享路径定位 |
| [版本源](Telegram/build/version)、[更新日志](.github/CHANGELOG.md) | 应用唯一发布版本与公开变更说明 |
| [更新信任根](Telegram/Resources/update)、[签名管理](scripts/update_signing.py)、[更新打包](scripts/package_update.py) | V2 公钥与根签名清单；私钥在打包时从外部读取，CI 使用 `UPDATE_SIGNING_KEY` |
| [工具链](scripts/build_support/toolchain.py)、[依赖配方](scripts/build_support/recipes.py) | MSVC/CMake 工具链、Qt 等依赖版本及缓存输入 |
| [产品构建](scripts/build_support/builder.py)、[CMake 清单](Telegram/CMakeLists.txt) | 编译参数、并发、生成器、源码与产物消费者 |
| [源码检查](scripts/build_support/source_policy.py) | 自有代码范围、第三方及生成物边界、文件行数检查；风格规则由 code-style 维护 |
| [官方设置](Telegram/SourceFiles/core/core_settings.h)、[定制设置](Telegram/SourceFiles/extras/extras_settings.h) | 状态模型与序列化，定制设置界面位于 `extras/ui/settings/` |
| [主窗口](Telegram/SourceFiles/window/main_window.cpp)、[界面基础库](Telegram/lib_ui/ui) | 窗口规格、状态持久化、标题栏与基础控件；主题样式由各模块 `.style` 和基础库维护 |
| [启动与运行目录](Telegram/SourceFiles/core/launcher.cpp)、[实例管理](Telegram/SourceFiles/core/sandbox.cpp) | 工作目录、`tdata`、日志、实例锁与退出协调 |
| [语言键及英文默认值](Telegram/Resources/langs/lang.strings)、[内置翻译](Telegram/Resources/langs/extras) | 官方与定制语言来源，保留当前语言包体系 |
| [语言生成](Telegram/cmake/td_lang.cmake)、[内置资源注册](Telegram/Resources/qrc/extras/extras.qrc)、[语言覆盖](Telegram/SourceFiles/extras/extras_lang.cpp) | 翻译生成、资源消费者和内置语言选择 |
| [调试入口](Telegram/SourceFiles/extras/debug)、[Debug 设置页](Telegram/SourceFiles/extras/ui/settings/settings_debug.cpp) | 开发版服务、命令注册、模拟能力及调试页面 |
| [上游登记](.github/upstream.json)、[构建补丁](scripts/build_support/cmake_patch.py) | 已适配官方基线、同步规则与官方 CMake 定制；标签保存在 `refs/upstream-tags/` |
| [Git 白名单](.gitignore)、[Pi 配置](.pi/settings.json)、[技能同步](scripts/build_support/sync_skills.py) | 共享文件范围、技能发现和镜像同步基线 |

## 任务连续性与验证

- 默认不新增或修改测试代码，只有用户明确要求编写测试时才执行；必要验证采用构建、已有测试或应用调试。
- 已完成且明确通过的验证作为后续工作的有效依据。新任务、切换 Bug 或新一轮修改本身不触发重查、重测或重复分析。
- 执行验证前先查已有记录，只分析本次改动及实际依赖关系。优先验证最小受影响功能；独立功能分别保留结果，公共接口、共享模块或集成流程受到影响时才扩大对应范围。
- 仅在改动影响原验证覆盖的行为，相关接口、依赖或共享状态变化，环境或构建配置使原结果失效，出现质疑原结果的新证据，或用户明确要求时，重新执行对应验证。同一文件、模块或项目不构成重跑依据。
- 未受影响的通过项直接继承；完成后只汇报本次修改和实际执行的验证。已完成的工作作为后续基础，验证范围由实际影响决定。

## 技能使用

`.codex/skills` 为主源，`.claude/skills` 为镜像，两者均由 Git 追踪，不创建修改前技能备份。Pi 只引用 `../.codex/skills`，Claude 通过单行 `CLAUDE.md` 读取本文件。首次修改技能前，在两侧一致时先执行一次同步建立本地基线；每批主源修改后立即同步并只读检查，关联改动一起提交。镜像独立修改先归入主源，再同步。

| 场景 | 阅读技能 |
|---|---|
| 编写、修改、重构或审查代码 | [code-style](.codex/skills/code-style/SKILL.md) |
| 新增、翻译或审查界面文案、翻译键和语言资源 | [translation](.codex/skills/translation/SKILL.md) |
| 独立修改完成且必要验证通过后及时创建本地提交，或整理、修改提交 | [commit](.codex/skills/commit/SKILL.md) |
| 推送提交、分支或标签 | [git-push](.codex/skills/git-push/SKILL.md) |
| 查看或更新版本、整理更新日志、准备发布 | [version-bump](.codex/skills/version-bump/SKILL.md) |
| 回应或回复 Git issue | [issue-reply](.codex/skills/issue-reply/SKILL.md) |
| 准备、创建、更新、描述或审查 PR | [pull-request](.codex/skills/pull-request/SKILL.md) |
| 审查隐私性或本机独特路径 | [privacy-review](.codex/skills/privacy-review/SKILL.md) |
| 业务逻辑变化或补充调试指令；应用调试、功能验证、切页、控件操作、启停与截图 | [app-debug](.codex/skills/app-debug/SKILL.md) |
| 检查官方版本、比较改动、同步适配或登记上游基线 | [upstream-sync](.codex/skills/upstream-sync/SKILL.md) |

专项规则与完整操作步骤由技能维护；脚本入口、用途和调用方式统一保存在下方“工程入口”。

## 方案与记录

按复现、验收和交接需要创建方案与记录；普通修改用提交与回复说明，完整工具输出保存在附件中。`build/` 整体不入库；构建清理保留 `build/docs/`、`build/record/` 和需要保留的验证证据。

### 命名与阶段

- 方案保存为 `build/docs/YYYYMMDD-简体中文标题.md`，同一任务维护一份。
- 记录保存为 `build/record/YYYYMMDD-简体中文标题-序号.md`，附件放在同名目录；文件首行使用与文件名一致的 H1 标题，不含 `.md` 后缀。
- 日期取任务开始日，标题用简体中文概括目标，跨天和跨会话保持相同日期与标题。
- 同一日期和标题下，序号从 1 连续递增。每个序号对应一个独立探索或实施目标，至少包含一项可复核的验收条件。
- 同一目标的重试、修正、补编和恢复继续更新原文件；切换到独立目标时才增加序号，保留前一阶段的实际完成或受阻状态。
- 每个阶段记录最多 300 行，含空行；超过时合并重复内容，将完整工具输出保存为附件，不按篇幅另开阶段。

### 内容与证据

- 顶部写任务目标、本阶段目标、状态、更新时间、分支与提交、未提交改动、下一步；没有的项写“无”。
- 状态为进行中、受阻、已完成、已终止；对应验收条件全部通过后标记已完成，终止时说明原因。
- 验收项逐项标记未执行、通过或失败，附证据路径或失败原因；截图和日志有固定输出位置时引用原位置。
- 用户约束和决定只保留当前有效内容；存在跨阶段依赖时，维护唯一覆盖清单或索引、已通过门槛和剩余项。
- 本地要求、私人环境与对话过程保存在本地记录，公开提交和 PR 聚焦实现与相关验证。
- 记录正在运行的作业及句柄、当前执行者、临时设置、测试数据、备份和恢复要求；结论变化、暂停或交接前更新。

### 恢复

1. 按开始日与中文标题定位任务，先阅读该任务最大数值序号的记录。
2. 核对记录状态、证据、当前分支、提交和工作区差异，判断后续改动是否影响已有验收。
3. 核对原作业句柄、应用或设备的真实状态，按依赖继续剩余工作；需要时回溯方案和旧阶段。等待超时后沿用原作业句柄确认终态。

## 工程入口

以下命令以仓库根目录为工作目录；从其他目录调用时，将脚本参数写为该仓库脚本的实际路径，脚本自行定位项目。普通 Python 调用即可。

| 操作 | 命令与作用 |
|---|---|
| 查看准备入口帮助 | `python scripts/prebuild.py --help` |
| 列出依赖阶段 | `python scripts/prebuild.py --list` |
| 准备依赖 | `python scripts/prebuild.py`；指定阶段用 `--stage <名称>`，可重复；仅首次构建或依赖输入变化时执行 |
| 全量代码行数检查 | `python scripts/prebuild.py --check-code-style`；离线只读，包含未跟踪和被忽略的自有源码，排除第三方与生成物；超限返回非零 |
| 指定改动的代码检查 | `python scripts/prebuild.py --check-code-style <仓库相对文件或目录> ...`；用于本次代码修改与提交前验证 |
| 查看构建入口帮助 | `python scripts/build.py --help` |
| 构建 Debug | `python scripts/build.py --dev` |
| 构建 Release | `python scripts/build.py`；当前入口默认 Release，`--all` 构建两种配置 |
| 指定构建并发 | 在构建命令后加 `--jobs N`，默认 12，实际范围以帮助和 `builder.py` 为准；用户覆盖值在本次会话后续构建中继续显式传入，新会话恢复默认 |
| 调试符号与重新配置 | Debug 需要完整符号时加 `--pdb`；改变该选项触发全量重编。`--reconfigure` 重新配置，日常 CMake 清单变化由脚本自动处理 |
| 运行已有存储测试 | `python scripts/build.py --dev --test storage`；按受影响范围选择 |
| 打包产物 | 构建命令加 `--pack`；`--clean-pack` 清理目标产物目录的运行残留后打包 |
| 同步技能镜像 | `python scripts/build_support/sync_skills.py --sync`；以 `build/skill-sync.json` 为基线，镜像独立变化时报错；主源删除的镜像文件保留到 `build/skill-sync-removed/` |
| 只读检查技能一致性 | `python scripts/build_support/sync_skills.py --check`；核对文件集合与字节，排除 Python 缓存，无需 SDK、编译器或应用 |
| 查看调试命令 | `python .codex/skills/app-debug/scripts/cli.py --help`；具体命令使用 `<命令> --help`，实际任务步骤查 app-debug |
| 调用调试命令 | `python .codex/skills/app-debug/scripts/cli.py <命令> [参数]`；用独立 `+` 串联命令，示例：`app.start --no-focus --offline --profile simulation + simulation.enter` |
| 生成 V2 更新签名材料 | `python scripts/update_signing.py init --private-dir <忽略的私钥目录>`；生成独立 Ed25519 根与发布密钥，并更新内置公钥清单；已有私钥拒绝覆盖 |
| 签署变更后的密钥清单 | `python scripts/update_signing.py sign-manifest --private-dir <私钥目录>`；校验根公钥匹配后签名原始 manifest；先递增清单版本，根私钥仅离线保存 |
| 生成签名更新包 | `python scripts/package_update.py --packer <原生Packer> --platform windows --arch x64 --input-dir <产物目录> --output-dir <输出目录> --path AstraGram.exe --path Updater.exe --path modules --private-key <发布私钥>`；平台支持 `windows/linux/macos`，架构支持 `x64/arm64`；默认从版本文件读通道和更新码，本地验收可用 `--version`、`--channel` 覆盖；CI 从 `UPDATE_SIGNING_KEY` 取发布私钥 |
| 查看或修改应用版本 | `python scripts/build_support/version.py` 查询当前值；传 `<官方版本>[.<修订号>]` 修改版本文件，执行条件按 version-bump |
| 检查官方版本 | `python .codex/skills/upstream-sync/scripts/upstream.py check`；包含正式和测试 tag |
| 生成上游报告 | `python .codex/skills/upstream-sync/scripts/upstream.py report`；`--to <版本>` 指定目标，`--base <版本>` 指定比较基线 |
| 登记已适配版本 | `python .codex/skills/upstream-sync/scripts/upstream.py done <版本>`；前置与登记后检查按 upstream-sync |
| 路径审查 | `python .codex/skills/privacy-review/scripts/scan_commits.py`；默认审查 `origin/main..main`，子仓库或明确范围用 `--repo <路径> --base <引用> --head <引用>`；候选判断按 privacy-review |
| 查看专项脚本帮助 | 上游与路径审查入口均支持 `--help`，先查实际参数再执行 |

构建入口管理配置与编译，不手动运行 CMake。依赖配方的内容参与缓存键；产物由版本源和目标架构派生到根 `build/`。Windows 包含主程序、更新器和 `modules/<架构>/TelegramWidgetHelper.exe`，x64 另有图形模块，收集及打包以 `paths.py` 为准。`AYUGRAM_CCACHE` 用于显式选择 ccache，会改变增量构建方式。

调试 CLI 的 `AYUGRAM_DEBUG_ROOT` 可显式选择目标源码根；它仍核对实际可执行路径和数据目录。`--profile` 选择独立调试数据，焦点、停止和现场恢复规则由 app-debug 维护。
