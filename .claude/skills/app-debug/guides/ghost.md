# 隐身模式

## `ghost.status`

返回当前会话的隐身模式状态 JSON：

- `userId` — 当前设置所属的账号编号
- `account` — 该账号独立保存的设置对象
  - `sendReadMessages` / `sendReadStories` / `sendOnlinePackets` / `sendUploadProgress`
  - `sendOfflinePacketAfterOnline` / `markReadAfterAction` / `useScheduledMessages`
  - `shouldSendWithoutSound` — 发送时是否静音（枚举）
  - `suggestGhostModeBeforeViewingStory` — 查看 Story 前提示
  - 五个 `*Locked` 只读锁位

```bash
python .claude/skills/app-debug/scripts/cli.py ghost.status
# 返回账号编号和该账号的设置对象。
```

**前提**：必须已登录（`hasSession: true`），否则返回 `ERR no active session`。

改隐身模式开关用 `settings.set`（见 `settings.md`），`ghost.*` 作用于当前账号，
`ghostModeSettings.<账号编号>.*` 指定账号；不再提供全局开关。
