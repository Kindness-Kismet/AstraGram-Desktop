# AstraGram — 项目规范与架构

AstraGram 是 Telegram Desktop 的 fork，前身是 AyuGram Desktop。本文档面向协作者和 AI 助手，说明项目结构、编码规范和构建流程。内容以本代码库的实际状态为准。

---

## 项目定位

在 Telegram Desktop 之上叠加五类定制能力：

- **隐身模式**：控制已读回执、在线状态、输入状态的发送时机
- **消息留档**：把已删除消息与编辑历史保存在本地
- **正则过滤**：按正则表达式隐藏消息（含隐藏已拉黑用户的消息）
- **文本处理**：中英文之间自动加空格、过滤异常组合字符
- **界面定制**：宽消息倍率、头像圆角、开关样式等

技术栈：C++20、Qt 5.15.19（静态编译，源码见 prebuild 的 `qt_5.15.19` 阶段）、rpl 响应式库、CMake 3.16+、nlohmann::json。

---

## 上游项目结构

改代码前先确认文件应该放在哪里。本节说明各目录的职责划分。

### 仓库骨架

```
AyuGramDesktop/
├── Telegram/                # 全部产品源码
│   ├── SourceFiles/         # 主源码（41 个顶层目录，见下）
│   ├── CMakeLists.txt       # 源文件在此逐个登记（extras/ 同样逐文件列出）
│   ├── cmake/               # 平台与依赖的 CMake 模块
│   ├── codegen/             # 样式、emoji、TL scheme 的代码生成器
│   ├── Resources/           # 图标、音频、翻译等资源
│   ├── ThirdParty/          # 外部工具（msys2、gyp 等，由 prebuild 安装）
│   ├── lib_ui/              # 界面基础库（fork 的子模块，本仓库有改动）
│   ├── lib_tl/              # TL scheme 解析（fork 的子模块，改过 codegen）
│   ├── lib_base / lib_crl / lib_rpl / lib_storage / ...  # 其余 desktop-app 子模块，一般不改
│   └── 其余文件             # 均为上游原样
├── scripts/                 # prebuild.py / build.py（见构建章节）
└── .github/upstream.json    # 已适配的官方 tag 版本号与同步跳过规则
```

**子模块约定**：`lib_ui`、`lib_tl`、`codegen` 是 fork，改动先推到 fork 仓库，再更新主仓库的子模块指针；`cmake` 用官方子模块，定制由构建脚本打补丁。其余 `lib_*` 视为只读依赖。

### SourceFiles 分层

按职责分四层理解（目录名沿用 tdesktop 原名）：

**进程与生命周期**

| 目录 | 职责 |
|---|---|
| `main` | Account / Session / Domain 生命周期，多账号 |
| `core` | `Core::App()` 全局单例，启动与退出流程，沙箱与更新调度 |
| `platform` | Windows / macOS / Linux 的平台差异（通知、托盘、字体） |
| `_other` | 打包、更新器、开机自启等安装周边 |

**数据层**

| 目录 | 职责 |
|---|---|
| `mtproto` | MTProto 协议：数据中心列表、加密连接、授权状态 |
| `api` | 主动请求的语义化封装（`ApiWrap` 一族），调用 MTProto 但不处理协议细节 |
| `data` | 内存数据仓库：`PeerData` / `UserData` / `HistoryItem` / `Session`，全部在主线程 |
| `storage` | 本地持久化：缓存、数据库、加密的 tdata |
| `tde2e` | 端到端加密库 |

**聊天界面层**

| 目录 | 职责 |
|---|---|
| `window` | 主窗口、会话导航、窗口控制器 |
| `history` | 消息列表渲染：气泡，`HistoryItem`（数据）与 `Element`（视图）分离 |
| `dialogs` | 左侧会话列表 |
| `chat_helpers` | 输入框、剪贴板、机器人交互辅助 |
| `media` | 媒体查看器与播放器（view / player 子目录） |
| `overview` | 聊天内媒体总览标签页 |
| `layout` | 媒体网格布局基类（overview / photo 共用） |
| `editor` | 图片与视频编辑 |
| `iv` | 即时预览页面 |
| `boxes` | 通用弹窗（editors / pickers / confirm） |
| `info` | 右侧信息面板（个人资料 / 媒体 / 管理员） |
| `calls` | 通话 |
| `settings` | 官方设置页 |
| `intro` | 登录引导 |
| `profile` / `statistics` / `payments` / `passport` / `export` / `support` / `poll` / `inline_bots` / `webauthn` | 各自独立的领域功能 |

**支撑层**

| 目录 | 职责 |
|---|---|
| `ui` | 通用控件与样式应用层（基础控件在 lib_ui） |
| `lang` | 翻译加载（`tr::lng_*`） |
| `countries` / `ffmpeg` / `menu` / `codegen` / `test(s)` | 国家码、FFmpeg 封装、菜单、内嵌生成、测试 |

### extras/ 定制层

定制代码集中在 `Telegram/SourceFiles/extras/`：

```
extras/
├── extras_infra.cpp              # 初始化入口（翻译/数据库/界面/工作线程/翻译器/调试服务端）
├── extras_settings.{h,cpp}       # 全部设置项（rpl::variable + JSON 序列化）
├── extras_state.{h,cpp}          # 跨组件的运行时状态
├── data/                      # SQLite 留档库与上层封装
├── features/                  # 业务功能，一个功能一个子目录
│   ├── auto_space/            # 中英文之间自动加空格
│   ├── filters/               # 正则过滤与隐藏（含幽灵拉黑名单）
│   ├── forward/ message_shot/ streamer_mode/ translator/
├── debug/                     # 调试服务端（仅 _DEBUG 编译），commands/ 一个领域一个文件
├── ui/                        # extras 的控件与设置页
├── utils/                     # Session / Peer 转换、远程配置
└── libs/sqlite/               # 内嵌 SQLite
```

**改动位置对照表**：

| 要做的事 | 放在哪里 | 附加要求 |
|---|---|---|
| 新增定制功能 | `extras/features/<名称>/`，并在 `Telegram/CMakeLists.txt` 的 `extras_files` 逐文件添加 | — |
| 新增设置项 | `extras_settings.{h,cpp}`：成员、`to_json`、`from_json` 三处同步 | — |
| 设置项的界面 | `extras/ui/settings/`，入口注册在 `settings_main.cpp` | — |
| 需要持久化的数据 | `extras/data/` | — |
| 新增调试指令 | `extras/debug/commands/<领域>_commands.cpp` + CMake 登记 | 必须包在 `#ifdef _DEBUG` 里 |
| 修改上游行为（渲染、菜单等） | 直接改上游文件 | — |
| 界面基础控件改动 | `Telegram/lib_ui/` | 先推 fork 仓库，再更新主仓库子模块指针 |

---

## 编码规范

### 文件大小与职责

- 主仓库自行维护的 `.cpp` 文件不超过 2000 行，含注释与空行；新增文件和拆分后的文件都按此判断。
- 既有超限文件按任务分批整改，先处理万行业务源码；按职责拆分，不用编号分片或互相包含实现文件来规避上限。
- 拆分前记录职责、依赖与验证方案，保持对外接口和行为，同步更新构建清单。
- 本次拆分只修改主仓库，不修改子仓库；自动生成文件和原样引入的第三方库源码不作业务拆分。

### 命名

| 元素 | 规则 | 上游实例 |
|---|---|---|
| 局部变量 | 小驼峰，优先 `const auto` | `dataName`、`phone`、`flags` |
| 成员变量 | 小驼峰加 `_` 前缀 | `_lastseen`、`_peerGiftsCount` |
| 常量 | `k` 前缀加大驼峰 | `kWideIdsTag` |
| 类与结构体 | 大驼峰 | `HistoryItem` |
| 函数 | 小驼峰（extras 代码必须遵守；上游风格混杂，不做统一改造） | `processUser` |
| 命名空间 | 大驼峰或匿名 | `ExtrasInfra`、`namespace { ... }` |
| 文件名 | 小写加下划线，前缀与所属领域一致 | `data_user.cpp`、`history_item.cpp` |

### 本地方案与过程记录

- 按需生成，不把写文档作为每次任务的固定步骤。提交说明和最终回复足以说明的普通修改，不另写方案或过程记录。
- 用户要求方案，或需要保留实现路线的取舍、迁移步骤、跨阶段依赖时，才在 `build/docs/<任务标识>.md` 保存方案；同一任务只维护一份。
- 需要保留复现依据、验收结果或交接进度时，过程记录保存在 `build/record/<任务标识>-<阶段号>.md`，不另建总结、复核、收尾等重复文档。
- 任务标识采用 `YYYYMMDD-主题`，日期取任务开始日；同一任务跨天、跨会话继续沿用。阶段号从 `1` 连续递增，例如 `20261007-设置迁移-1.md`、`20261007-设置迁移-2.md`。
- 一个阶段对应 **1 个可独立验收的目标、至少 1 项可复核的验收条件**。开始记录时写清目标和条件；全部条件通过，或明确终止并记录原因后，进入下一个目标才增加阶段号。
- 同一目标下的重试、审查修正、补编和暂停后继续都更新原阶段。新增需求能独立验收时列为后续阶段；未完成或终止的阶段标明状态，不写成已通过。
- 记录顶部固定写明任务目标、阶段状态（进行中 / 受阻 / 已完成 / 已终止）、更新时间、分支与提交号、未提交改动摘要、下一步具体动作；没有的项写“无”。结论变化、暂停或交接前更新。
- 验收项逐项标记未执行、通过或失败，并附证据路径。关键决定、用户约束和未完成事项只保留当前有效内容；涉及测试账号、临时设置或备份时，写清现场及恢复要求。
- 接手时先阅读同一任务阶段号数值最大的记录，再核对当前分支、提交和工作区差异；确认记录后的改动是否影响已有验收，按需回溯方案和旧阶段，不凭文件编号推断完成状态。
- 每份阶段记录最多 **200 行，含空行**。超过上限先合并重复内容，不按时间、消息轮数或文档长度增加阶段号。
- 临时脚本和验证附件放在 `build/record/<任务标识>-<阶段号>/`。工具已有固定输出位置的日志、截图保留原位置，记录中引用路径，不复制原始输出或完整对话。

### 注释

- 简体中文，说清意图即可，不使用行话和缩略语
- **最多两行**，一行能说清就写一行
- 只写意图、约束、边界条件，不复述代码本身在做什么
- 单行注释用 `//`，函数注释写在声明或定义上方

```cpp
// 构造本地模拟模式绕过登录，不写入磁盘，重启后消失。
[[nodiscard]] Result enterSimulationMode(const QStringList &args);
```

### 优先使用卫语句

先排除异常情况提前返回，正常流程靠左对齐，避免深层嵌套。上游代码普遍是这个形态：

```cpp
// 正确
void Process(const TextWithEntities &text) {
	if (text.empty()) {
		return;
	}
	const auto entity = text.entities.front();
	// ...正常逻辑，无嵌套
}

// 错误：把正常流程包进 else
void Process(const TextWithEntities &text) {
	if (!text.empty()) {
		const auto entity = text.entities.front();
		// ...正常逻辑多包了一层
	}
}
```

- 分支以 `return`、`continue`、`break` 结尾时，后面不接 `else`，下一个判断直接左对齐
- 循环里先用 `continue` 跳过不相关的项；查找类逻辑先找到目标，再在循环外处理
- 同一个函数既能查询又能修改时，先处理只查询的情况并返回，修改流程不包在 `if` 里
- 分支里还有校验、嵌套超过两层时，把分支抽成返回 `Result` 或 `std::optional` 的小函数
- 二选一的对称分支（显示或隐藏、新建或更新）保留 `if/else`，不为了套卫语句强行改写

### 不要过度防御

- 上游核心代码（`data_session.cpp`、`history.cpp` 等）完全没有 `try/catch`——**异常不是这个项目的错误处理方式**，不要引入
- 用 `Expects()` / `Ensures()`（GSL）表达契约，上游大量使用；前置条件由调用方保证时，被调方断言即可，不需要双向判空
- 只在真正可能失败的地方校验；写任何兜底分支前先确认这个分支现实中会走到
- 本地无法处理的错误交给上层（返回空值或错误码），不要直接忽略

### 响应式（rpl）

- 所有设置项都是 `rpl::variable<T>`：一次性读取用 `current()`（返回引用），订阅变化用 `value()`（返回数据流）
- 界面订阅必须绑定 `lifetime()`，不允许裸 lambda 捕获 `this` 而不做生命周期保护
- 写入设置直接赋值即可触发通知，不要赋值之后再手动发一次

### 线程

- 所有界面操作以及 `Data::Session`、`History`、`PeerData` 的访问都在主线程
- 工作线程需要访问主线程对象时，用 `dispatchToMainThread`（`extras/utils/telegram_helpers.h`）
- 不要在工作线程调用 `ExtrasSettings::getInstance()` 或 `Core::App()`
- Qt 对象有线程归属：`QTcpServer` 在主线程创建，信号槽就在主线程回调，无需额外调度；跨线程信号要显式指定 `Qt::QueuedConnection`

### 错误处理与日志

- 失败用返回值表达：`std::optional<T>` 或 `Result { bool ok; QString payload; }`
- 唯一允许的 `try/catch`：调试服务端的 `Execute()` 包住 handler，避免异常终止监听
- 日志用 `LOG(("Category: message %1").arg(value))`，不要用 `qDebug()`
- 内部错误返回、调试日志和脚本诊断信息使用英文；界面文案继续使用翻译键。

---

## 构建

### 环境

- Windows：MSVC v145（Visual Studio 2026，工具集 14.51）、Windows SDK 10.0.26100.0
- macOS：Xcode 14+；Linux：GCC 11+ 或 Clang 15+

工具链版本集中在 `scripts/build_support/toolchain.py`（`TOOLSET_VERSION` / `CMAKE_TOOLSET` / `CMAKE_GENERATOR`），换 Visual Studio 版本只改这里。

### prebuild（第三方依赖预编译）

```bash
python scripts/prebuild.py            # 全量预编译，产物在 build/tmp
python scripts/prebuild.py --list     # 34 个阶段清单（qt_5.15.19、openssl3、ffmpeg、tg_angle、tg_owt、breakpad、tde2e 等）
python scripts/prebuild.py --stage openssl3 --stage qt_5.15.19   # 只跑指定阶段，可重复指定
python scripts/prebuild.py --clean    # 清空依赖缓存
```

- **只在依赖变化时需要跑**（首次构建、子模块指针更新、`build/version` 变更），日常改代码不需要
- 自动探测 MSVC 环境，进度输出强制 UTF-8，避免 cp936 控制台报错
- 阶段的定义（含注释）参与缓存键计算，改注释也会导致该阶段重新编译

### build（产品构建）

```bash
python scripts/build.py               # Release，默认
python scripts/build.py --dev         # Debug
python scripts/build.py --dev --pdb   # Debug，并重新生成完整的 AstraGram.pdb
python scripts/build.py --all         # 两个配置都构建
python scripts/build.py --jobs 12     # 协作统一使用 12；脚本默认 32，上限 128
python scripts/build.py --reconfigure # 丢弃 CMake 缓存重新配置
python scripts/build.py --api-id <id> --api-hash <hash>   # 覆盖 API 凭据
```

- 产物在带版本号的目录：`build/AstraGram-v<版本>-win-x64-release|dev/`（版本号读取 `Telegram/build/version`）
- 脚本自己完成配置和构建两步，**不要手动执行 cmake**
- 收集产物前会自动停止占用目标可执行文件的进程（按绝对路径匹配，不按进程名）
- 默认不生成调试信息和 pdb；`--pdb` 只作用于 Debug：调试信息嵌进 obj、关闭增量链接，每次重写 pdb，避免增量链接让 pdb 只增不减
- 加上或去掉 `--pdb` 会改变全部编译参数，触发全量重编，只在需要符号时使用
- 设置环境变量 `AYUGRAM_CCACHE=<ccache.exe 路径>` 后经 ccache 编译（云端 Windows 构建使用）；开启后每次都要重新编译全部文件（大多直接命中缓存），不适合本地增量构建

### 编译并发

- 构建脚本默认 32 并发，`--jobs` 接受 1 至 128，超过上限或非正数直接报错。
- 助手编译统一显式传入 `--jobs 12`，例如 `python scripts/build.py --dev --jobs 12`；后续任务同样遵守，不使用脚本默认并发。

---

## 调试（app-debug skill）

Debug 构建会在 `ExtrasInfra::init()` 里启动 `QTcpServer`，监听 `127.0.0.1:20100`。服务端指令通过 `app.help` 查询，命令行工具另有应用生命周期指令：

| 指令 | 说明 |
|---|---|
| `app.ping` / `app.info` / `app.help` | 探活、应用信息、指令清单 |
| `app.quit` | 走 `Core::Quit()` 正常退出（`app.stop` 内部先用它） |
| `crash.log` | 读取崩溃日志 |
| `simulation.enter [userId]` | 构造本地模拟模式绕过登录并自动生成固定场景，默认 999999999 |
| `simulation.list` / `simulation.open <key>` | 查询七类固定场景和实际状态，打开话题、收藏来源或其他视图 |
| `simulation.trigger [list|key]` / `simulation.clear` | 触发生日、更新、下载等临时提示，或清除列表提示 |
| `simulation.message <text> [--peer <peerId>] [--from <userId>] [--blocked] [--shadow-ban]` | 往模拟模式插入本地文本消息（缺省 Saved Messages），用于验证渲染与隐藏逻辑 |
| `chat.list [filter]` | 列出会话的 peerId 与名称，供 message.send / chat.open 定位目标 |
| `message.send <peerId> <text\|--file path>` | 真实发送文本到指定对话（`--file` 按 UTF-8 读文件原样发送），走官方发送链路，仅发往自己掌控的测试对话 |
| `chat.open [peerId]` | 打开指定聊天，缺省为 Saved Messages |
| `session.test-mode` | 切换到官方测试数据中心（+99966 号段，验证码 22222） |
| `chat.history-stats <msgId>...` | 查询收藏夹中指定消息的存在、隐藏与视图状态 |
| `theme.set <dark|light>` | 切换暗色或浅色主题 |
| `theme.reset-background` | 重置聊天背景 |
| `settings.keys` / `settings.dump` | 设置键名清单、全量 JSON 导出 |
| `settings.get <key>` / `settings.set <key> <value>` | 读写单个设置 |
| `page.list [filter]` / `page.open <id>` | 查询官方与定制设置索引，按编号打开设置页 |
| `ghost.status` | 隐身模式状态（需要已登录） |
| `storage.stats` | 留档数据库的路径与大小 |
| `screenshot.take` | 截取活动窗口，保存到 `build/screenshots/` |
| `control.list [filter] [--all]` | 列出控件树（objectName、类名、几何、可见性） |
| `control.click <objectName \| #序号>` | 进程内合成点击，按真实事件路径投递 |
| `filter.*` / `storage.*` / `text.process` / `action.*` / `control.get\|set` 等 | 过滤、留档、文本处理、官方快捷动作与控件值，完整清单见技能指南 |

命令行工具封装（另含本地实现的 `app.start` / `app.restart` / `app.stop`）：

```bash
python .claude/skills/app-debug/scripts/cli.py app.info
python .claude/skills/app-debug/scripts/cli.py settings.set streamerMode true
```

- 绕过登录：登录页的“模拟模式”按钮（仅 `_DEBUG` 构建可见），每次进入自动生成固定场景
- 自动测试用独立配置：`app.start --profile <名称>`，数据在 `build/debug-profiles/`，跳过链接协议注册等系统集成
- 服务端代码全部在 `#ifdef _DEBUG` 内，Release 二进制里不存在
- 指令在主线程同步执行，耗时指令会导致界面暂时无响应
- `app.stop` 按可执行文件绝对路径校验进程，不按进程名结束进程，避免误杀正式安装版
- **长期持有的 QObject 必须挂在会随 `aboutToQuit` 销毁的父对象上**，或者自行连接 `QCoreApplication::aboutToQuit` 清理。文件级静态 `unique_ptr<QObject>` 会存活到 QApplication 析构之后，触发 Debug 运行库的中止弹窗（曾修复过一次：`dcae424b40`）
- 界面验证需要看截图内容时，用支持读图的模型直接查看 `screenshot.take` 的输出

详见 `.claude/skills/app-debug/SKILL.md`。

### Debug 设置页

Settings → AstraGram Preferences → Debug，可见条件是 `#ifdef _DEBUG` 或 `Logs::DebugEnabled()`。实现在 `extras/ui/settings/settings_debug.{h,cpp}`。

---

## upstream-sync skill

同步官方 Telegram Desktop 最新 tag，包含正式版与测试版。`.github/upstream.json` 只登记已适配的官方版本号，子模块基线取官方该版本记录的子模块指针，本地定制用 git diff 计算：

```bash
python scripts/upstream.py check              # 官方有没有更新的 tag，包含测试版
python scripts/upstream.py report             # 生成各仓库的改动报告，保存在 build/upstream-sync/
python scripts/upstream.py done <官方版本>     # 适配并编译通过后登记
```

- 按版本号选取最新官方 tag，正式版与测试版一同参与比较；不跟进 tag 之间的开发分支提交
- 保持 main 主线与现有发版方式，上游 beta 不强制本项目增加 beta 后缀；适配使用临时功能分支
- 官方标签放在 `refs/upstream-tags/`，不和本仓库同名的发布标签混在一起
- 报告按“需要合并 / 直接采用 / 官方新增 / 官方删除 / 已与目标一致 / 按 skip 跳过”分类，需要合并的文件已预演三方合并
- `upstream.json` 里 `skip` 是长期不跟进的路径，`notes` 是跟进时要注意的路径，`deferred` 是暂缓、以后要补的改动
- `lib_ui` 是 fork 的子模块：改动先推到 fork 仓库，再在主仓库更新子模块指针，两步都要做
- `cmake` 是官方子模块，定制由 `scripts/build_support/cmake_patch.py` 构建时动态 patch，`git status` 里 `modified: cmake (modified content)` 是预期状态；报告会检查补丁锚点在官方新版里是否还在

---

## AI 助手配置目录

`.claude/skills/` 与 `.codex/skills/` 内容完全相同，分别供不同的 AI 工具读取，修改时两边同步。`CLAUDE.md` 只有一行 `@AGENTS.md`，规范只维护本文件这一份。

`.pi/` 只有 `settings.json` 进入版本控制，运行时产生的 `tasks/` 已被忽略。

---

## Git 协作

- 功能分支命名 `feature/<name>` / `fix/<name>`
- **原子提交**：一个提交只做一件事
- **逐个路径添加**：只 `git add` 具体路径，提交前用 `git diff --cached` 确认内容都属于本次改动（工作区经常有子模块指针变动和临时文件）
- 执行 `git commit`（含 `--amend`）前，应用当前客户端的 `commit` 技能；助手自行决定提交、操作配套构建仓库时同样适用。
- 提交标题和正文使用英文，中文回复与中文注释规范不适用于提交信息。标题一句祈使句，不超过 70 字符；正文说明原因、最终行为和验证结果。
- 不要强制推送已公开的分支

---

## 常见问题

1. **nlohmann::json 临时对象**：`for (const auto &[k, v] : SettingsJson().items())` 会产生悬空引用，range-for 的生命周期延长不覆盖 `items()` 返回的代理对象。先把结果存进具名变量再调用 `.items()`。
2. **`value()` 与 `current()`**：前者返回数据流用于订阅，后者返回引用用于一次性读取。不要在 lambda 里捕获 `current()` 返回的引用。
3. **`not_null<T*>`**：GSL 类型，不能隐式转换成 `T*`，需要调用 `.get()`。不要让它指向栈上对象之后返回。
4. **`.gitignore` 大小写**：Windows 文件系统大小写不敏感，`Debug/` 规则会连带忽略 `extras/debug/`（已显式放行）。新增路径前先跑 `git check-ignore -v <path>`。
5. **配置与构建**：`build.py` 自己处理配置步骤；改 `CMakeLists.txt` 会自动重新配置，不需要删除 build 目录。
6. **静态持有 QObject**：任何静态或全局的 QObject 都无法安全存活到 QApplication 析构之后，在析构阶段操作 Qt 对象是未定义行为。见调试章节最后一条。
7. **字符串字面量拼接**：`"(" kPattern ")"` 这种写法要求 `kPattern` 是宏，`constexpr` 变量不能参与字面量拼接。同理 `QStringLiteral` 本身是宏，参数里不能拼接标识符，这种场合改用 `QLatin1String`。

---

## 禁止事项

1. Release 构建里保留调试代码——调试服务端、模拟模式、测试数据中心开关全部要在 `#ifdef _DEBUG` 内
2. 按进程名结束 `AstraGram.exe`——应按可执行文件绝对路径或端口占用 PID 校验
3. 在主线程调用阻塞接口——`ExtrasSync::*Sync` 系列会运行事件循环等待 MTProto 响应，导致界面无响应
4. 在 `Telegram/lib_ui/` 之外引用 `extras/extras_ui_settings.h`——codegen 硬编码了该 include 路径（`codegen/style/generator.cpp:676`）
5. 提交 `build/`、`tdata/`、`.user` 文件
6. 只在本地提交 fork 依赖（`lib_ui` / `lib_tl` 等）的改动就更新主仓库指针——子模块提交必须先推到 fork 仓库
7. 用异常做错误处理——见"不要过度防御"

---

## 审查清单

改代码前自查：

- [ ] 新代码放在正确的目录？（见"改动位置对照表"）
- [ ] 新文件登记进 `Telegram/CMakeLists.txt` 的 `extras_files`？
- [ ] 新增或拆分后的 `.cpp` 文件不超过 2000 行？
- [ ] `extras/debug/` 下的新文件没有被 `.gitignore` 误拦？
- [ ] 调试代码包在 `#ifdef _DEBUG` 内？
- [ ] 新设置项在 `extras_settings` 的成员、`to_json`、`from_json` 三处同步？
- [ ] rpl 订阅绑定了 `lifetime()`？
- [ ] 跨线程调用走了 `dispatchToMainThread`？
- [ ] 优先用卫语句，`return` 后不接 `else`，嵌套不超过两层，没有过度防御？
- [ ] 注释是中文、不超过两行、不用行话？
- [ ] 改了 lib_ui 等 fork 子模块，已推到 fork 仓库再更新指针？
- [ ] 提交前 `git diff --cached` 确认内容都属于本次改动？

---

## 上游与社区

- 官方 tag（包含测试版）的检查、改动报告与登记统一走 `upstream-sync` skill
