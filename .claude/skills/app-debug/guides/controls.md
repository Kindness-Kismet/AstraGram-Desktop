# 控件树与合成交互

## 值与动作

`control.get <target>` 查询当前值、类型、文字、启用状态及辅助功能动作。
可选列表项还会返回 `selected`，可用于核对当前选中的会话分组。
`control.set <target> <value>` 修改输入框、开关、单选项、选择框或滑块，并调用原有业务回调。
滑块值为 0 至 1；输入字符串保留原文。单选项通过选择另一个选项切换，不直接取消选中。

`control.action <target> <action>` 执行 `control.get` 返回的辅助功能动作。
`control.mouse <target> <x> <y> [left|right|double|press|release]` 在控件内部指定位置投递点击、右键或双击，
适用于消息气泡等自绘区域。所有事件都在本应用内，不移动系统光标。
`press` 只按下、`release` 只松开，用于长按录音等需要保持按下的操作；松开直接投递给目标控件。

目标可使用控件标识、可见文字或 `control.list` 的全树序号。文字重复时使用标识或序号。

## `control.scroll <objectName | #序号> [top]`

读取普通或弹性滚动区的位置；传入整数时滚动到该位置，用于历史消息、设置页和菜单。
先用 `control.list @scroll` 找到滚动区，再用默认模式的 `#序号` 定位；页面变化后重新列举。
返回 `top`、`maximum` 和 `height`。主聊天滚动区名称为 `historyScroll`。

## `control.hover <objectName | #序号> <on|off>`

设置可见且启用的按钮悬停状态，通过截图检查背景、圆角和文字，不触发点击、不移动系统光标。
验证后用 `off` 恢复；只验证绘制状态，不代表真实鼠标命中验收。

## `control.pointer <objectName | #序号> [x y]`

按控件内坐标命中子控件并合成进入、移动事件，用于观察自绘消息的快速回复和回应按钮。
不移动系统鼠标、不按下按钮；省略坐标时清除上一次合成悬停。真实鼠标操作可能覆盖这个状态。

```bash
python .codex/skills/app-debug/scripts/cli.py control.pointer historyScroll 160 240
python .codex/skills/app-debug/scripts/cli.py control.pointer historyScroll
```

## `control.key <目标> <escape|up|down|left|right|enter|tab>`

向控件投递按下和释放事件，不操作系统键盘。菜单可使用 `@menu`，用于退出键和方向键导航。
`control.list @menu` 列出菜单，`isWindow` 区分原生窗口与宿主内浮层；`@menu` 同样可用于点击、移动和悬停指令。

## 点击命中检查

control.drop-files <目标> <本地文件路径>... 仅限本进程假会话，向控件投递本地文件拖放事件，沿正常输入区流程打开附件预览；不会点击发送，也不使用系统剪贴板。

`control.click <目标> --mouse` 从窗口开始命中测试，再发送鼠标事件。目标中心被遮挡时返回错误，
用于检查遮罩、层级与点击回调；不等同于人工鼠标验收，依赖真实光标的行为仍需人工确认。

## `control.input <objectName | #序号> <text|--file path>`

修改活动窗口中可见且启用的输入框，用于验证单行、多行和清空后的布局。不会触发发送，
但正常草稿保存仍会执行，因此只在本地假会话或自己掌控的测试对话中使用。

`--file` 按 UTF-8 读取文字，保留换行和引号。返回 `previousText`、`length` 和 `height`；
验证前保存原文字，完成后恢复。协议使用编码后的文字，命令行负责转换。

```bash
python .claude/skills/app-debug/scripts/cli.py control.input messageInput --file build/compose-test.txt
```

## `control.list [filter] [--all]`

列出活动窗口的控件树。每项含：全树序号（`index`）、层级（`depth`）、类名（`class`）、
objectName（`name`）、accessibleName（`accessible`）、文本（`text`）、相对父级与全局几何
（`rect` / `globalRect`）、`visible` / `enabled`。

```bash
python .claude/skills/app-debug/scripts/cli.py control.list
python .claude/skills/app-debug/scripts/cli.py control.list sendButton
python .claude/skills/app-debug/scripts/cli.py control.list --all
# {"total":386,"shown":386,"truncated":false,"widgets":[{"index":0,"depth":0,...}]}
```

- 过滤参数是子串匹配（objectName / 类名 / accessibleName / 文本），只影响展示；
  `index` 始终是全树序号，与 `control.click #序号` 一致。
- `--all` 扫全部顶层窗口（菜单、弹层是独立顶层）。此时序号语义与默认模式不同，
  混用时优先 objectName。
- 最多返回 500 项，超出时 `truncated: true`。

自带 objectName 的常用控件：`brandMenuButton`（顶部布局主菜单）、`chatFolders.menu`（左侧标签主菜单）、
`mainMenuButton`（窄列表主菜单）、`sendButton`（发送）、
`messageInput`（消息输入框）、`menu.*`（主菜单项）、`extras/*`（AstraGram 设置按钮，
id 即 objectName，如 `extras/search`、`extras/cat/ghost`）。没有 objectName 的控件用 `control.list`
查类名/accessibleName 定位，或用 `#序号` 寻址。

## `control.click <objectName | #序号> [--all]`

点击控件。按钮直接执行控件动作，不经过系统输入和命中测试；其余控件在中心做命中测试，
沿 Enter → Press → Release → Leave 顺序投递鼠标事件。不依赖窗口前台与真实光标。

```bash
python .claude/skills/app-debug/scripts/cli.py control.click mainMenuButton
python .claude/skills/app-debug/scripts/cli.py control.click "#42"
# {"class":"Ui::IconButton","name":"mainMenuButton","receiver":"...","point":[44,66],"handled":true}
```

- 寻址：objectName（先活动窗口，再全部顶层）或 `#序号`（对应默认 `control.list` 的
  `index`，UI 变化后序号会漂移，需重新 list）。objectName 找不到时按 accessibleName
  精确匹配作为备选。
- 按钮的控件动作与鼠标松开的顺序一致：复选框先切换选中状态，单选框只会被选中，再通知点击回调。
- 直接执行不能证明鼠标点得到；检查遮挡、事件过滤等点击路径时加 `--mouse`。
- 目标不可见或被禁用时报错；被隐藏页里的控件先导航到对应页面。
- 回调可能销毁控件自身（如菜单项点击后 PopupMenu 整体销毁），返回前已拷贝取值并确认控件存活，
  点击后 `control.list` 可能因控件销毁找不到目标，属正常。
- 少数依赖 `QCursor::pos()` 真实光标位置的代码路径不会触发。
- Windows 自绘标题栏（最小化/最大化/关闭）不是 QWidget，无法寻址；退出用 `app.quit`。

多个页面具有相同控件名时，交互指令选择当前可见控件。序号仍按完整控件树定位；切页后重新查询。
