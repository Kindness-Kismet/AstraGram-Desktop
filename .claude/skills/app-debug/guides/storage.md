# 消息留档与本地事件

`storage.stats` 查询留档开关、数据库路径、大小与过滤规则数量。
返回的 `archiveReady` 表示留档已解锁，`archiveError` 表示最近的存储错误。

`storage.verify-archive` 仅在独立模拟模式中运行，用临时数据库验证加密、篡改拒绝、检索和旧库迁移。
它不操作当前留档；本地密码的创建、修改、关闭和冷启动解锁仍需通过真实界面另行验证。

`storage.deleted <peerId> [limit] [search]` 查询已删除消息；
`storage.edits <peerId> <messageId> [limit]` 查询编辑历史。
默认返回 100 条，上限 1000 条；只读取当前账号的数据。
结果包含正文、原始消息编号、会话、发送者和保存时间。

`message.edit-local` 和 `message.delete-local` 仅用于模拟模式，走原生本地事件处理以检查显示状态。
模拟编辑、删除不写入留档；数据库的加密和迁移通过 `storage.verify-archive` 独立验证。

`message.inspect <peerId> <messageId>` 查看正文与已删除、过滤、历史和视图状态；同时返回当前显示文字、译文是否显示、是否手动翻译、请求状态和聊天翻译状态。
`deletedOpacity` 返回主聊天视图的当前透明度，无视图时为空；相册内部各项还会独立处理透明度，需结合截图核对。
真实编辑／删除和远端同步效果需要另行授权的测试账号，本轮不执行。
