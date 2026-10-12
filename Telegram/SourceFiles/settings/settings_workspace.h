#pragma once

#include "window/section_widget.h"
#include "settings/settings_type.h"

namespace Info {
class Memento;
class WrapWidget;
} // namespace Info

namespace Settings {

namespace Builder {
struct SearchEntry;
} // namespace Builder

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
	void showSearch();
	void showSearchResult(Builder::SearchEntry entry);
	void backFromCategory();
	void closeWorkspace();
	void updateLayout();
	void updateNavigation();

	object_ptr<Navigation> _navigation;
	object_ptr<Info::WrapWidget> _content;
	// 最近打开的设置项，搜索浮层每次新建，记录随设置页保留。
	const std::shared_ptr<std::vector<QString>> _searchRecent;
	bool _listShown = true;
	bool _updatingLayout = false;
	Type _category;
};

} // namespace Settings
