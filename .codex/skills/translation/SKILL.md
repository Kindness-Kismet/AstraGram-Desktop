---
name: translation
description: 新增、翻译或审查 AstraGram 界面文案、Telegram 翻译键与 extras 多语言资源时使用。覆盖操作、进度、结果、状态和面向用户的失败提示。
---

# 界面翻译

先从根 `AGENTS.md` 找到实际资源来源，再查看控件、触发状态和已有译文。本项目沿用 Telegram 的 `.strings`、`tr::` 和 extras JSON 机制，键名与参数按现有实现维护。

## 按任务阅读指南

| 任务 | 指南 |
|---|---|
| 本项目的键名、登记、参数、复数、加载与校验 | [Telegram / Qt 翻译规则](guides/frameworks/qt.md) |
| 选择语态、精简提示、处理动态内容 | [字符串写法](guides/wording.md) |
| 常用动作、状态和已确定的产品术语 | [中英文词典](guides/glossary.md) |

其他框架资料保留在 [GPUI](guides/frameworks/gpui.md)、[Kotlin](guides/frameworks/kotlin.md)、[Avalonia](guides/frameworks/avalonia.md)、[TypeScript](guides/frameworks/typescript.md) 指南；它们不改变本项目现有键系统。

## 执行流程

1. 查看文案的位置和触发条件，区分按钮动作、进行中、已结束、当前状态与失败原因，在同一功能的资源与调用处查已有表达。
2. 先阅读 Telegram / Qt 指南并核对实际键定义。相同含义、用途和参数优先复用现有键；新定制键沿用 extras 命名，不批量重命名上游键或改变既有参数契约。
3. 按字符串写法和词典处理译文。英文拼写相同但语境不同的词分别判断，产品术语以现有界面和已确认词义为准；需要跨页面复用的词义维护到词典。
4. 按本次变化核对 `.strings`、实际语言 JSON、资源注册和 C++ 调用；参数、复数与富文本分别检查。构建生成器只验证它实际消费的资源，不能代替 JSON 覆盖检查。
5. 按本次需要核对语言切换、最终参数替换和界面布局，沿用根规范的最小验证范围。异步操作的提示由真实状态触发，不把请求受理写成操作完成。

## 完成与维护

核对本次译文、键注册、参数集合、数量分支和真实调用方；保留现有语言及其覆盖关系。术语变化同时检查相关界面。
