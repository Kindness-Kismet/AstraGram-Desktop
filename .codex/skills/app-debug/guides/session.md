# 会话与消息

## `session.fake [userId]`

在没有当前会话时创建本地假账号，默认编号 `999999999`。用于检查布局、主题、设置和本地消息渲染；
本地编辑／删除事件可用 `message.edit-local/delete-local` 验证。`app.info.fakeSession` 可核对身份。

进入时自动生成固定会话、消息并显示开发者功能，登录页按钮与命令共用同一入口。
自动测试先 `app.ensure --profile scenarios`，此后无需环境变量或额外导入命令。
假账号身份和场景消息在重启后消失；设置、草稿与留档仍走正常保存流程，因此使用独立目录。

后台授权请求会失败；假会话临时替换全局失败回调，保持界面可用，日志会记录授权失败。

## `session.test-mode`

切到官方测试数据中心，再在登录界面用测试号登录。拿到的是**真实会话**，有真实消息、真实的
删除和编辑事件。

等价于在登录界面输入 `testmode`（`settings_codes.cpp:147`）。再次执行可切回生产环境。

测试服的账号与生产环境完全隔离，登录方式见 Telegram 官方 API 文档的 test accounts 一节。

前置条件：`domain` 已启动、无会话、且恰好只有一个账号。`addActivated` 会新建账号，多账号时
切换会留下多余的空账号，官方 `testmode` 也是这个前提。

## `message.fake <text> [--peer <peerId>] [--from <userId>] [--blocked] [--shadow-ban]`

往假会话插入一条本地构造的文本消息，走 `addNewMessage` 官方路径，
渲染行为与真实消息一致。只存在内存，重启即消失，不触发任何网络请求。
用于无网络观测渲染与隐藏逻辑（如被拉黑/影子拉黑用户的消息隐藏）。

- `--peer` 取 `scenario.list` 或 `chat.list` 的会话编号，缺省为 Saved Messages。
- `--from` 缺省时发送者是 self（表现为 out 消息，不会被隐藏链过滤）。
- `--blocked` 走 `hideFromBlocked` 真拉黑路线：需同时开 `filtersEnabled` + `hideFromBlocked`。
- `--shadow-ban` 走影子拉黑路线：只需 `filtersEnabled`，名单可用 `settings.set` 独立维护。

## `chat.list [filter]`

列出当前账号已加载对话的 peerId 与名称，`filter` 为名称子串、忽略大小写。
`message.send` / `chat.open` 的 peerId 均以本指令输出为准。

peerId 是内部 64 位标识（高位带类型掩码，不是客户端里的 -100 拼接格式），
原样传入即可。结果覆盖已加载的对话；其它对话先打开再查询。

## `message.send <peerId> <text|--file path>`

真实发送文本消息，走官方发送链路（`session->api().sendMessage`），发送侧钩子
（如 auto_space）均生效。需要已登录的真实会话，与 fake-session 不兼容。
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

## 验证忽略用户（拉黑 / 影子拉黑）

组合用法：`session.fake` 创建会话，`message.fake` 的 `--from <id>` 配合
`--blocked` / `--shadow-ban` 构造被隐藏的消息，`settings.set filtersEnabled true`
（真拉黑路线还需 `hideFromBlocked`）开启过滤，`chat.open` + `screenshot.take`
观测效果，关闭开关对比消息是否恢复。

## 该用哪个

| 目标 | 用 |
|---|---|
| 看界面、改设置、验证 Debug 入口 | `session.fake` |
| 测已删除消息、编辑历史、幽灵模式、过滤器 | `session.test-mode` + 测试号登录 |
| 发文本验证发送链路 | `chat.list` 定位 + `message.send` |

## 默认背景验证

`theme.reset-background` 重置当前主题的默认背景，返回 `themePath`、`isPattern`、`intensity`、`imageWidth` 和 `imageHeight`，用于核对截图中的背景是否加载正确。
