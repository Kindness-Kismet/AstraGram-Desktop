# 运行控制与观察

## 服务端指令

| 指令 | 参数 | 作用 |
|---|---|---|
| `app.ping` | 空 | 心跳检测，返回 `pong`，验证服务端连通性。 |
| `app.info` | 空 | 返回版本、配置、工作目录、会话与窗口状态 JSON。 |
| `app.check-update` | 空 | 手动触发一次更新检查，不修改自动检查偏好，链接与包校验按当前构建的发布语义走。 |
| `app.update-info` | 空 | 报告自动检查偏好 `autoCheckUpdates`，以及 `tdata/prefix` 的内容与内存里解析出的地址。 |
| `app.help` | 空 | 列出服务端已注册的全部指令名。 |
| `app.quit` | 空 | 让应用走正常退出流程；退出动作排在事件循环尾部，确保 `OK` 写完 socket 再退。 |
| `window.resize` | `[width height]` | 报告窗口尺寸与最大化状态；给出尺寸时按逻辑像素调整窗口。 |
| `window.material` | `[x y]` | 查询生效模式、状态修订号、根部混色与 Windows 原生材质编号；可选坐标采样现有绘制缓冲，不触发截图、重绘或系统输入。 |
| `window.maximize` | `true\|false` | 最大化或还原窗口。 |
| `window.focus-mode` | `[focus\|no-focus]` | 查询或切换当前实例的调试焦点策略，省略参数时只报告 `focus_mode`。 |
| `window.activation` | `active\|inactive` | 只切换 Qt 内部的焦点窗口与应用激活状态，系统前台不变。 |
| `window.tray` | `hide\|show` | 调用真实托盘流程，隐藏或恢复最近活动的主窗口。 |
| `anim.speed` | `[speed]` | 查询或临时调整过渡动画速度，范围 0.01 至 10，1 为正常；只存内存，重启恢复。 |
| `anim.sample-window` | `<新 WebP 绝对路径> <x> <y> <width> <height>` | 每 16 毫秒采样已绘制缓冲，持续 1600 毫秒；区域使用窗口逻辑坐标，最多 100000 像素。返回任务编号，`job.status` 提供连续颜色样本及 14 帧预览图，不触发重绘。 |
| `anim.check-image-transition` | 空 | 用真实动画驱动检查同图中途续帧，返回任务编号；用 `job.status` 核对重建前后像素与最终蓝色。要求动画开启、速度为 1，不改消息或设置。 |

`app.quit` 一般不直接调，`app.stop` 内部先发它。

`app.start`、`app.ensure`、`app.restart` 支持 `--offline`，从启动起禁止 Telegram 连接和连接配置下载，用于验证离线恢复。
`app.info` 的 `offline` 表示该开关是否生效。开关只在开发版有效、不保存到设置；不带该参数重启即可恢复联网，不影响系统网络。

`app.info` 返回字段：`version` / `versionCode` / `configuration` / `workingDir` / `debugLogs` / `hasSession` / `hasWindow` / `simulationMode` / `focus_mode` / `userId`（已登录时）。
`focus_mode` 取值为 `focus` 或 `no-focus`，由启动参数或运行中切换决定。
口令状态另含 `passcodeLocked`、`hasPasscode` 和 `appLockEnabled`，分别表示当前已锁定、设置了口令和启用了应用锁。
`windowMaterialActive` 表示主窗口的材质实际生效状态，可与 `windowMaterial` 设置对照；截图不包含系统合成的桌面背景。

`app.check-update` 只触发不等待：下载与验签是异步的，结果看工作目录下 `tupdates/`
保存的文件和应用日志。更新被禁用时返回 `updater is disabled`。

`app.update-info` 用来定位更新检查会打到哪个地址：`tdata/prefix` 里写的内容与
`resolvedPrefix` 不一致时，说明前缀在运行时被固定值覆写过，改文件不会生效。

`window.resize` 与 `window.maximize` 用来把窗口摆到指定状态，验证依赖窗口
宽度的行为（宽屏布局、栏位折叠）。尺寸是 Qt 逻辑像素，与 `control.list` 的几何同一
坐标系；最大化状态下 `resize` 不生效，指令会先还原窗口再设尺寸。
窗口动作沿用当前 `focus_mode`：`no-focus` 下不请求系统焦点，`focus` 下按应用正常行为显示。
返回的 `visible`、`active` 分别表示可见和活动状态，`focus_mode` 为执行后的实际模式。

```bash
python .claude/skills/app-debug/scripts/cli.py window.resize 1300 900 + screenshot.take
python .claude/skills/app-debug/scripts/cli.py window.maximize true + screenshot.take
```

`window.material` 的 `mode` 为 0 关闭、1 云母、2 亚克力、3 模糊；`rootTint` 和 `pixel` 均按红、绿、蓝、透明度返回 0 至 255。
Windows 的 `nativeBackdrop` 为 1 关闭、2 云母、3 亚克力；`revision` 仅在材质或主题状态改变时递增。
坐标使用选定窗口内的逻辑像素；采样来自上一帧已绘制的缓冲，切换后需让事件循环继续运行，不包含系统合成后的桌面。

`window.activation` 用来验证依赖“应用在前台”的逻辑（已读、阅读时长统计、暂停与恢复）。
它只注入 Qt 的窗口激活事件，不改变系统前台；用户真正点击或切走窗口时会被系统事件覆盖。
返回的 `applicationActive` 为应用级激活状态。需要真实系统焦点时使用 `focus_mode`，
`window.activation` 不能代替前台验收。

`window.tray hide` 要求托盘已启用；`window.tray show` 按当前 `focus_mode` 恢复窗口，
`no-focus` 下显示但不激活，`focus` 下按托盘正常流程激活。
验证关闭到托盘时先用 `window.close`，再用 `window.tray show` 恢复，检查原对话、滚动位置和草稿是否保留。

`anim.speed` 用来观察过渡动画：`0.1` 放慢十倍，`2` 加快一倍。它缩放全部
`Ui::Animations::Simple` 动画（页面滑动、弹出菜单、开关、渐隐渐显、点击波纹），
上述动画只在启动时读取倍率，删除粒子特效实时跟随倍率；加载圈、贴纸、视频这类逐帧播放不受影响。返回 `speed`、
`durationMultiplier`（时长倍率，即速度的倒数）和 `disabled`；`disabled` 为 true
表示动画已被关闭，调速不起作用。Debug 版的 Ctrl+F11 / Ctrl+F12 改的是同一个倍率。
观察结束后用 `anim.speed 1` 恢复。

```bash
python .claude/skills/app-debug/scripts/cli.py anim.speed 0.1 + chat.open-archive + screenshot.take
python .claude/skills/app-debug/scripts/cli.py anim.speed 1
```

`player.state [song|voice]` 查询指定类型或当前活动媒体的 `messageId`、播放状态 `playing`、位置 `position`、长度 `length`、时间单位 `frequency` 和循环模式 `repeat`（0 关闭、1 单曲、2 列表）。连续采样可验证自然结束后的重播，业务控制使用 `player.control`；键盘与焦点检查才使用界面。

## 独立窗口

`chat.open [peerId] [msgId] --new-window` 通过正式入口打开独立聊天窗口，缺省打开收藏夹。
用 `control.list --all` 找到新窗口序号，再用 `control.window #序号` 选中。

`window.resize`、`window.maximize`、`window.activation` 和 `window.close` 操作 `control.window` 选定的窗口，缺省为主窗口。
状态包含该窗口的 `windowMaterialActive`；`window.close` 保留原有关闭确认，不强制丢弃内容。

- `iv.open-file <文件路径>`：通过正式入口打开本地文章预览，窗口标识为 `iv.preview`。
- `iv.open-media-sample <图片一> <图片二> <视频>`：仅在独立模拟配置中生成完整本地富消息，通过正式入口打开 `iv.preview`，包含独立图片、视频、拼图与幻灯片；不发送消息。图片每边不超过 2048 像素，视频播放直接使用原文件，验证期间保留文件。返回模拟消息与媒体编号，可配合 `message.inspect`、`control.list --all` 查询实际状态。
- `iv.open-site <站点网址>`：通过正式入口打开 TON 站点，窗口标识为 `iv.browser`；返回成功只表示已打开窗口，不代表网页加载成功。
- `iv.open-editor`：仅在独立模拟模式调用正式文章编辑入口，创建空白独立编辑窗口 `iv.editor`，不发送消息、不迁移聊天草稿。
- `iv.toggle-search`：向当前选中的活动文章预览或编辑器窗口发送搜索快捷键事件；需要时先用 `window.activation active` 设置应用内部焦点。

## CLI 本地指令

不进服务端，由 CLI 自己完成。

| 指令 | 参数 | 作用 |
|---|---|---|
| `app.start` | `[--profile <名称>] [--background] [--offline] [--no-focus]` | 检查 Debug 应用是否运行，未运行时启动并等待端口就绪（最多 60 秒）；已在运行则保持不动。`--no-focus` 只作用于新启动的实例。 |
| `app.restart` | 同上 | 无论是否在运行都先停止再启动已有开发版产物，等到端口就绪。 |
| `app.stop` | `[--force]` | 校验端口 PID 的可执行文件路径必须匹配本仓库 `build/AstraGram-v*-win-x64-dev/AstraGram.exe`，再让应用自行退出；失败才回落到强制结束。端口未建立时只枚举路径匹配同一形态的进程。 |

`app.start --focus-mode <focus|no-focus>` 只切换已运行实例，不创建新进程；目标不存在时
直接报错。交互与非交互调用都不要把它当作启动参数。

```bash
python .claude/skills/app-debug/scripts/cli.py app.start --no-focus
# Application is ready.
```

## PC 焦点模式

- 不带焦点参数启动 Debug 产物时按应用正常行为显示并聚焦，初始模式为 `focus`。
- 调试技能启动新实例时使用 `app.start --no-focus`；`app.restart --no-focus` 同理，`--no-focus` 不改变已有实例模式。
- 运行中用 `window.focus-mode no-focus|focus` 或窗口动作的 `--focus-mode` 切换；省略与重连沿用实例当前模式。
- `--no-focus` 与 `--focus-mode` 同一段互斥；`--focus-mode` 只允许作用于已运行实例，目标不存在时报错且不启动进程。
- 切换到 `focus` 只更新策略，不主动激活窗口；后续窗口动作按新模式执行。
- `app.info` 与窗口指令返回 `focus_mode`；手动启动且未切换的实例返回 `focus`。
- `no-focus` 下显示、还原、最大化、调整尺寸和托盘恢复都不改变系统前台，窗口保持在前台窗口后面的层级。
- `window.activation` 只改应用内部焦点，与 `focus_mode` 是两套能力；需要真实系统焦点时切换 `focus` 并取得用户明确同意。

```bash
python .codex/skills/app-debug/scripts/cli.py app.start --no-focus
python .codex/skills/app-debug/scripts/cli.py window.maximize true
python .codex/skills/app-debug/scripts/cli.py window.tray hide + window.tray show --focus-mode no-focus
python .codex/skills/app-debug/scripts/cli.py window.focus-mode no-focus
python .codex/skills/app-debug/scripts/cli.py app.start --focus-mode no-focus
```

平台相关的显示、层级与原生状态核对见 [Windows 焦点](focus/windows.md) 和
[Qt 焦点](focus/qt.md)。

单条指令的读超时是 180 秒，长操作不会被 CLI 提前中断。PID 路径不匹配时直接报错拒绝操作，不会错误结束正式安装版。

## 崩溃排查

`app.stop --force` 按原有进程路径校验后直接结束开发版，用于复现“结束任务”后的恢复；默认 `app.stop` 仍先正常退出。

Debug 构建装了未处理异常过滤器：进程崩溃时把异常码与调用栈写进
工作目录 `crash.log`（工作目录见 `app.info` 的 workingDir）。

```bash
python .claude/skills/app-debug/scripts/cli.py page.open extras   # 复现操作
cat build/AstraGram-v*-win-x64-dev/crash.log                   # 读调用栈定位
```

本程序缺少匹配的 PDB 时，调用栈仅显示模块内偏移，无法定位函数名与行号。

## 独立数据目录

先退出当前调试应用，再选择独立配置。CLI 会记住选择，后续无需重复指定；
数据保存至 `build/debug-profiles/simulation/`。

```bash
python .codex/skills/app-debug/scripts/cli.py app.stop
python .codex/skills/app-debug/scripts/cli.py app.start --no-focus --profile simulation
python .codex/skills/app-debug/scripts/cli.py simulation.enter
```

独立配置使用 `-debugprofile`，`app.info` 应显示 `isolatedDebug: true`；外部链接正常打开。

配置名允许 1 至 48 个小写字母、数字、下划线或连字符，首位为字母或数字。
端口已有进程时，CLI 核对可执行路径和工作目录；目录不符会报告并停止执行。
恢复原配置：先 `app.stop`，再 `app.start --no-focus --profile default`。

播放、暂停、切换和停止使用 `player.control`；跳转、倍速、收起和关闭仍通过播放器控件操作。`player.state` 中的 `type` 与 `video` 可核对当前类型。

`app.start`、`app.ensure`、`app.restart` 支持 `--background`，新启动时进入托盘，不主动显示窗口。已有实例保持原状；这不改变单实例规则，也不会操作系统鼠标。

音乐播放器使用独立顶层窗口 `music.window`。点击 `music.floating` 展开迷你条，`music.expand` 或迷你封面展开完整面板，`music.collapse` 收回迷你条，`music.close` 停止并关闭播放器，`music.minimize` 收起窗口并继续播放。关闭语音时若仍有音乐播放流，则停止语音并返回音乐。
`control.window music.window` 选择播放器后可操作控件与截图；主窗口失焦不收起播放器。完整面板默认收起播放列表，`music.playlist` 切换展开或收起，曲目标识为 `music.track.<下标>`。
`music.download` 保存当前曲目的封面原图；附件下载使用 `downloads.start`。`player.state` 还返回倍速、音量、顺序、上下首可用性和已加载播放列表；列表不代表整个会话的全部音乐。
播放器列表滚动结束约 1 秒后淡出滚动条，鼠标停留在滚动条上时保持可见。
