# 不切换界面的业务指令

先使用业务指令验证数据与行为；只有布局、点击命中和焦点检查才操作窗口。
业务指令不会为了查询切换聊天、弹出文件选择器或抢占焦点；设置、下载与发送仍会产生正常业务效果。

## 下载

| 指令 | 参数 | 结果 |
|---|---|---|
| `downloads.list` | `[all|archives|music|videos|other] [query]` | 当前账号的下载记录、分类、路径、大小、已下载字节及状态，按下载时间倒序。 |
| `downloads.start` | `<peerId> <messageId> <destination>` | 向新建的绝对路径保存附件，返回 `jobId`；父目录必须存在，不覆盖已有文件。 |
| `downloads.cancel` | `<peerId> <messageId>` | 取消指定附件的活动下载。 |

列表使用下载页面的同一个数据提供器，类型与关键词取交集；查询不会改变页面筛选或选择状态。
`resolved: false` 表示旧下载记录尚未解析完成，此时空列表不是最终结果。只返回当前账号的记录。
`state` 为 `downloading` 或 `completed`；真实下载进度来自下载管理器，不用文件是否出现来推断完成。

下载页常驻搜索框为 `downloads/search`，用 `control.get/set` 查询和修改。
弹层与侧栏共用共享媒体的画廊页头，`downloads/header` 包含分类标题、数量与搜索框；分类色块 `downloads/typeTiles` 固定在文件列表上方。
色块为 `downloads/type/<all|archives|music|videos|other>`，用 `control.click` 切换，`control.get` 的 `selected` 表示当前分类。
色块右上角数量已应用关键词，过窄时隐藏；标签放不下时整行缩小字号和间距，搜索栏的“下载”标签页可用于验证。
切换分类保留关键词并清除多选。`downloads.list` 仍独立查询业务数据。

下载任务通过 `job.status` 查看最终成功、失败或取消。已有本地内容在后台复制，成功后仍登记下载记录。
同一消息另存后列表按最新记录显示，不增加重复行。禁止借此绕过媒体禁止保存的限制。
取消不承诺删除已写入的部分文件，重试应选择新路径。

本地下载样本及进度注入见[模拟模式](simulation.md#补充消息和下载)。

## 消息、成员与编号提及

| 指令 | 参数 | 结果 |
|---|---|---|
| `peer.info` | `<peerId>` | 已知用户或会话资料；不返回手机号、访问密钥。 |
| `chat.members` | `<peerId> [no-username]` | 已缓存的群成员及是否可提及，不批量请求服务器成员列表。 |
| `chat.draft` | `<peerId> [topicRootId]` | 已保存的草稿、回复、转发及暂存状态；话题编号取 `simulation.list` 或 `message.inspect`，尚未保存的文字以控件查询为准。 |
| `message.list` | `<peerId> [limit]` | 当前缓存中的消息，默认 20 条，最多 100 条。 |
| `message.fetch` | `<peerId> [limit]` | 异步获取指定对话的最近消息；不打开对话，不发送已读回执。 |
| `message.media` | `<peerId> [limit] [offsetId]` | 异步读取文件附件的原始结构：类型、大小、视频属性、缩略图及全部附带文件；不写入本地消息。 |
| `message.media-file` | `<peerId> <messageId> <mime> <destination>` | 异步保存附带文件中第一个指定类型的文件，例如缩略图拼图 `application/x-tgstoryboard`；只请求一次，超过 1 MB 直接拒绝。 |
| `video.cover` | `<peerId> <messageId>` | 与右键“查看视频封面”相同，在媒体查看器中查看上传者设置的封面，没有时查看服务器缩略图；消息需已加载，两者都没有时报错。返回封面来源与缩略图大小。 |
| `media.open` | `<peerId> <messageId>` | 与点击消息里的照片或视频相同，在媒体查看器中打开，可继续左右翻看同一会话的媒体；消息需已加载，没有照片或文件时报错。返回 `photo` 或 `document`。 |
| `peer.resolve` | `<username>` | 异步按用户名解析公开频道或用户，返回会话编号；不加入、不关注。 |
| `message.inspect` | `<peerId> <messageId>` | 原有消息状态，以及发送者、提及实体、附件名、类型、大小和下载状态。 |
| `message.send-file` | `<peerId> <absolutePath> [caption] [--media]` | 后台准备单个文件并沿官方附件发送链路提交，不打开附件预览、不清除草稿；默认按文件发送，`--media` 对应“压缩”，视频按可播放的媒体发送。 |
| `mention.resolve` | `<userId>` | 与编号提及弹窗共用解析逻辑，返回异步任务，不插入输入框。 |
| `mention.send` | `<peerId> <userId> <displayText> <followingText>` | 通过当前账号解析目标，构造真实提及标签并沿官方发送链路提交。 |

提及保留原有约束：编号必须有效，仅复用当前账号已知资料或已有消息上下文，不猜测访问参数。
`peer.info` 还返回普通认证、官方资源、支持者和表情状态的标记，用于区分徽标来源。
`cache.badges` 返回徽标名单的 `restoredFromCache`、`refreshing` 和各类数量；只查询状态，不请求网络。

无用户名成员可先用 `chat.members <peerId> no-username` 查询；缓存不足时可查看已授权对话中的消息发送者，再用 `peer.info` 核对。

发送任务的 `state: submitted` 只表示已交给官方发送链路，不等于服务器确认。
随后使用 `message.fetch` 检查服务器返回的正文、消息编号和提及实体，才能确认发送结果。
实体的 `offset`、`length` 采用 UTF-16；编号提及只返回 `userId` 和 `accountId`，不返回访问密钥。
真实发送只限用户已经明确授权的测试对话与自建素材，不因新增指令自动扩大授权范围。

`chat.draft` 的 `stash` 为空表示没有暂存，否则返回文字、回复编号、转发数量与选项、附件数量及富文本状态。
`action.run stash-message` 沿原生快捷键暂存或恢复当前输入；验证时只在模拟模式操作，不触发发送。

## 媒体编辑器

`editor.open <peerId> <msgId>` 对图片消息发出与查看器绘图按钮相同的请求，在当前窗口打开编辑器。
`editor.state` 仅在独立模拟配置中查询当前图片编辑器的裁剪框、图层数量、音轨长度、裁剪起止、音量和选中状态。
切到画笔工具后，`editor.add-file <path>` 沿正式媒体粘贴链路添加本地图片、视频或音轨，不使用系统剪贴板。
添加是异步操作，返回 `submitted` 后通过 `editor.state` 或界面确认实际结果；文件不受支持时仍由编辑器显示原有错误。
裁剪、音量和删除通过原有控件操作，查询命令本身不修改编辑内容。
`editor.video-state` 查询可见视频时间线的裁剪起止、封面、播放位置与缩放；时间单位均为毫秒。

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

## 收藏缓存状态

message.cache-state <收藏消息编号>... 查询类型、投票、反应、媒体、会员状态及反应是否正在发送，不导出正文。
message.cache-react <收藏消息编号> <普通表情> 复用正式反应接口，需用户事先授权；收藏反应标签要求会员。
反应内存状态不代表服务器确认；调试日志记录服务器成功或拒绝及错误类型，未获确认不能算缓存验收通过。
