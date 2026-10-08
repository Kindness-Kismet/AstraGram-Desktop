# 会话与消息

本地模拟身份、固定场景和消息注入统一见[模拟模式](simulation.md)。

## `session.test-mode`

切到官方测试数据中心，再在登录界面用测试号登录。拿到的是**真实会话**，有真实消息、真实的
删除和编辑事件。

等价于在登录界面输入 `testmode`（`settings_codes.cpp:147`）。再次执行可切回生产环境。

测试服的账号与生产环境完全隔离，登录方式见 Telegram 官方 API 文档的 test accounts 一节。

前置条件：`domain` 已启动、无会话、且恰好只有一个账号。`addActivated` 会新建账号，多账号时
切换会留下多余的空账号，官方 `testmode` 也是这个前提。

## `notification.test [text] [--peer <userId>]`

让模拟用户发一条消息，触发真实的通知链路。模拟模式的对话没有服务端下发的通知设置，
通知会被判为“未知”而跳过；本指令先在本地把该用户与用户默认设置标记为已知、未静音，
再走 `simulation.message`。`--peer` 缺省为模拟用户 `830000001`，`text` 缺省为 `Debug 通知测试`。

- 需要模拟模式；应用自带通知可在独立配置中验证，原生通知需使用默认配置。
- 走 Windows 系统通知先 `settings.set core.nativeNotifications true`；设为 `false` 则用应用自带通知。
- Windows 开启时注册当前构建的通知身份；关闭时清理通知并取消注册，普通退出保留注册。
- 通知设置页的“重置通知注册”会关闭原生通知，并清理同一构建类型的旧路径登记；正式版、开发版互不影响。
- 该对话正打开且窗口在前台时，消息会立刻变成已读而不再通知，验证时打开别的对话或让窗口失焦。

## `notification.hover <on|off>`

给所有自绘通知窗口发合成的进入／离开事件，模拟鼠标悬停，不移动真实光标。`on` 之后右下角的回复文字按钮和右上角的关闭按钮淡入，
`off` 恢复；配合 `screenshot.take --notification` 检查悬停态。悬停期间通知不会自动消失。

## `notification.click <reply|close>`

给所有自绘通知中可见的按钮投递合成点击（按真实的进入、按下、抬起事件），`reply` 展开回复输入框，
`close` 关闭该通知。先用 `notification.hover on` 显示按钮；展开回复后仍可关闭通知，隐藏或不可回复时拒绝点击回复按钮。

## `chat.list [filter]`

列出当前账号已加载对话的 peerId 与名称，`filter` 为名称子串、忽略大小写。
`message.send` / `chat.open` 的 peerId 均以本指令输出为准。

peerId 是内部 64 位标识（高位带类型掩码，不是客户端里的 -100 拼接格式），
原样传入即可。结果覆盖已加载的对话；其它对话先打开再查询。

## `message.send <peerId> <text|--file path>`

真实发送文本消息，走官方发送链路（`session->api().sendMessage`），发送侧钩子
（如 auto_space）均生效。需要已登录的真实会话。
仅向自己掌控的测试对话发送，避免打扰真实联系人。

- `--file` 按 UTF-8 发送文件原文，保留换行与引号；相对路径从 CLI 当前目录解析。
- 返回只表示请求已提交，服务器确认是异步的，验证效果稍等片刻再 `screenshot.take`。
- 不清除目标对话的草稿，不影响输入框。

## `chat.open [peerId]`

打开指定对话并清空导航栈；参数取 `chat.list` 输出的 peerId，缺省 self（Saved Messages）。配合 `screenshot.take` 做 UI 观测，
免去找列表项点击的不稳定。

## `chat.open-archive`

直接打开归档文件夹，不走抽屉入口。用于单独观测归档页本身（列表渲染、留档消息），
抽屉入口的行为另有 `menu.archiveChats` 可点。

## `chat.history-stats <msgId>... [--peer <peerId>]`

查询指定消息是否存在、是否为自己发送、是否隐藏及是否有主视图，默认查询收藏夹。
`--peer` 使用 `chat.list` 或 `simulation.list` 返回的对话编号；`mediaSize` 返回主视图媒体的实际布局宽高，无媒体时为空。

模拟模式中的群菜单“删除自己所有消息”会显示 5 秒撤销倒计时，到期只删除本地自己的消息。
可在倒计时前后用本指令核对，其他成员消息保留，不会发送删除请求。

## 该用哪个

| 目标 | 用 |
|---|---|
| 看界面、改设置、验证 Debug 入口 | `simulation.enter` |
| 测已删除消息、编辑历史、隐身模式、过滤器 | `session.test-mode` + 测试号登录 |
| 发文本验证发送链路 | `chat.list` 定位 + `message.send` |

## 默认背景验证

`theme.reset-background` 重置当前主题的默认背景，返回 `themePath`、`isPattern`、`intensity`、`imageWidth` 和 `imageHeight`，用于核对截图中的背景是否加载正确。
