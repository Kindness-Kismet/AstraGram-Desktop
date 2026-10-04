#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class FlatLabel;
class SettingsButton;
class VerticalLayout;
} // namespace Ui

namespace Settings {

class CardPage final : public Ui::RpWidget {
public:

	explicit CardPage(QWidget *parent);

	[[nodiscard]] not_null<Ui::VerticalLayout*> content() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

private:
	const not_null<Ui::VerticalLayout*> _content;
	bool _resizing = false;

};

[[nodiscard]] not_null<Ui::VerticalLayout*> AddCardGroup(
	not_null<Ui::VerticalLayout*> container);
not_null<Ui::FlatLabel*> AddCardTitle(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> title);
void AddCardDescription(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text);
void AddSectionRowDetails(
	not_null<Ui::SettingsButton*> button,
	rpl::producer<QString> description = nullptr);

} // namespace Settings
