# 固定会话场景

进入假会话时自动生成固定列表。自动测试可先选择一次独立配置：

```bash
python .codex/skills/app-debug/scripts/cli.py app.start --profile scenarios + session.fake
python .codex/skills/app-debug/scripts/cli.py scenario.open discussion + screenshot.take
```

- `scenario.list`：返回场景的 `key`、`name`、`peerId`，可在初始化前查看。
- `scenario.open <key> [--view main|alternate|scheduled|shortcuts|pinned|actions] [--input keep|empty|reply|edit]`：打开列表中的场景，话题场景先在左栏展开话题列表，再进入真实话题输入区。
- `app.info` 的 `fakeSession` 表示当前会话是否由本进程创建，重启后失效。

| 键名 | 展示内容 |
|---|---|
| private | 普通私聊与输入区 |
| contact | 陌生人添加／屏蔽提示 |
| blocked | 已屏蔽用户的底部动作 |
| bot | 机器人启动按钮 |
| channel | 只读频道、置顶及通知按钮 |
| discussion | 频道底部讨论入口 |
| join | 未加入频道的加入按钮 |
| restricted | 群聊发送限制 |
| translate | 翻译与置顶组合 |
| requests | 加入申请与置顶组合 |
| topic | 话题输入区 |
| call | 计划通话、申请与置顶堆叠 |
| business | 商业机器人提示 |
| paid | 付费消息提示 |
| keyboard | 机器人键盘、命令补全与输入区 |
| sponsored | 本地广告样本与顶部条 |
| pinned | 多条置顶消息，可取消全部置顶的群组 |
| archived-private | 归档中的普通私聊 |
| archived-group | 归档中的群聊 |

假会话还会创建“朋友”和“工作”两个分组，分别展示未归档的私聊、群聊和频道。
切换分组后用 `chat.open-archive` 进入归档，它与菜单入口同样调用 `openFolder()`；两个归档场景仅存在内存中。
返回必须点击界面上可见的“Go back”，不同布局由不同按钮关闭归档，直接调用 `closeFolder()` 会绕过这些点击处理。
先开启 `hideAllChatsFolder` 再进入假会话，分组列表与真实账号一样不含全部对话。

这些场景使用正式界面的数据与控件路径，数据存在内存中；按钮仍保留原有业务行为。
布局检查使用截图、控件树和内部悬停，涉及发送、加入、通话等业务操作时单独安排测试。
修改输入文字可用于多行布局验证，测试数据只写入独立配置。

`--view alternate` 进入另一套聊天组件，`--view scheduled` 进入计划消息输入区，默认 `main` 使用主聊天。三种入口共用同一份本地会话数据，可用于检查组件差异。

`--view shortcuts` 打开本地快捷回复输入区。`--view pinned` 打开置顶消息列表，场景须含置顶消息：`pinned` 显示“取消置顶所有消息”，`channel` 显示“隐藏置顶消息”。`--view actions` 打开最近操作，仅限有管理权限的 `requests`、`call`；假会话没有服务器日志，只用于检查外框和底部按钮。`--input reply` 和 `--input edit` 为普通私聊或话题安装本地回复／编辑草稿，`empty` 清空，默认 `keep` 保留。草稿状态只用于主聊天和另一套聊天组件。机器人每次打开时恢复启动参数。

机器人键盘使用真实消息标记，并内置 `/help` 命令，可输入 `/` 检查补全列表。广告布局样本在假会话中固定显示，只使用本地数据，便于单独检查广告条；广告开关的真实业务验证另行安排。
