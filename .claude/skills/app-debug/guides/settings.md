# 设置读写

## 主题创建器

`theme.editor.open` 在独立模拟配置中沿正式入口打开主题创建器。
`theme.editor.state` 查询尺寸、窄窗详情状态、选中键和值、实际颜色、可见数量和修改数量。
`theme.editor.select <颜色键名>` 选择详情；`theme.editor.set <颜色键名> <#rrggbb[aa]|引用键名>`
调用正式编辑接口，修改仅允许在独立模拟配置执行。界面输入、透明度、返回、还原和保存按钮使用控件指令验证。
`theme.editor.import <文件路径>` 在独立模拟配置中复用正式导入链路，接受主题或配色文件。

消息定位入口在“外观 → 主题设置 → 消息定位”，页面索引为 `chat/message-locate`。
`messageLocateStyle` 为 0 随形断线、1 跳动定位；`messageLocateColor` 留空跟随主题，或使用 `#RRGGBB`。
`messageLocateDuration` 为 800 至 4000 毫秒；`messageLocateStroke` 为 1 至 4，`messageLocateCorner` 为 12 至 36，
`messageLocatePinSize` 为 12 至 32，`messageLocateBounce` 为 0 至 12，尺寸均为界面缩放前的像素。
通过 `chat.open <peerId> <msgId>` 触发正式定位；设置弹窗的 `messageLocate/preview` 共用正式消息与动画实现。

`settings.keys [prefix]` 列出已注册的完整键名；`settings.schema [prefix]` 返回当前值、类型与可写状态。
`settings.dump [group]` 导出全部或指定分组；`settings.get <key>` 读取值，也可读取整个分组。

| 键名范围 | 内容 |
|---|---|
| 无前缀，如 `windowMaterial` | 本项目定制设置 |
| `ghost.*` | 当前账号实际使用的隐身模式设置 |
| `ghostModeSettings.<userId>.*` | 指定账号的隐身模式配置 |
| `streamer.enabled` | 当前账号的主播模式设置 |
| `streamerModeSettings.<userId>.enabled` | 指定账号的主播模式设置 |
| `messageShotSettings.*` | 消息截图与截图主题 |
| `core.*` | 官方应用设置：通知、音视频、输入、翻译、窗口等 |
| `session.*` | 当前账号设置 |
| `session.autoDownload.<source>.<type>` | 自动下载字节上限 |
| `proxy.*` | 应用代理、列表、选中项与轮换 |
| `experimental.*` | 实验设置页中的选项，结构查询会注明平台可用性与重启要求 |
| `shadowBanIds` | 本地隐藏名单数组 |

`settings.set <key> <value>` 修改单个完整键名，返回实际生效值。写入调用原有设置访问器，
保留通知、缓存更新和保存流程；不会重建已有的隐身模式对象。

`hideGroupCalls` 控制频道直播和群语音、视频通话入口，默认关闭；切换后即时刷新顶栏、资料页、通话提示栏、通话列表和会话头像角标，菜单重新打开时同步生效。

布尔值使用 `true/false`，数字、数组和结构体使用 JSON；字符串原样保留，包括首尾空格。
二进制设置使用 Base64 字符串。字段不存在、类型不匹配和非法枚举返回英文错误。
嵌套对象先用 `settings.keys` 找到叶子键；代理等自身为结构体的键接受完整 JSON 对象。

```bash
python .codex/skills/app-debug/scripts/cli.py settings.set ghost.sendReadMessages false
python .codex/skills/app-debug/scripts/cli.py settings.set messageShotSettings.showDate true
python .codex/skills/app-debug/scripts/cli.py settings.set shadowBanIds '[123456789]'
python .codex/skills/app-debug/scripts/cli.py settings.get core.songVolume
python .codex/skills/app-debug/scripts/cli.py settings.keys session.autoDownload
```

自动下载 source：0 私聊、1 群组、2 频道；type：0 图片、1 视频自动播放、2 语音、
3 视频消息自动播放、4 音乐、5 动图自动播放、6 文件。0 字节表示关闭。

`session.peer-settings <peerId> [key value]` 管理会话级自动下载覆盖、标签模式和群组表情区域。
`privacy.get/set/reload` 管理服务端全局隐私配置，详见[官方业务](official.md)。

半径、倍率、音量等数值超出支持范围会返回错误；部分字体、窗口和渲染配置需重开页面或重启应用。
`core.configScale` 保存界面缩放，重启后生效；`core.autoUpdate` 控制应用更新器。
`core.soundOverrides` 接受六种提示音路径的完整对象，可先查询当前值再修改。
语言、主题文件、密码、账号安全及服务器动态选项经 `page.list/open` 与控件命令操作，保留原生确认流程。
已跳转到定制设置的实验选项使用对应定制键，不保留重复入口。
`theme.set <dark|light>` 切换主题并解除跟随系统；`theme.reset-background` 恢复默认壁纸。

下载入口设置键为 `showDownloadsButtonInHeader`，默认显示；旧配置文件中的 `showDownloadsButtonInSearch` 自动迁移。

横向标签固定为细下划线，跟随主题强调色；不再提供款式设置，不影响竖向导航或数值刻度。

自绘通知样式为 `notificationStyle`：0 卡片、1 叠放；切换时清空正在显示的通知，之后的通知按新样式出现。

应用名称为 `customAppName`：修改主界面左上角和窗口标题，即时生效；合并连续空白，留空恢复默认名称。

频道身份按钮为 `showSendAsButtonInMessageField`，归入“聊天 → 输入框元素”，默认显示。
开关即时改变按钮和输入区占位，不改变当前发送身份；使用 `simulation.open send-as` 及 `--view alternate` 验证两套输入区。

附件菜单各项归入“聊天 → 附件菜单项”，默认全部显示：`showPhotoInAttachMenu`、`showFileInAttachMenu`、`showPollInAttachMenu`、`showTodoListInAttachMenu`、`showArticleInAttachMenu`、`showLocationInAttachMenu`、`showMusicInAttachMenu`，以及“录制消息”分组 `showRecordMessageInAttachMenu`；旧配置中的 `showMicrophoneButtonInMessageField` 自动迁移。开关只隐藏菜单项，聊天权限不允许的项仍按官方逻辑隐藏。
录制分组每次打开菜单都从折叠状态开始；聊天权限、录制设备、系统授权或通话占用不满足时，对应录制项置灰。

频道邀请开关为 `showCommunityChannelInvite`，默认开启；确认或“不再显示”会关闭，取消只跳过本次启动。
邀请在登录并进入主界面后显示，每次启动最多一次；恢复开关后重启即可再次验证，弹窗标识为 `communityChannel/invite`。

侧边栏顺序为 `drawerOrder`，接受选项标识数组；查询可核对菜单和外观设置页的共同顺序，空数组恢复默认。
两处均长按后拖动排序；使用 `control.mouse` 的 `press`、`move`、`release` 分别投递，间隔超过系统拖动等待时间。
