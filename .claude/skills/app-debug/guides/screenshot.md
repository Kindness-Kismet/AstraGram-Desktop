# 截图

## `screenshot.take`

截取活动窗口并保存为 JPG（质量 80）。CLI 自动生成路径 `build/screenshots/shot-<时间戳>.jpg`，
时间戳包含微秒，连续截图不会因同秒命名而覆盖；不接受路径参数。

```bash
python .claude/skills/app-debug/scripts/cli.py screenshot.take
# {"height":938,"path":"<repo>/build/screenshots/shot-20260901-190750-123456.jpg","width":1200}
```

**前提**：必须有活动窗口（`hasWindow: true`）。

截图走 `QWidget::grab()`，抓窗口自身渲染内容而非屏幕像素，被其他窗口遮挡时也能正常截图；
但 OpenGL 渲染的区域可能抓不全。

消息气泡等自绘内容不在控件树里，验证这类视觉效果只能靠截图，配合
`control.list` / `control.click` 做前置导航。

## `screenshot.take --notification [--bg #RRGGBB]`

截取应用自绘通知（`core.nativeNotifications` 为 `false` 时出现的那种）。它们是独立的透明顶层窗口，
主窗口截图抓不到；本模式按通知标识收集可见窗口，按屏幕位置合成到 `--bg` 底色上，保存为 PNG，
用来检查圆角、透明和阴影。底色缺省 `#5b6b7f`，换成浅色或深色可分别核对亮暗两种背景。

## `screenshot.take --popup`

仅截取当前活动的浮动菜单，用于检查菜单分隔、圆角和子菜单。嵌入菜单从宿主截图裁切，保留真实背景。
先打开菜单并等待展开动画结束，再执行截图。没有活动菜单时报错，不会悄悄改截主窗口。
默认不传 `--popup` 时截取主窗口及其内部菜单，路径与图片格式不变。
