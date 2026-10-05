#pragma once

#include "window/section_widget.h"
#include "settings/settings_type.h"

namespace Info {
class Memento;
class WrapWidget;
} // namespace Info

namespace style {
struct WindowTitle;
} // namespace style

namespace Settings {

namespace Builder {
struct SearchEntry;
} // namespace Builder

class Navigation;
class WorkspaceSearch;

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
	void hideEvent(QHideEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void doSetInnerFocus() override;
	void showFinishedHook() override;

private:
	void showCategory(Type type);
	void showSearchResult(Builder::SearchEntry entry);
	void backFromCategory();
	void closeWorkspace();
	void updateLayout();
	void updateNavigation();

	object_ptr<Navigation> _navigation;
	object_ptr<Info::WrapWidget> _content;
	object_ptr<WorkspaceSearch> _search;
	std::unique_ptr<style::WindowTitle> _titleStyle;
	bool _listShown = true;
	bool _updatingLayout = false;
	bool _searchInTitle = false;
	Type _category;
};

} // namespace Settings
