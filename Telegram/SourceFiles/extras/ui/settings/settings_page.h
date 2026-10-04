#pragma once

#include "window/section_widget.h"
#include "settings/settings_type.h"
#include "base/binary_guard.h"

namespace Info {
class Memento;
class WrapWidget;
} // namespace Info

namespace Ui {
class ScrollArea;
class VerticalLayout;
class FlatLabel;
class IconButton;
} // namespace Ui

namespace Settings {

class PageNavigationButton;

class Page final : public Window::SectionWidget {
public:
	Page(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Info::Memento*> memento);
	~Page();

	bool usesFullWidth() const override;
	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	bool showBackInternal() override;
	bool preventsClose(Fn<void()> &&continueCallback) const override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	SendMenu::Details sendMenuDetails() const override;
	bool processChosenSticker(ChatHelpers::FileChosen &&chosen) override;
	rpl::producer<> removeRequests() const override;
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void doSetInnerFocus() override;
	void showFinishedHook() override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void setupNavigation();
	void openSection(Type type);
	void switchSection(Type type, bool navigationOnly);
	void closePage();
	void updateLayout();
	void updateSelection();
	[[nodiscard]] Type currentSection() const;
	[[nodiscard]] bool showingNavigation() const;

	object_ptr<Ui::RpWidget> _navigation;
	object_ptr<Ui::IconButton> _back;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::ScrollArea> _navigationScroll;
	object_ptr<Info::WrapWidget> _content;
	std::vector<std::pair<Type, PageNavigationButton*>> _sectionButtons;
	bool _wide = false;
	bool _closing = false;
	bool _navigationOnly = true;
	base::binary_guard _languageRequest;

};

} // namespace Settings
