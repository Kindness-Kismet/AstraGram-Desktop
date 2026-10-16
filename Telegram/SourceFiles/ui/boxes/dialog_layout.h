#pragma once

#include "ui/vertical_list.h"

class QWidget;

namespace style {
struct SettingsButton;
} // namespace style

namespace Ui {

class RpWidget;

// 模态内容在装入外壳之前，也可由 BoxContent 祖先识别。
[[nodiscard]] bool IsDialogContent(not_null<QWidget*> widget);

[[nodiscard]] const style::SettingsButton &DialogButtonStyle(
	not_null<RpWidget*> owner,
	const style::SettingsButton &base,
	bool overlay = false);

not_null<FlatLabel*> AddDialogText(
	not_null<VerticalLayout*> container,
	rpl::producer<QString> text,
	const style::margins &margins = st::defaultBoxDividerLabelPadding,
	const style::DividerLabel &st = st::defaultDividerLabel);
not_null<FlatLabel*> AddDialogText(
	not_null<VerticalLayout*> container,
	rpl::producer<TextWithEntities> text,
	const style::margins &margins = st::defaultBoxDividerLabelPadding,
	const style::DividerLabel &st = st::defaultDividerLabel);

} // namespace Ui
