#pragma once

#include "ui/widgets/buttons.h"

namespace Ui {

// 沿用设置行的右侧开关布局，并支持外部同步状态。
class SettingsToggle final : public SettingsButton {
public:
	SettingsToggle(
		QWidget *parent,
		rpl::producer<QString> text,
		bool checked,
		const style::SettingsButton &st);
	SettingsToggle(
		QWidget *parent,
		const QString &text,
		bool checked,
		const style::SettingsButton &st);

	[[nodiscard]] bool checked() const;
	[[nodiscard]] rpl::producer<bool> checkedChanges() const;
	[[nodiscard]] rpl::producer<bool> checkedValue() const;
	void setChecked(bool checked, anim::type animated = anim::type::normal);

private:
	rpl::event_stream<bool> _setChecked;

};

} // namespace Ui
