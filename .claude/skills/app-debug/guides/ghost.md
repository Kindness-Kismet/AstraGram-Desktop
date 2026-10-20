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

主播模式同样按账号保存，当前账号用 `streamer.enabled`，指定账号用
`streamerModeSettings.<账号编号>.enabled`；设置导入导出的账号范围只作用于目标账号。

`settings.account <账号编号> [ghost.enabled|streamer.enabled true|false]` 可显式查询或保存指定账号；
保存结果通过 `job.status` 查询，未登录账号没有协议或窗口副作用，适合在独立离线配置检查持久化。
