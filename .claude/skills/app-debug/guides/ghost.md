# 隐身模式

## `ghost.status`

返回当前会话的隐身模式状态 JSON：

- `useGlobalGhostMode` — 是否使用全局开关
- `account` — per-account 设置对象
  - `sendReadMessages` / `sendReadStories` / `sendOnlinePackets` / `sendUploadProgress`
  - `sendOfflinePacketAfterOnline` / `markReadAfterAction` / `useScheduledMessages`
  - `shouldSendWithoutSound` — 发送时是否静音（枚举）
  - `suggestGhostModeBeforeViewingStory` — 查看 Story 前提示
  - 五个 `*Locked` 只读锁位

```bash
python .claude/skills/app-debug/scripts/cli.py ghost.status
# {"useGlobalGhostMode":false,"account":{...}}
```

**前提**：必须已登录（`hasSession: true`），否则返回 `ERR no active session`。

改隐身模式开关用 `settings.set`（见 `settings.md`）：全局开关键 `useGlobalGhostMode`，
账号级键在 `ghostModeSettings` 对象内。
