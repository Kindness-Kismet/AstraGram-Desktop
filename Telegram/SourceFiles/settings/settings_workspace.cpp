#include "settings/settings_workspace.h"

#include "extras/ui/settings/settings_main.h"
#include "info/info_content_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/info_wrap_widget.h"
#include "info/settings/info_settings_widget.h"
#include "main/main_session.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_navigation.h"
#include "settings/settings_search.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"

namespace Settings {
namespace {

int navigationWidth() {
	return style::ConvertScale(268);
}

int wideThreshold() {
	return navigationWidth() + style::ConvertScale(364);
}

} // namespace

Workspace::Workspace(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Info::Memento*> memento,
	const QRect &geometry)
: Window::SectionWidget(parent, controller)
, _navigation(this, controller,
	[=](Type type) { showCategory(type); },
	[=](QString query) { showSearch(query); },
	[=] { closeWorkspace(); })
, _content(nullptr)
, _listShown(memento->settingsNavigationState()
	? memento->settingsNavigationState()->listShown
	: memento->content()->section().settingsType() == MainId())
, _category(memento->settingsNavigationState()
	? memento->settingsNavigationState()->category
	: Type()) {
	setObjectName(u"settings-workspace"_q);
	setGeometry(geometry);
	// 宽屏首页直接展示定制设置，完整概览仍可从分类进入。
	if (_listShown && !memento->settingsNavigationState()
		&& memento->stackSize() == 1) {
		auto initial = Info::Memento(Info::Settings::Tag{ controller->session().user() },
			Info::Section(ExtrasMain::Id()));
		_content.create(this, controller, Info::Wrap::Narrow, &initial,
			geometry.width() >= wideThreshold());
	} else {
		_content.create(this, controller, Info::Wrap::Narrow, memento,
			geometry.width() >= wideThreshold());
	}
	_content->contentChanged() | rpl::on_next([=] {
		_listShown = false;
		updateNavigation();
		updateLayout();
	}, lifetime());
	_content->setSettingsRootBack([=] { backFromCategory(); });
	updateNavigation();
	updateLayout();
}

Workspace::~Workspace() = default;

void Workspace::showCategory(Type type) {
	_content->checkBeforeClose([=] {
		_listShown = false;
		_category = type;
		_content->controller()->showSettings(type,
			Window::SectionShow(Window::SectionShow::Way::ClearStack));
		updateLayout();
		_content->setInnerFocus();
	});
}

void Workspace::showSearch(const QString &query) {
	_content->checkBeforeClose([=] {
		_listShown = false;
		auto state = std::make_shared<Info::Settings::Memento>(
			controller()->session().user(), Search::Id());
		state->setSectionState(SearchSectionState{ .query = query });
		auto memento = Info::Memento({ state });
		_content->showInternal(&memento,
			Window::SectionShow(Window::SectionShow::Way::ClearStack));
		updateLayout();
		_content->setInnerFocus();
	});
}

void Workspace::backFromCategory() {
	if (width() >= wideThreshold() || _listShown) {
		controller()->showBackFromStack();
		return;
	}
	_listShown = true;
	updateLayout();
	_navigation->setFocus();
}

void Workspace::closeWorkspace() {
	_content->checkBeforeClose([=] { controller()->showBackFromStack(); });
}

void Workspace::updateNavigation() {
	const auto type = _content->controller()->section().settingsType();
	if (!_category || !_content->hasSettingsHistory()) {
		_category = type;
	}
	_navigation->setActive(_category);
}

void Workspace::updateLayout() {
	if (!_content || _updatingLayout) {
		return;
	}
	_updatingLayout = true;
	const auto wide = width() >= wideThreshold();
	const auto navigationShown = wide || _listShown;
	const auto contentShown = wide || !_listShown;
	_navigation->setVisible(navigationShown);
	_navigation->setNarrow(!wide);
	_navigation->setGeometry(0, 0, wide ? navigationWidth() : width(), height());
	_content->setVisible(contentShown);
	_content->setSettingsNavigation(wide);
	const auto left = wide ? navigationWidth() : 0;
	_content->updateGeometry(QRect(left, 0, width() - left, height()),
		false, true, 0, height());
	_updatingLayout = false;
}

bool Workspace::showInternal(
	not_null<Window::SectionMemento*> memento,
	const Window::SectionShow &params) {
	const auto info = dynamic_cast<Info::Memento*>(memento.get());
	if (!info || info->content()->section().type() != Info::Section::Type::Settings) {
		return false;
	}
	const auto listShown = info->settingsNavigationState()
		? info->settingsNavigationState()->listShown
		: info->content()->section().settingsType() == MainId();
	const auto result = _content->showInternal(memento, params);
	if (result) {
		_listShown = listShown;
	}
	updateLayout();
	return result;
}

bool Workspace::showBackInternal() {
	return _content->closeByBackButton();
}

std::shared_ptr<Window::SectionMemento> Workspace::createMemento() {
	const auto result = std::static_pointer_cast<Info::Memento>(
		_content->createMemento());
	result->setSettingsNavigationState({
		.listShown = _listShown,
		.category = _category,
	});
	return result;
}

bool Workspace::floatPlayerHandleWheelEvent(QEvent *e) {
	return _content->floatPlayerHandleWheelEvent(e);
}

QRect Workspace::floatPlayerAvailableRect() {
	return _content->floatPlayerAvailableRect();
}

void Workspace::resizeEvent(QResizeEvent *e) {
	updateLayout();
}

void Workspace::paintEvent(QPaintEvent *e) {
	Window::SectionWidget::paintEvent(e);
	if (!animatingShow()) {
		QPainter(this).fillRect(e->rect(), st::windowBg);
	}
}

void Workspace::doSetInnerFocus() {
	if (_content->isVisible()) {
		_content->setInnerFocus();
	} else {
		_navigation->setFocus();
	}
}

void Workspace::showFinishedHook() {
	_content->showFast();
	updateLayout();
}

} // namespace Settings
