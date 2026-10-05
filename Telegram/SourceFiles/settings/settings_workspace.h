#pragma once

#include "window/section_widget.h"
#include "settings/settings_type.h"

namespace Info {
class Memento;
class WrapWidget;
} // namespace Info

namespace Settings {

class Navigation;

class Workspace final : public Window::SectionWidget {
public:
	Workspace(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Info::Memento*> memento,
		const QRect &geometry);
	~Workspace();
	bool useFullWidth() const override {
		return true;
	}

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	bool showBackInternal() override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;
	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params) override;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void doSetInnerFocus() override;
	void showFinishedHook() override;

private:
	void showCategory(Type type);
	void showSearch(const QString &query);
	void backFromCategory();
	void closeWorkspace();
	void updateLayout();
	void updateNavigation();

	object_ptr<Navigation> _navigation;
	object_ptr<Info::WrapWidget> _content;
	bool _listShown = true;
	bool _updatingLayout = false;
	Type _category;
};

} // namespace Settings
