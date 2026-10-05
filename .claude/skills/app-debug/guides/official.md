# 官方设置与业务操作

官方界面入口采用原生设置索引和控件事件。命令存在不代表服务端操作已通过验证；
本轮自动验证仅使用假会话，本地状态与真实服务端状态要区分。

## 页面和值

`page.list [filter]` 从应用实时设置索引返回编号、标题、页面路径和勾选状态。
`page.open <id>` 打开该入口所在页面并定位控件；`settings` 和 `extras` 分别打开官方设置和定制设置主页。
索引随当前账号、功能开关和平台变化。

```bash
python .codex/skills/app-debug/scripts/cli.py page.list
python .codex/skills/app-debug/scripts/cli.py page.open settings + control.list
python .codex/skills/app-debug/scripts/cli.py control.get '<控件编号或名称>'
```

通知、隐私、账号资料、安全、设备、下载与存储、代理、聊天文件夹、商业功能、快捷回复、
付费功能等页面都通过这份索引定位。弹窗与动态选项使用 `control.list/get/set/click`；
服务器返回的选项只有在对应数据加载后才出现。密码、支付、加入、发送和账号退出等动作保留原有界面流程。

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

- `session.list`：本地账号、激活状态和假会话标记。
- `session.activate <index>`：激活指定本地账号。
- `session.peer-settings <peerId> [key value]`：查询／修改会话级配置。
- `session.thread-settings <peerId> <topicId> <subpeerId> [key value]`：查询／修改话题及子会话配置；普通聊天后两个编号填 0。
- `privacy.get`：读取已加载的全局隐私状态，假会话返回 `localOnly: true`。
- `privacy.set <key> <value>`：提交全局隐私设置。
- `privacy.reload`：重新请求服务端隐私设置。

隐私键包括 `archiveAndMute`、`unarchiveOnNewMessage`、`hideReadTime`、
`requirePremium`、`chargeStars`、`disallowedGiftTypes`、`paidReactionShownPeer`。
提交与重新加载要求真实登录；返回已提交不等于服务器确认保存成功。

会话级键包括 `autoDownload`（0 默认、1 允许、2 禁止）、
`subsectionTabsMode`、`groupStickersHidden`、`groupEmojiHidden`。
话题级键为 `hiddenPinnedMessageId`、`ringtoneVolume`；默认通知音量用 `session.defaultRingtoneVolume.<0|1|2>`。

操作系统集成、真实网络请求和真实账号业务不在假会话验证范围内；不要为了测试修改系统配置。
