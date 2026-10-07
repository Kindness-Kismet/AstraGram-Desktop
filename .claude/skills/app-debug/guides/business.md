# 不切换界面的业务指令

先使用业务指令验证数据与行为；只有布局、点击命中和焦点检查才操作窗口。
业务指令不会为了查询切换聊天、弹出文件选择器或抢占焦点；设置、下载与发送仍会产生正常业务效果。

## 下载

| 指令 | 参数 | 结果 |
|---|---|---|
| `downloads.list` | `[all|archives|music|videos|other] [query]` | 当前账号的下载记录、分类、路径、大小、已下载字节及状态，按下载时间倒序。 |
| `downloads.start` | `<peerId> <messageId> <destination>` | 向新建的绝对路径保存附件，返回 `jobId`；父目录必须存在，不覆盖已有文件。 |
| `downloads.cancel` | `<peerId> <messageId>` | 取消指定附件的活动下载。 |
| `downloads.fake` | `<path> <file|song|video|voice> <loading|done>` | 仅假会话：将自建素材加入真实下载管理器，不上传或播放。 |
| `downloads.progress` | `<peerId> <messageId> <bytes|done>` | 仅假会话：推进模拟下载或完成，用于检查分类和列表更新。 |

列表使用下载页面的同一个数据提供器，类型与关键词取交集；查询不会改变页面筛选或选择状态。
`resolved: false` 表示旧下载记录尚未解析完成，此时空列表不是最终结果。只返回当前账号的记录。
`state` 为 `downloading` 或 `completed`；真实下载进度来自下载管理器，不用文件是否出现来推断完成。

下载任务通过 `job.status` 查看最终成功、失败或取消。已有本地内容在后台复制，成功后仍登记下载记录。
同一消息另存后列表按最新记录显示，不增加重复行。禁止借此绕过媒体禁止保存的限制。
取消不承诺删除已写入的部分文件，重试应选择新路径。

假下载只表示在真实管理器中模拟事件，不能作为联网下载成功的证据；素材文件须由测试自行构造。

## 消息、成员与编号提及

| 指令 | 参数 | 结果 |
|---|---|---|
| `peer.info` | `<peerId>` | 已知用户或会话资料；不返回手机号、访问密钥。 |
| `chat.members` | `<peerId> [no-username]` | 已缓存的群成员及是否可提及，不批量请求服务器成员列表。 |
| `chat.draft` | `<peerId>` | 普通聊天已保存的本地草稿，不修改草稿；尚未保存的输入框内容仍以控件查询为准。 |
| `message.list` | `<peerId> [limit]` | 当前缓存中的消息，默认 20 条，最多 100 条。 |
| `message.fetch` | `<peerId> [limit]` | 异步获取指定对话的最近消息；不打开对话，不发送已读回执。 |
| `message.inspect` | `<peerId> <messageId>` | 原有消息状态，以及发送者、提及实体、附件名、类型、大小和下载状态。 |
| `message.send-file` | `<peerId> <absolutePath> [caption]` | 后台准备单个文件并沿官方附件发送链路提交，不打开附件预览、不清除草稿。 |
| `mention.resolve` | `<userId>` | 与编号提及弹窗共用解析逻辑，返回异步任务，不插入输入框。 |
| `mention.send` | `<peerId> <userId> <displayText> <followingText>` | 通过当前账号解析目标，构造真实提及标签并沿官方发送链路提交。 |

提及保留原有约束：编号必须有效，仅复用当前账号已知资料或已有消息上下文，不猜测访问参数。
无用户名成员可先用 `chat.members <peerId> no-username` 查询；缓存不足时可查看已授权对话中的消息发送者，再用 `peer.info` 核对。

发送任务的 `state: submitted` 只表示已交给官方发送链路，不等于服务器确认。
随后使用 `message.fetch` 检查服务器返回的正文、消息编号和提及实体，才能确认发送结果。
实体的 `offset`、`length` 采用 UTF-16；编号提及只返回 `userId` 和 `accountId`，不返回访问密钥。
真实发送只限用户已经明确授权的测试对话与自建素材，不因新增指令自动扩大授权范围。

## 设置迁移与播放

- `settings.export <newAbsolutePath> <all|official|custom|account>`：异步导出指定范围，目标不得已存在。
- `settings.inspect-import <path> <scope>`：读取并验证文件，返回可应用数量、跳过项、缺失路径及重启项，不修改设置。
- `settings.import <path> <scope>`：验证通过后应用，使用 `job.status` 检查 `saved` 与错误；失败不伪称保存成功。
- `player.control <play|pause|toggle|stop> [song|voice]`：直接控制已有活动媒体；没有当前媒体时报错，不自动选择历史文件。

设置迁移共用正式功能的导出、校验及保存代码。导入以指令执行时的账号为目标，异步期间账号销毁则失败。
播放控制只验证业务动作，不代表空格键、输入法或焦点行为已通过验证。

## 异步任务

返回 `jobId` 后通过 `job.status <id>` 查询 `running`、`succeeded` 或 `failed`。
完成的任务可用 `job.forget <id>` 清理查询记录。任务只存在当前进程，重启后失效。
不要把指令被接收、文件出现或界面变化当作异步任务完成。
