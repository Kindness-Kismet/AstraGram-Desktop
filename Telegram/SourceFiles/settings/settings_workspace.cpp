#include "settings/settings_workspace.h"
#include "extras/features/window_material/window_material.h"
#include "core/application.h"
#include "core/click_handler_types.h"

#include "extras/ui/settings/settings_main.h"
#include "info/info_content_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/info_wrap_widget.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_navigation.h"
#include "settings/settings_workspace_search.h"
#include "ui/widgets/buttons.h"
#include "ui/platform/ui_platform_window_title.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

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
	[=] { closeWorkspace(); })
, _content(nullptr)
, _search(nullptr)
, _titleStyle(std::make_unique<style::WindowTitle>(st::defaultWindowTitle))
, _listShown(memento->settingsNavigationState()
	? memento->settingsNavigationState()->listShown
	: memento->content()->section().settingsType() == MainId())
, _category(memento->settingsNavigationState()
	? memento->settingsNavigationState()->category
	: Type()) {
	setObjectName(u"settings-workspace"_q);
	_titleStyle->height = style::ConvertScale(52);
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
	_search.create(this, this, &controller->session(), [=](Builder::SearchEntry entry) {
		showSearchResult(std::move(entry));
	});
	controller->widget()->hitTestRequests() | rpl::on_next([=](
			not_null<Ui::Platform::HitTestRequest*> request) {
		if (_searchInTitle && _search->containsGlobalPoint(
				controller->widget()->mapToGlobal(request->point))) {
			request->result = Ui::Platform::HitTestResult::Client;
		}
	}, lifetime());
	_content->contentChanged() | rpl::on_next([=] {
		_search->dismiss();
		_listShown = false;
		updateNavigation();
		updateLayout();
	}, lifetime());
	_content->setSettingsRootBack([=] { backFromCategory(); });
	updateNavigation();
	updateLayout();
}

Workspace::~Workspace() {
	_updatingLayout = true;
	lifetime().destroy();
	_search.destroy();
	if (_searchInTitle) {
		controller()->widget()->setTitleStyle(st::defaultWindowTitle);
	}
}

void Workspace::showSearchResult(Builder::SearchEntry entry) {
	_content->checkBeforeClose([=] {
		controller()->setHighlightControlId(entry.id);
		if (!entry.deeplink.isEmpty()) {
			Core::App().openLocalUrl(entry.deeplink,
				QVariant::fromValue(ClickHandlerContext{
					.sessionWindow = base::make_weak(controller()),
				}));
			return;
		}
		_listShown = false;
		_category = Builder::SearchRegistry::Instance().sectionCategory(entry.section);
		_content->controller()->showSettings(entry.section);
		updateLayout();
		_content->setInnerFocus();
	});
}

void Workspace::showCategory(Type type) {
	if (type == _category) {
		if (_listShown) {
			_listShown = false;
			updateLayout();
			_content->setInnerFocus();
		}
		return;
	}
	_content->checkBeforeClose([=] {
		_listShown = false;
		_category = type;
		_content->controller()->showSettings(type,
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
	const auto window = controller()->widget();
	const auto title = window->titleWidget();
	const auto inTitle = title && !title->isHidden() && isVisible();
	if (_searchInTitle != inTitle) {
		_searchInTitle = inTitle;
		window->setTitleStyle(inTitle
			? *_titleStyle
			: st::defaultWindowTitle);
	}
	const auto wide = width() >= wideThreshold();
	const auto navigationShown = wide || _listShown;
	const auto contentShown = wide || !_listShown;
	const auto searchHeight = inTitle ? 0 : style::ConvertScale(60);
	const auto navigationTop = wide ? 0 : searchHeight;
	_navigation->setVisible(navigationShown);
	_navigation->setNarrow(!wide);
	_navigation->setGeometry(0, navigationTop, wide ? navigationWidth() : width(),
		height() - navigationTop);
	_content->setVisible(contentShown);
	_content->setSettingsNavigation(wide);
	const auto left = wide ? navigationWidth() : 0;
	_content->updateGeometry(QRect(left, searchHeight,
		width() - left, height() - searchHeight),
		false, true, 0, height() - searchHeight);
	if (_search) {
		const auto searchParent = inTitle ? title : this;
		if (_search->parentWidget() != searchParent) {
			_search->setParent(searchParent);
		}
		if (inTitle) {
			const auto controls = st::defaultWindowTitle.minimize.width
				+ st::defaultWindowTitle.maximize.width
				+ st::defaultWindowTitle.close.width;
			const auto inset = wide ? controls : 0;
			// 正文卡片与标题栏之间的间隙也计入可见的上下留白。
			_search->setGeometry(inset, 0,
				std::max(1, title->width() - inset - controls),
				title->height() + st::windowCardGap);
		} else {
			_search->setGeometry(left, 0, width() - left, searchHeight);
		}
		_search->setVisible(isVisible());
		_search->raise();
		_search->updateLayout();
	}
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
	if (_search->dismiss()) {
		return true;
	}
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

QPixmap Workspace::grabForShowAnimation(const Window::SectionSlideParams &params) {
	updateLayout();
	return Window::SectionWidget::grabForShowAnimation(params);
}

void Workspace::resizeEvent(QResizeEvent *e) {
	updateLayout();
}

void Workspace::hideEvent(QHideEvent *e) {
	if (_search) {
		_search->hide();
	}
	if (_searchInTitle) {
		_searchInTitle = false;
		controller()->widget()->setTitleStyle(st::defaultWindowTitle);
	}
	Window::SectionWidget::hideEvent(e);
}

void Workspace::paintEvent(QPaintEvent *e) {
	Window::SectionWidget::paintEvent(e);
	if (!animatingShow()) {
		QPainter(this).fillRect(e->rect(),
			ExtrasFeatures::WindowMaterial::surfaceColor(this, st::dialogsBg->c));
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
