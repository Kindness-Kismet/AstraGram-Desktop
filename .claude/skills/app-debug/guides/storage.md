# 消息留档与本地事件

`storage.stats` 查询留档开关、数据库路径、大小与过滤规则数量。

`storage.deleted <peerId> [limit] [search]` 查询已删除消息；
`storage.edits <peerId> <messageId> [limit]` 查询编辑历史。
默认返回 100 条，上限 1000 条；只读取当前账号的数据。
结果包含正文、原始消息编号、会话、发送者和保存时间。

在假会话中可走原生事件处理链验证留档：

```bash
python .codex/skills/app-debug/scripts/cli.py message.fake '原始消息' --from 123456789
python .codex/skills/app-debug/scripts/cli.py settings.set saveMessagesHistory true
python .codex/skills/app-debug/scripts/cli.py message.edit-local self '<上一步消息编号>' '编辑后的消息'
python .codex/skills/app-debug/scripts/cli.py storage.edits self '<上一步消息编号>'
```

`message.edit-local` 走编辑事件处理，`message.delete-local` 走原有删除逻辑；
保存开关、发送者身份等条件仍按应用规则决定是否留档。两条命令只允许本进程创建的假会话。

`message.inspect <peerId> <messageId>` 查看正文与已删除、隐藏、过滤、历史和视图状态。
`message.hide <peerId> <messageId>` 与右键菜单相同：隐藏消息及所在相册并移出列表，收藏夹不支持。
真实编辑／删除和远端同步效果需要另行授权的测试账号，本轮不执行。
