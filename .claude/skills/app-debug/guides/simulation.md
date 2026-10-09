# 模拟模式

## 进入和操作

仅开发版提供。登录页点击“模拟模式”或执行 `simulation.enter [userId]`，默认用户编号 `999999999`。
进入时自动构造 27 个会话、3 个话题和 3 个收藏来源，无需下载素材。
固定场景按原生会话标签分类；使用 `simulation.open` 定位，使用 `simulation.trigger` 触发临时提示。

```bash
python .codex/skills/app-debug/scripts/cli.py app.start --profile simulation + simulation.enter
python .codex/skills/app-debug/scripts/cli.py simulation.list
python .codex/skills/app-debug/scripts/cli.py simulation.open group + screenshot.take
```

`app.info` 的 `simulationMode: true` 确认身份。已有真实会话时拒绝进入；已在模拟模式中再次调用保持现场。
重启后身份和场景消息消失，重新进入即可重建。设置和草稿仍保存在独立配置中；模拟编辑、删除不写入消息留档。
后台授权请求不能成功，因此模拟模式用于本地数据、控件和布局验证；联网业务使用已授权的测试账号。

## 固定场景

原生会话分组按私聊、群组、频道、机器人、话题、收藏夹、归档排列，尊重“隐藏全部对话”设置。
`simulation.list` 返回每项的 `key`、`name`、`category`、`categoryName`、`features` 和 `peerId`。
进入后还返回 `state`，查询实际未读、静音、归档、置顶、草稿、分类归属、话题状态及收藏来源数量。
编号供 `message.list/inspect`、`chat.draft` 等查询使用，不手写内部会话编号。

| 分类 | 键名 | 内容 |
|---|---|---|
| 私聊 | `private` | 对话置顶、普通、引用、已编辑、已删除、一天自动删除、禁止转发、已读、富文本与链接 |
| 私聊 | `draft` | 未读、静音、多行草稿 |
| 私聊 | `contact` | 陌生人的添加和屏蔽提示 |
| 私聊 | `blocked` | 已屏蔽用户的底部动作 |
| 私聊 | `deleted-account` | 已注销账号 |
| 私聊 | `business` | 商业机器人管理提示 |
| 私聊 | `paid` | 付费消息提示 |
| 群组 | `group` | 讨论、置顶和消息状态，图片、相册、文件、联系人、投票、静态贴纸 |
| 群组 | `restricted` | 发送限制 |
| 群组 | `requests` | 管理权限、加入申请和置顶 |
| 群组 | `slow` | 慢速模式；每次打开刷新倒计时 |
| 群组 | `protected-group` | 内容保护和置顶 |
| 群组 | `send-as` | 本人与频道两种发送身份，默认选中频道，可通过输入框按钮在本地切换 |
| 频道 | `channel` | 公告、置顶、浏览次数、通知按钮 |
| 频道 | `discussion` | 评论入口、关联讨论群和置顶 |
| 频道 | `join` | 未加入频道的加入按钮；对话置顶使其常驻固定列表 |
| 频道 | `translate` | 外语消息、翻译与置顶组合 |
| 频道 | `protected-channel` | 内容保护 |
| 机器人 | `bot` | 启动按钮；每次打开恢复启动参数 |
| 机器人 | `keyboard` | 回复键盘和 `/help` 命令补全 |
| 机器人 | `inline-bot` | 消息内联按钮 |
| 机器人 | `sponsored` | 本地广告样本 |
| 话题 | `topic` | 常规与置顶、未读与草稿、已关闭三个真实话题 |
| 收藏夹 | `saved` | 本人、私聊、频道来源，置顶、一小时后的提醒与非音视频内容 |
| 归档 | `archived-private` | 未读私聊 |
| 归档 | `archived-group` | 静音群组 |
| 归档 | `archived-channel` | 置顶频道 |

`simulation.open <key> [--view <view>] [--input keep|empty|reply|edit]` 打开场景。
打开时切换到对应的会话分类，并退出上一场景的归档或话题导航。
默认 `main` 使用主聊天；话题会先在左栏展开话题列表，再打开常规话题。

| 视图 | 适用范围 |
|---|---|
| `alternate` | 另一套聊天组件 |
| `scheduled` | 计划消息；`saved` 已有提醒样本 |
| `shortcuts` | 本地快捷回复输入区 |
| `pinned` | 含置顶消息的场景 |
| `actions` | 有管理权限的 `requests`；没有服务器日志，只显示分区外框 |
| `topic-unread` / `topic-closed` | `topic` 的未读或已关闭话题 |
| `source-self` / `source-private` / `source-channel` | `saved` 的三个收藏来源 |

`--input` 只用于私聊和话题的聊天视图：`keep` 保留草稿，`empty` 清空，`reply` 安装回复草稿，`edit` 安装编辑草稿。
未读话题只有来信，不能安装编辑草稿；已关闭话题只允许保留状态。
归档分组用于分类浏览；`chat.open-archive` 打开真正的归档列表，返回时点击界面可见的返回按钮。

`send-as` 同时支持 `main` 与 `alternate`，使用 `showSendAsButtonInMessageField` 检查身份按钮的显示、隐藏及输入区占位。
`simulation.list` 中的 `sendAsCount` 和 `sendAsPeerId` 报告可选身份数量与实际选中身份；隐藏按钮应保留后者，切换身份不请求服务器。

## 临时触发

`simulation.trigger [list|key]` 查询清单或触发指定场景，每次调用替换上一组列表提示。
`simulation.clear` 或 `simulation.trigger none` 清除列表提示、恢复固定论坛状态。
提示按钮只显示本地反馈，更新提示不会重启或安装程序。

| 类型 | 键名 |
|---|---|
| 生日、头像 | `birthday-setup`、`birthday-contact`、`birthday-contacts`、`userpic` |
| 会员、星币 | `premium-annual`、`premium-upgrade`、`premium-restore`、`premium-grace`、`credits` |
| 推广、拍卖 | `custom`、`auction`、`auction-outbid` |
| 登录、社区 | `auth`、`auth-multiple`、`community-requests`、`community-add` |
| 搜索 | `search-id`、`search-global`、`search-loading`、`search-posts`、`search-messages` |
| 下载条 | `download`、`download-done` |
| 列表加载 | `more-chats`、`load-more`、`loading` |
| 更新、冻结 | `update`、`frozen` |
| 话题提示 | `forum-requests`、`forum-report`、`forum-unarchive`、`forum-all` |
| 组合提示 | `stack` |
| 自动删除 | `delete-countdown`：在 `private` 插入 15 秒后删除的消息，返回编号和到期时间 |

短消息倒计时独立运行，清除列表提示不会取消它。
切换材质、主题、强调色和窗口宽度后可继续查看。非默认材质的列表提示条沿用透明背景，无边框或阴影。

## 补充消息和下载

`simulation.message <text> [--peer <peerId>] [--from <userId>] [--blocked] [--shadow-ban]`
向指定会话插入本地消息，默认收藏夹、自己发送。屏蔽标记须配合 `--from`，自己发送的消息不进入隐藏链。
`--blocked` 配合 `filtersEnabled` 与 `hideFromBlocked`；`--shadow-ban` 配合 `filtersEnabled`。

图片参数：`--sticker <imagePath>` 生成不超过 512 像素的静态贴纸；`--photo <imagePath>` 生成不超过 2048 像素的照片。
两者互斥。照片可配合 `--group <positiveId>` 构造相册，同一对话内使用相同编号。
图片只保存在内存中，不上传。

`simulation.download <path> <loading|done>` 将本地文件加入下载管理器；拒绝音视频素材。
`simulation.download-progress <peerId> <messageId> <bytes|done>` 推进模拟进度。
这类事件用于检查下载列表、分类和进度，真实下载验证见[业务指令](business.md)。

## 验证入口

先用 `simulation.list`、`message.list/inspect`、`chat.draft` 核对状态，再截图观察布局。
`message.inspect` 返回实际删除、置顶、引用、话题、评论和 `ttlDestroyAt`；短倒计时到期后再次查询存在状态。
模拟删除和编辑不得新增 `storage.deleted/edits` 记录。留档数据库独立验证使用 `storage.verify-archive`。
固定场景的原生按钮仍保留业务路径，发送、加入、付费等联网动作不能作为本地模拟的成功证据。
