#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

namespace ExtrasUi {

// 按单独一行的 --- 切段，只取全部由 "- " 条目组成的段，中文段或英文段二选一。
[[nodiscard]] QStringList ParseReleaseNotes(const QString &body, bool chinese);

// 更新版本整数转为发布标签，例如 70300110 转为 v7.3.1.10。
[[nodiscard]] QString UpdateVersionTag(uint64 version);

// 界面语言是简体中文时更新说明显示中文，其它语言显示英文。
[[nodiscard]] bool ReleaseNotesInChinese();

struct UpdateBoxArgs {
	uint64 version = 0;
	Fn<void()> update;
	Fn<void()> skip;
};

void UpdateBox(not_null<Ui::GenericBox*> box, UpdateBoxArgs args);

} // namespace ExtrasUi
