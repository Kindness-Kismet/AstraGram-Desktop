#pragma once

#include "settings/settings_type.h"
#include "ui/abstract_button.h"

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Settings {

class PageNavigationButton final : public Ui::AbstractButton {
public:
	PageNavigationButton(
		QWidget *parent,
		rpl::producer<QString> title,
		const style::icon *icon,
		bool tile = false);

	void setSelected(bool selected);
	QString accessibilityName() override;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

private:
	QString _title;
	const style::icon *_icon = nullptr;
	bool _tile = false;
	bool _selected = false;

};

void buildPageOverview(Builder::SectionBuilder &builder);

} // namespace Settings
