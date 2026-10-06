---
name: pull-request
description: Use this skill when the user asks to create, prepare, review, or describe a pull request, or says phrases like "提 PR", "发 PR", "创建 PR", "审查 PR", "review PR", "pull request" in this AyuGram Desktop project.
---

# 拉取请求

默认以 `main` 为目标，功能分支使用 `feature/<名称>` 或 `fix/<名称>`。
用户指定分支时按指定值执行，准备前核对实际分支关系。

描述先说明具体问题和修改后的行为，再给出必要的实现理由、验证结果及限制。
简单修改用一两段；较复杂的修改按问题、改动、验证组织。标题使用英文祈使句，
描述最终完成的内容；提交遵循本地提交技能。

准备和审查时核对：

- 源码位置、2000 行上限与构建登记符合项目规范。
- 调试代码仅在调试构建生效。
- 设置声明、保存和读取同步；界面订阅绑定生命周期。
- 界面与会话数据在主线程处理，错误按返回值表达。
- 注释简短，说明意图和约束。
- fork 子模块的改动已推送到 fork 仓库，主仓库指针指向已推送的提交。
- 提交范围清楚，界面改动有实际截图，构建与测试结果可追溯。
- 已知限制写清楚，并区分实测结果和源码检查。

一个请求围绕一个主题；跨层或涉及大量文件时说明必要性。
准备好可审查的结果后，再按用户授权创建请求或推送。
