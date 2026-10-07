---
name: app-debug
description: Use this skill when the user asks to debug, test, or verify AstraGram functionality, take a screenshot, read or change settings, inspect ghost mode, check deleted-message storage, control the running Debug app, restart or stop the app, or says phrases like "调试", "测试", "验证", "截图", "看一下设置", "改个设置", "隐身模式", "已删除消息", "重启应用", "停掉应用", "观察界面" in this AstraGram project.
---

# 应用调试

通过命令行控制本仓库的调试构建，覆盖官方与定制设置、业务入口和本地验证。服务端仅在 `_DEBUG` 下启用，监听
`127.0.0.1:20100`。界面指令在主线程执行，返回 `OK` 加可选数据或 `ERR 原因`。
每条连接处理一行指令；带引号的参数采用 JSON 字符串转义，CLI 自动处理。

## 入口与顺序

在项目根目录运行：

```bash
python .codex/skills/app-debug/scripts/cli.py app.start
python .codex/skills/app-debug/scripts/cli.py settings.get streamerMode + screenshot.take
```

用 `+` 串联指令，按顺序执行；整体参数解析成功后才开始操作，执行失败时停在当前步骤。
同一应用的所有调试调用保持串行，CLI 用 `build/app-debug-cli.lock` 排队。

## 验证方式

指令分三类：业务逻辑直接执行；界面交互通过控件指令操作；除页面切换外，不提供直接打开界面的指令。

- 设置和业务状态：用 `settings.*`、`ghost.status`、`storage.stats` 查询。
- 下载、附件、编号提及、设置迁移和播放优先使用[业务指令](guides/business.md)，不通过点击代替已有业务入口。
- 业务验证不接管系统鼠标或键盘，不抢占用户正在使用的其他应用；开发版仍按原有单实例方式运行。
- 官方页面先用 `page.list` 查询真实索引，再用 `page.open` 定位；控件值用 `control.get/set`。
- 实际业务动作使用 `action.list/run` 或原生界面的控件和菜单，异步任务用 `job.status` 查询结果。
- 布局和颜色：导航到目标界面，截取图片并直接查看内容；控件树用于核对几何和可见性。
- 滚动：用 `control.scroll` 查询位置、最大值和可见高度，再结合截图判断。
- 控件交互：用 `control.click`、`control.key`、`control.pointer`，事件只投递到应用内部。
- 悬停：`control.hover` 检查按钮绘制；`control.pointer` 检查自绘控件及菜单的内部事件。
- 命中：`control.click --mouse` 从窗口内部查找目标。系统光标、原生窗口及焦点行为另行人工验证。
- 修改输入文字只用于假会话或已获授权的测试对话，因为应用仍会保存草稿。
- 真实发送、加入、通话等业务动作按用户明确指定的测试范围执行。

截图来自 `QWidget::grab()`，包含宿主内菜单。需要等待切页、主题和弹层动画结束再截图；
OpenGL 区域可能缺失。消息气泡等自绘内容主要通过图片观察。

## 按任务查参数

命名采用“领域.动词 宾语 参数”，复合词用连字符。完整参数与返回值放在各指南中。

| 任务 | 指南 |
|---|---|
| 应用启动、停止、版本、更新、窗口、崩溃 | [运行控制](guides/runtime.md) |
| 假会话、消息、通知、打开聊天、测试环境 | [会话与消息](guides/session.md) |
| 固定会话列表、顶部条、底部动作、各种输入区 | [场景](guides/scenarios.md) |
| 设置值、主题、设置页面 | [设置](guides/settings.md) |
| 官方业务、账号、隐私、设置索引、快捷动作 | [官方业务](guides/official.md) |
| 下载、消息附件、编号提及、设置迁移、不切换界面的播放控制 | [业务指令](guides/business.md) |
| 过滤规则、文本、翻译、表情包、转发、消息截图 | [定制业务](guides/features.md) |
| 隐身模式 | [隐身模式](guides/ghost.md) |
| 消息留档 | [存储](guides/storage.md) |
| 控件树、点击、输入、按键、悬停、滚动 | [控件](guides/controls.md) |
| 主窗口、菜单与通知截图 | [截图](guides/screenshot.md) |

`cli.py --help` 查看客户端清单，`app.help` 查看当前构建的服务端清单。

## 数据与进程

登录页点击“进入假会话”或执行 `session.fake`，会自动创建固定会话与消息，并显示开发者功能。
每次新建假会话都会初始化，无需额外导入场景或环境变量。

需要隔离测试数据时使用独立配置：先 `app.stop`，再 `app.start --profile scenarios + session.fake`，仍串行运行原开发版。
数据保存在 `build/debug-profiles/scenarios/`，CLI 会记住配置，后续调用无需重复指定。
独立配置使用 `-debugprofile`，跳过链接协议注册，并禁用原生通知及其注册表操作。
`-testagent` 专供自动测试，会拦截外部链接；日常调试不使用这个标记。
不要在独立配置中放置官方测试运行器的 `testing` 标记，避免额外场景自动运行。
配置名限 1 至 48 个小写字母、数字、下划线或连字符，首位为字母或数字；`default` 表示原默认目录。

CLI 会核对已有进程的可执行文件路径和工作目录。恢复原调试配置时先退出应用，
再 `app.start --profile default`。验证其它工作树时用 `AYUGRAM_DEBUG_ROOT` 指定根目录。

停止应用统一使用 `app.stop`：先请求正常退出，必要时仅结束经路径校验的本仓库调试进程。
端口被其它应用占用时保留现场并报告路径、进程编号和错误。正式安装版有独立数据与进程。

## 构建与验证

C++ 修改后先 `app.stop`，再运行 `python scripts/build.py --dev --jobs 12`。
产物位于 `build/AstraGram-v<版本>-win-x64-dev/`，默认不含 pdb；需要符号时加 `--pdb`，切换会全量重编。
构建成功后 `app.start` 启动已有产物，最多等待 60 秒；单条服务端指令超时为 180 秒。

假会话身份与场景消息在重启后消失，重新进入假会话即恢复场景；设置、草稿与留档仍写入调试配置。
崩溃时先查看当前工作目录的 `crash.log`；默认只有模块内偏移，`--pdb` 构建才能定位文件与行号。

按当前任务临时组合指令进行实测，操作前核对账号与目录，验证后恢复临时修改的设置和草稿。
技能只维护命令客户端与操作指南，不收录验证、回归或审计脚本。方案和阶段记录按 [项目记录规范](../../../AGENTS.md#本地方案与过程记录) 按需保存；
临时辅助脚本及附件放在 `build/record/<任务标识>-<阶段号>/`，工具已有固定输出路径的文件保持原位置。

## 维护

1. 新增、修改或删除业务逻辑时，同步核对调试覆盖，保证业务动作有指令控制、业务状态有指令查询；发现缺口及时补齐，废弃入口同步清理，不能只改界面。
2. 指令复用正式业务接口，及时同步参数、结果和状态变化；异步操作区分已提交、进行中、成功、失败和取消，不把接收请求当成完成。
3. 同步服务端注册、CLI 参数和对应指南；新增领域更新指南索引，通过 `app.help` 与 `cli.py --help` 核对指令入口，并按本次任务实测。
4. `.codex/skills/app-debug/` 和 `.claude/skills/app-debug/` 保持相同内容。
5. 调试源码包在 `_DEBUG` 内，新增文件登记 CMake，并检查 Git 忽略规则。
6. 订阅绑定控件生命周期，长期持有的 Qt 对象随应用退出清理。
7. 遍历 JSON 的 `items()` 前把 JSON 存入具名变量，确保代理引用有效。
8. 命令错误信息统一使用英文；界面文案使用翻译键，内部控件标识保持固定。
