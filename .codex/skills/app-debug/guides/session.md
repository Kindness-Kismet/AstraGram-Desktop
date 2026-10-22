# 会话与消息

## 离线缓存

- `cache.state [peerId]`：查询恢复的会话数、缓存命中次数、当前分组、连接状态；指定会话时附带已显示消息范围。
- `messageDataHits` 表示引用原文等单条消息从缓存恢复的次数；`rendered` 记录会话列表首次与最近一次实际绘制的标签和行数。
- `tabs` 和 `archiveRows` 返回标签列表和归档的缓存行数；指定会话时，`latestSliceMessages`、`latestSliceTotal`、`latestSliceSkippedAfter` 核对新历史视图的最新切片，不导出正文。
- 当前新聊天视图可见时，`view` 返回实际列表是否为空、可见范围首尾消息编号、滚动坐标及底部加载边界；坐标落在列表留白时编号为 0，切片指标本身不代表界面已显示。
- `cache.read <peerId> [offsetId] [addOffset] [limit]`：只查询缓存，返回任务编号；用 `job.status` 获取消息编号和内容摘要，不导出消息正文。
- `cache.flush`：安排保存缓存，返回不代表磁盘写入已完成；正常退出会等待存储完成。
- `cache.trace-history <peerId|off>`：跟踪当前账号指定会话的读写、查询参数与旧/新视图网络交付；只保留最近 120 条编号和数量，不含正文。切换目标清空旧事件，重启后关闭。
- `cache.history-info <peerId> [topicRootId sublistPeer]`：异步返回实际连续区间、区间内数量、当前及缓存顶部、临时摘要编号和诊断事件；用 `job.status` 读取结果，不建立聊天视图或发送网络请求。

验证离线重启时，先联网浏览需要测试的会话，再运行 `app.restart --offline --background`。
核对 `app.info.offline`、`cache.state` 和截图；用不带 `--offline` 的重启恢复联网。

本地模拟身份、固定场景和消息注入统一见[模拟模式](simulation.md)。

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

## `chat.open [peerId] [msgId]`

打开指定对话并清空导航栈；参数取 `chat.list` 输出的 peerId，缺省 self（Saved Messages）。配合 `screenshot.take` 做 UI 观测，
免去找列表项点击的不稳定。`msgId` 定位到指定消息，缺省为对话末尾；目标尚未加载时走首次加载流程。

## `chat.media <peerId> <photo|video|file|music|link|gif|voice> [--side]`

直接打开对话资料里的共享媒体分类，免去先开资料页再点入口。默认以弹层打开；`--side` 在窗口够宽时放进第三栏侧栏，
窗口不够宽时仍为弹层。收藏夹、收藏来源、回复会话与普通对话的照片、文件等分类统一使用画廊布局；投票使用独立消息列表。

## `chat.open-archive`

直接打开归档文件夹，不走抽屉入口。用于单独观测归档页本身（列表渲染、留档消息），
抽屉入口的行为另有 `menu.archiveChats` 可点。

## `chat.history-stats <msgId>... [--peer <peerId>]`

查询指定消息是否存在、是否为自己发送、是否隐藏及是否有主视图，默认查询收藏夹。
`--peer` 使用 `chat.list` 或 `simulation.list` 返回的对话编号；`mediaSize` 返回主视图媒体的实际布局宽高，无媒体时为空。
`copyText` 是右键“复制文本”写入剪贴板的纯文本，`richPageText` 是富消息主视图排版后的全文，无主视图时为空；核对复制结果不必读写系统剪贴板。

模拟模式中的群菜单“删除自己所有消息”会显示 5 秒撤销倒计时，到期只删除本地自己的消息。
可在倒计时前后用本指令核对，其他成员消息保留，不会发送删除请求。

## 该用哪个

| 目标 | 用 |
|---|---|
| 看界面、改设置、验证 Debug 入口 | `simulation.enter` |
| 发文本验证发送链路 | `chat.list` 定位 + `message.send` |

## 默认背景验证

`theme.reset-background` 重置当前主题的默认背景，返回 `themePath`、`isPattern`、`intensity`、`imageWidth` 和 `imageHeight`，用于核对截图中的背景是否加载正确。


## 媒体缓存管理

- `cache.media-stats`：异步查询当前账号分类容量、未归属容量与各聊天大小；结果通过 `job.status` 查询。
- `cache.media-clear <all|类别[,类别...]> [peerId]`：异步清理本机缓存；类别为 `images`、`stickers`、`documents`、`avatars`、`other`、`messages`，`all` 只包含媒体。
- `cache.media-retention <private|groups|channels|stories> [days]`：查询或保存分类型保留天数，可选 0、1、2、7、30；0 表示永久。

媒体与消息分别选择。清理不删除云端消息、消息留档或手动保存文件；其它聊天仍引用的共享媒体保留。
清理期间不能重复提交，实际完成与释放容量以异步结果为准。
