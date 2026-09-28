# 定制业务命令

## 过滤规则

- `filter.list`：全部规则，编号采用十六进制。
- `filter.put <json>`：新增规则；带已有 `id` 时更新。字段为 `text`、`enabled`、
  `reversed`、`caseInsensitive`、`dialogId`；`dialogId: null` 表示全局。
- `filter.remove <id>`：移除指定规则与关联排除项。
- `filter.exclusions`：列出排除项。
- `filter.exclude <id> <peerId> <true|false>`：设置全局规则对指定会话是否排除。
- `filter.check <peerId> <messageId>`：经过真实过滤逻辑查询消息结果。
- `filter.visible <peerId> [true|false]`：查询／修改被过滤消息的临时显示状态。

规则保存后会重建缓存并通知界面。规则中的 dialogId 沿用数据库编号；
命令的 peerId 则使用 `chat.list` 返回值，两者不要混用。

## 文本与翻译

`text.process <send|edit|receive|auto-space|zalgo> <text> [entitiesJson]`
验证文本处理。前三种按当前开关执行，后两种直接调用对应处理器。
实体格式为 `[{"type":数字,"offset":0,"length":2,"data":""}]`，
偏移与长度使用 UTF-16 单位；结果返回文本和处理后的实体。

`translate.start <language> <text>` 按当前翻译服务发起请求，返回任务编号。
官方翻译服务要求真实登录。翻译可能向所选服务发送文字，不在未授权账号上验证。
`translate.clear-cache` 清空应用的翻译缓存。

## 表情包

`emoji.list` 返回当前包、已安装包和预设。
`emoji.import <fontPath>` 导入本地字体；`emoji.install <presetId>` 下载并安装预设；
`emoji.select <id>` 切换已安装包；`emoji.cancel <presetId>` 取消预设下载。

导入、下载与切换返回任务编号。使用 `job.status <id>` 查询进度或结果，
终态为 `succeeded/failed`；`job.forget <id>` 清理已结束的查询记录。
异步工作不会在调试服务端主线程中等待。

## 消息截图与转发

`message.shot <peerId> <messageId>...` 打开原生消息截图预览，
后续使用控件命令调整并保存；截图设置可通过 `messageShotSettings.*` 修改。

`forward.status <peerId>` 查询转发任务，`forward.cancel <peerId>` 取消正在进行的任务。
发起转发、重复发送和更多消息动作可通过消息右键菜单操作，保留原有选项与目标确认流程。

`feature.status` 返回隐私遮挡开关、窗口材质实际生效状态、支持的材质、翻译服务和当前表情包。
