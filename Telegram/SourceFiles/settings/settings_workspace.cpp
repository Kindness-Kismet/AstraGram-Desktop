#include "settings/settings_workspace.h"
#include "extras/features/window_material/window_material.h"
#include "base/event_filter.h"
#include "core/application.h"
#include "core/click_handler_types.h"

#include "extras/ui/settings/settings_main.h"
#include "info/info_content_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/info_wrap_widget.h"
#include "main/main_session.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_navigation.h"
#include "settings/settings_workspace_search.h"
#include "window/window_session_controller.h"
#include "styles/style_dialogs.h"

#include <QApplication>

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
	[=] { showSearch(); },
	[=] { closeWorkspace(); })
, _content(nullptr)
, _searchRecent(std::make_shared<std::vector<QString>>())
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
	base::install_event_filter(this, qApp, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::KeyPress || !isVisible()) {
			return base::EventFilterResult::Continue;
		}
		const auto key = static_cast<QKeyEvent*>(e.get());
		if (key->key() != Qt::Key_F
			|| key->modifiers() != Qt::ControlModifier
			|| QApplication::activeModalWidget()
			|| controller->isLayerShown()) {
			return base::EventFilterResult::Continue;
		}
		showSearch();
		return base::EventFilterResult::Cancel;
	});
	_content->contentChanged() | rpl::on_next([=] {
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
}

void Workspace::showSearch() {
	controller()->showLayer(
		std::make_unique<WorkspaceSearch>(
			&controller()->session(),
			_searchRecent,
			crl::guard(this, [=](Builder::SearchEntry entry) {
				showSearchResult(std::move(entry));
			})),
		Ui::LayerOption::CloseOther);
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
	// 已登记的页面按所属分类高亮；未登记的子页沿用进入时的分类。
	const auto category = Builder::SearchRegistry::Instance().sectionCategory(type);
	if (_navigation->has(category)) {
		_category = category;
	} else if (!_category || !_content->hasSettingsHistory()) {
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

QPixmap Workspace::grabForShowAnimation(const Window::SectionSlideParams &params) {
	updateLayout();
	return Window::SectionWidget::grabForShowAnimation(params);
}

void Workspace::resizeEvent(QResizeEvent *e) {
	updateLayout();
}

void Workspace::paintEvent(QPaintEvent *e) {
	Window::SectionWidget::paintEvent(e);
	if (!animatingShow()) {
		QPainter(this).fillRect(e->rect(),
			ExtrasFeatures::WindowMaterial::surfaceColor(this, st::settingsPageBg->c));
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
