#include "ui/widgets/settings_toggle.h"

namespace Ui {

SettingsToggle::SettingsToggle(
	QWidget *parent,
	rpl::producer<QString> text,
	bool checked,
	const style::SettingsButton &st)
: SettingsButton(parent, std::move(text), st) {
	toggleOn(_setChecked.events_starting_with_copy(checked));
}

SettingsToggle::SettingsToggle(
	QWidget *parent,
	const QString &text,
	bool checked,
	const style::SettingsButton &st)
: SettingsToggle(parent, rpl::single(text), checked, st) {
}

bool SettingsToggle::checked() const {
	return toggled();
}

rpl::producer<bool> SettingsToggle::checkedChanges() const {
	return toggledChanges();
}

rpl::producer<bool> SettingsToggle::checkedValue() const {
	return toggledValue();
}

void SettingsToggle::setChecked(bool checked, anim::type animated) {
	if (checked != toggled()) {
		_setChecked.fire_copy(checked);
	}
	if (animated == anim::type::instant) {
		finishAnimating();
	}
}

} // namespace Ui
