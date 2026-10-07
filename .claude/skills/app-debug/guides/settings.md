# 设置读写

`settings.keys [prefix]` 列出已注册的完整键名；`settings.schema [prefix]` 返回当前值、类型与可写状态。
`settings.dump [group]` 导出全部或指定分组；`settings.get <key>` 读取值，也可读取整个分组。

| 键名范围 | 内容 |
|---|---|
| 无前缀，如 `streamerMode` | 本项目定制设置 |
| `ghost.*` | 当前账号实际使用的隐身模式设置 |
| `ghostModeSettings.<userId>.*` | 指定账号的隐身模式配置，0 为全局 |
| `messageShotSettings.*` | 消息截图与截图主题 |
| `core.*` | 官方应用设置：通知、音视频、输入、翻译、窗口等 |
| `session.*` | 当前账号设置 |
| `session.autoDownload.<source>.<type>` | 自动下载字节上限 |
| `proxy.*` | 应用代理、列表、选中项与轮换 |
| `experimental.*` | 实验设置页中的选项，结构查询会注明平台可用性与重启要求 |
| `shadowBanIds` | 本地隐藏名单数组 |

`settings.set <key> <value>` 修改单个完整键名，返回实际生效值。写入调用原有设置访问器，
保留通知、缓存更新和保存流程；不会重建已有的隐身模式对象。

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

频道邀请开关为 `showCommunityChannelInvite`，默认开启；确认或“不再显示”会关闭，取消只跳过本次启动。
邀请在登录并进入主界面后显示，每次启动最多一次；恢复开关后重启即可再次验证，弹窗标识为 `communityChannel/invite`。
