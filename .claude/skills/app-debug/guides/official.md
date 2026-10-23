# 官方设置与业务操作

官方界面入口采用原生设置索引和控件事件。命令存在不代表服务端操作已通过验证；
模拟模式只验证本地状态；真实服务端行为通过已授权的测试账号验证。

## 页面和值

`page.list [filter]` 从应用实时设置索引返回编号、标题、页面路径和勾选状态。
`page.sections` 列出可直接打开的页面键名，含通知分类、自动下载分类、商业子页和定制子页；
`flow` 为 true 的是流程入口（本地密码、两步验证、下载列表），只打开入口页，不跳过中间步骤；
未进入搜索索引的页面 `title` 和 `path` 为空。
`page.open <key|id>` 优先按键名匹配，按手动路径先切换左侧分类再进入子页；
不是键名时按 `page.list` 编号打开该入口所在页面并定位控件。
索引随当前账号、功能开关和平台变化。

页面按逻辑身份区分，窗口宽度只改变展示方式。设置区宽度达到 `wideThreshold` 时左右两栏同时显示；
不足时分类列表和内容页各占一页。`page.current` 返回当前页面键名、所属分类、
`settingsLayout`（宽窄、两栏可见性、宽度、阈值）和窗口的单栏／双栏／三栏布局。
没有键名的流程页在 `sectionClass` 给出页面类型。
验证两种布局时先用 `window.resize` 调整窗口：

```bash
python .codex/skills/app-debug/scripts/cli.py page.list
python .codex/skills/app-debug/scripts/cli.py page.open settings + control.list
python .codex/skills/app-debug/scripts/cli.py window.resize 570 900 + page.open business-greeting + page.current
python .codex/skills/app-debug/scripts/cli.py control.get '<控件编号或名称>'
```

通知、隐私、账号资料、安全、设备、下载与存储、代理、聊天文件夹、商业功能、快捷回复、
付费功能等页面都通过这份索引定位。弹窗与动态选项使用 `control.list/get/set/click`；
服务器返回的选项只有在对应数据加载后才出现。密码、支付、加入和账号退出等动作保留原有界面流程；已授权的测试发送使用[业务指令](business.md)。

## 动作

`action.list` 列出官方快捷动作；`action.run <name>` 按当前绑定的按键走 Qt 快捷键匹配，与真实按键一致。
应用窗口不在前台时 Qt 不匹配快捷键，指令直接报错，不会抢占焦点；自动流程切换页面改用 `chat.open`、`page.open`。
涵盖账号／聊天／文件夹切换、收藏夹、联系人、归档、搜索、计划消息、消息发送方式、
录音、视频消息、已读、媒体播放、聊天菜单、管理日志和客服动作。
当前页面没有处理该动作时返回错误，不伪造成功。

自绘内容用 `control.mouse <target> <x> <y> [left|right|double]`，坐标为控件内部逻辑像素。
此命令只向本应用投递事件，不移动系统光标；右键会投递上下文菜单事件。
`control.get` 返回可用的辅助功能动作，`control.action <target> <action>` 触发对应动作。

## 账号与服务端隐私

- `session.list`：本地账号、激活状态和模拟模式标记。
- `session.activate <index>`：激活指定本地账号。
- `session.peer-settings <peerId> [key value]`：查询／修改会话级配置。
- `session.thread-settings <peerId> <topicId> <subpeerId> [key value]`：查询／修改话题及子会话配置；普通聊天后两个编号填 0。
- `privacy.get`：读取已加载的全局隐私状态，模拟模式返回 `localOnly: true`。
- `privacy.set <key> <value>`：提交全局隐私设置。
- `privacy.reload`：重新请求服务端隐私设置。

隐私键包括 `archiveAndMute`、`unarchiveOnNewMessage`、`hideReadTime`、
`requirePremium`、`chargeStars`、`disallowedGiftTypes`、`paidReactionShownPeer`。
提交与重新加载要求真实登录；返回已提交不等于服务器确认保存成功。

会话级键包括 `autoDownload`（0 默认、1 允许、2 禁止）、
`groupStickersHidden`、`groupEmojiHidden`。
话题级键为 `hiddenPinnedMessageId`、`ringtoneVolume`；默认通知音量用 `session.defaultRingtoneVolume.<0|1|2>`。

操作系统集成、真实网络请求和真实账号业务不在模拟模式验证范围内；不要为了测试修改系统配置。
