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
| `window.maximize` | `true\|false` | 最大化或还原窗口。 |

`app.quit` 一般不直接调，`app.stop` 内部先发它。

`app.info` 返回字段：`version` / `versionCode` / `configuration` / `workingDir` / `debugLogs` / `hasSession` / `hasWindow` / `fakeSession` / `userId`（已登录时）。

`app.check-update` 只触发不等待：下载与验签是异步的，结果看工作目录下 `tupdates/`
保存的文件和应用日志。更新被禁用时返回 `updater is disabled`。

`app.update-info` 用来定位更新检查会打到哪个地址：`tdata/prefix` 里写的内容与
`resolvedPrefix` 不一致时，说明前缀在运行时被固定值覆写过，改文件不会生效。

`window.resize` 与 `window.maximize` 用来把窗口摆到指定状态，验证依赖窗口
宽度的行为（宽屏布局、栏位折叠）。尺寸是 Qt 逻辑像素，与 `control.list` 的几何同一
坐标系；最大化状态下 `resize` 不生效，指令会先还原窗口再设尺寸。

```bash
python .claude/skills/app-debug/scripts/cli.py window.resize 1300 900 + screenshot.take
python .claude/skills/app-debug/scripts/cli.py window.maximize true + screenshot.take
```

`player.state` 查询当前音乐的 `messageId`、播放状态 `playing`、位置 `position`、长度 `length`、时间单位 `frequency` 和循环模式 `repeat`（0 关闭、1 单曲、2 列表）。连续采样可验证自然结束后的重播，控制操作仍通过播放器控件执行。

## CLI 本地指令

不进服务端，由 CLI 自己完成。

| 指令 | 参数 | 作用 |
|---|---|---|
| `app.ensure` | 空 | 检查 Debug 应用是否运行，未运行时启动并等待端口就绪（最多 60 秒）。 |
| `app.restart` | 空 | 先停止再启动，等到端口就绪。改完 C++ 重新编译后用它启动新构建。 |
| `app.stop` | 空 | 校验端口 PID 的可执行文件路径必须匹配本仓库 `build/AstraGram-v*-win-x64-dev/AstraGram.exe`，再让应用自行退出；失败才回落到强制结束。端口未建立时只枚举路径匹配同一形态的进程。 |

```bash
python .claude/skills/app-debug/scripts/cli.py app.ensure
# 应用已就绪。
```

单条指令的读超时是 180 秒，长操作不会被 CLI 提前中断。PID 路径不匹配时直接报错拒绝操作，不会错误结束正式安装版。

## 崩溃排查

Debug 构建装了未处理异常过滤器：进程崩溃时把异常码与符号化调用栈写进
工作目录 `crash.log`（工作目录见 `app.info` 的 workingDir）。

```bash
python .claude/skills/app-debug/scripts/cli.py page.open ayu   # 复现操作
cat build/AstraGram-v*-win-x64-dev/crash.log                   # 读调用栈定位
```

复现崩溃后先查看 crash.log 的调用栈，定位文件与行号。

## 独立数据目录

先退出当前调试应用，再选择独立配置。CLI 会记住选择，后续无需重复指定；
数据保存至 `build/debug-profiles/scenarios/`。

```bash
python .codex/skills/app-debug/scripts/cli.py app.stop
python .codex/skills/app-debug/scripts/cli.py app.ensure --profile scenarios
python .codex/skills/app-debug/scripts/cli.py session.fake
```

配置名允许 1 至 48 个小写字母、数字、下划线或连字符，首位为字母或数字。
端口已有进程时，CLI 核对可执行路径和工作目录；目录不符会报告并停止执行。
恢复原配置：先 `app.stop`，再 `app.ensure --profile default`。
