---
name: version-bump
description: 更新本应用的发布版本号并编写更新说明。用于升级版本号、准备新版本或查看当前版本。
---

# 版本更新

本应用版本号只保存在 `Telegram/build/version`。`version.h` 与 Windows 资源里的版本数值
在 CMake 配置阶段从它生成，不需要手动修改。

版本号前三段等于 `.github/upstream.json` 登记的官方版本，第四段是本项目修订号：
首次发布用 `x.y.z`，之后依次用 `x.y.z.1`、`x.y.z.2`。测试版在其后加 `.beta`，
例如 `7.2.10.beta`、`7.2.10.1.beta`。官方版本的变化走 upstream-sync。

`dev` 只发布测试版，GitHub 标记为 prerelease 且不覆盖 Latest；`main` 只发布稳定版。
测试发布从 GitHub Releases 手动安装，本次未增加应用内 beta 更新源；现有自动更新仍指向稳定 Latest。
存储版本、显示版本、系统资源版本仍为纯数字，GitHub tag 与发布名称保留 `.beta` 后缀。

1. 从 `origin` 确认本仓库地址，显式指定仓库查询所有已发布版本（包括 prerelease），确认当前官方版本下最大的修订号。
   稳定推广还需核对 main 与 dev 两个分支的版本，不能仅比较 Latest。
   查询失败时说明原因，待获得确切版本后继续。
2. 稳定版与测试版共用递增修订号（0～99），新版本的 AppUpdateVersion 必须比已发布版本更高。
   例如 `7.2.10.1.beta` 转稳定应使用 `7.2.10.2`，不能只删除 `.beta`；后续 dev 使用 `.3.beta`。
   不改变既有更新码编码，避免影响已安装客户端；修订号耗尽时停止，不回绕。
   本应用发布通道独立于上游；已适配的官方 beta tag 也可用于我们验证后的稳定发布。
3. 写好更新说明后修改版本号：

   ```bash
   python scripts/build_support/version.py <官方版本>[.<本库修订号>][.beta]
   ```

   脚本只改 `Telegram/build/version`，并检查前三段与已登记的官方版本一致、
   `.github/CHANGELOG.md` 符合下面的双语格式。

`AppVersion` 使用官方整数编码，负责数据兼容；
`AppUpdateVersion` 负责打包、版本比较和更新索引。

## 更新说明

范围为上一个已发布稳定版本到目标版本之间的最终变化，结合提交历史和最终代码差异整理。
只写用户能直接看到或使用的界面、功能、行为变化及问题修复，内容须有代码和验证依据。
构建、测试、重构和内部实现写在提交或本地过程记录中；隐藏功能信息保留在内部。

按最终结果合并：

- 同一功能多次修改、同一问题反复修复，只写最终结果
- 范围内新增后又移除、修改后又恢复的内容视为未发生
- 本次新增功能自身问题的修复并入该功能条目，不单独列出

`.github/CHANGELOG.md` 采用追加机制，整个文件仍是目标版本的发布正文：

- 先阅读现有内容，保留尚未发布的条目，在中英文两部分末尾同步追加本轮变化，不全量重写
- 追加前核对已有条目，避免重复；同一功能只有补充或修正时，仅修改对应的中英文条目
- 用户明确要求保留原文并追加时，保留已有条目的内容和顺序
- 现有内容已随稳定版发布时，开始新一轮更新说明，历史内容由已发布版本和 Git 历史保留

格式保持不变：

- 英文条目在上，空一行后单独一行 `---`，再空一行写简体中文条目
- 中英文条目数量、顺序、变化类型和含义一一对应
- 不写版本标题、日期或分类小节
- 每条以 `- ` 开头，末尾不加标点
- 完成官方适配时写 `Adapted to official Telegram Desktop x.y.z` 和 `适配了官方 Telegram Desktop x.y.z`

句式按变化类型选择：

- 修复：`Fixed an issue with {what} that previously caused {consequence}`；`修复了关于{对象}的错误，该问题曾导致{后果}`
- 改善：`Improved {feature}, which now {result}`；`对{功能}进行了改善，这使得{结果}`
- 新增：`Added {feature}, so you can now {action}`；`新增了{功能}，现在可以{做什么}`
- 移除：`Removed {feature}; {scenario} is no longer available`；`移除了{功能}，{场景}不再可用`
- 调整：`Changed how {feature} works; it now {new behavior}`；`调整了{功能}的行为，现在{新行为}`

```markdown
- Added {feature}, so you can now {action}

---

- 新增了{功能}，现在可以{做什么}
```

## 校验

```bash
python scripts/build_support/version.py
python scripts/release_notes.py .github/CHANGELOG.md build/release-notes.md
```

第一条显示当前版本，第二条按发布流程校验更新日志格式并生成发布正文。
发布前运行 `python scripts/release_config.py --branch main` 或 `--branch dev` 核对通道。
推送到 main 且版本文件有变化时自动发布稳定版；dev 上 BetaChannel=1 时自动发布测试版。
dev 携带稳定版本文件时跳过远程构建与发布，支持新建分支、回合 main 和准备稳定推广。
稳定推广通过 dev → main 的普通 merge，保留 dev，不 squash、不删除；随后 main 修复回合 dev。
测试版不生成稳定更新清单 current6，也不改变稳定版自动更新入口。
