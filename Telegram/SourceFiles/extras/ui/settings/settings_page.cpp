#include "extras/ui/settings/settings_page.h"

#include "boxes/language_box.h"
#include "core/application.h"
#include "core/shortcuts.h"
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "extras/features/window_material/window_material.h"
#include "extras/ui/settings/settings_main.h"
#include "extras/ui/settings/settings_page_widgets.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/info_content_widget.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "settings/settings_builder.h"
#include "settings/settings_power_saving.h"
#include "settings/settings_search.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_calls.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_folders.h"
#include "settings/sections/settings_information.h"
#include "settings/sections/settings_main.h"
#include "settings/sections/settings_notifications.h"
#include "settings/sections/settings_privacy_security.h"
#include "styles/style_extras_settings.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "window/window_session_controller.h"

#include <QKeyEvent>

namespace Settings {

Page::Page(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Info::Memento*> memento)
: Window::SectionWidget(parent, controller)
, _navigation(this)
, _back(_navigation, st::infoTopBarBack)
, _title(_navigation, tr::lng_menu_settings(), st::settingsPageTitle)
, _navigationScroll(_navigation, st::defaultScrollArea)
, _content(this, controller, Info::Wrap::Narrow, memento) {
	setObjectName(u"settings.page"_q);
	_navigation->setObjectName(u"settings.page.navigation"_q);
	_navigationScroll->setObjectName(u"settings.page.navigation.scroll"_q);
	_content->setObjectName(u"settings.page.content"_q);
	_back->setObjectName(u"settings.page.back"_q);
	_title->setObjectName(u"settings.page.title"_q);
	_back->setAccessibleName(tr::lng_go_back(tr::now));
	_back->setClickedCallback([=] { closePage(); });
	_navigationOnly = memento->settingsNavigationVisible().value_or(
		controller->highlightControlId().isEmpty());
	_content->setRootBackHandler([=] {
		if (currentSection() != MainId()) {
			switchSection(MainId(), true);
			return;
		}
		if (!_wide && !_navigationOnly) {
			_navigationOnly = true;
			updateLayout();
			doSetInnerFocus();
			return;
		}
		_closing = true;
		controller->showBackFromStack();
	});
	setupNavigation();
	Shortcuts::Requests() | rpl::filter([=] {
		return isVisible() && showingNavigation()
			&& Core::App().activeWindow() == &controller->window();
	}) | rpl::on_next([=](not_null<Shortcuts::Request*> request) {
		request->check(Shortcuts::Command::Search) && request->handle([=] {
			openSection(Search::Id());
			return true;
		});
	}, lifetime());
	_content->contentChanged() | rpl::on_next([=] {
		if (currentSection() == MainId()
			&& !controller->highlightControlId().isEmpty()) {
			_navigationOnly = false;
		}
		updateSelection();
		updateLayout();
		// 内层动画会重新显示容器，切页处理结束后恢复单栏可见性。
		crl::on_main(this, [=] { updateLayout(); });
	}, lifetime());
	updateSelection();
}

Page::~Page() = default;

bool Page::usesFullWidth() const {
	return true;
}

void Page::setupNavigation() {
	const auto inner = _navigationScroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(_navigationScroll));
	const auto profile = inner->add(object_ptr<Ui::FixedHeightWidget>(
		inner,
		st::settingsPageProfileHeight));
	const auto self = controller()->session().user();
	const auto photo = Ui::CreateChild<Ui::UserpicButton>(
		profile,
		self,
		st::settingsPageSidebarPhoto);
	photo->setObjectName(u"settings.page.profile.photo"_q);
	photo->showSavedMessagesOnSelf(false);
	photo->setClickedCallback([=] { openSection(InformationId()); });
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		profile,
		Info::Profile::NameValue(self),
		st::settingsPageProfileName);
	const auto subtitle = Ui::CreateChild<Ui::FlatLabel>(
		profile,
		tr::lng_settings_my_account(),
		st::settingsPageCaption);
	name->setObjectName(u"settings.page.profile.name"_q);
	subtitle->setObjectName(u"settings.page.profile.subtitle"_q);
	profile->widthValue() | rpl::on_next([=](int width) {
		const auto left = st::settingsPageInset;
		const auto textLeft = left + photo->width() + st::settingsPageSmallSkip;
		photo->moveToLeft(left, st::settingsPageSmallSkip, width);
		name->resizeToWidth(std::max(width - textLeft - left, 0));
		name->moveToLeft(textLeft, st::settingsPageSmallSkip, width);
		subtitle->resizeToWidth(std::max(width - textLeft - left, 0));
		subtitle->moveToLeft(textLeft,
			st::settingsPageSmallSkip + name->height() + st::lineWidth * 3,
			width);
	}, profile->lifetime());
	const auto add = [&](
			QString id,
			rpl::producer<QString> title,
			const style::icon *icon,
			Type type,
			Fn<void()> action = nullptr) {
		const auto button = inner->add(
			object_ptr<PageNavigationButton>(inner, std::move(title), icon),
			{ st::settingsPageSmallSkip, 0, st::settingsPageSmallSkip, 0 });
		button->setObjectName(u"settings.page.navigation."_q + id);
		button->setClickedCallback(action ? std::move(action) : [=] {
			openSection(type);
		});
		if (type) {
			_sectionButtons.emplace_back(type, button);
		}
	};
	add(u"search"_q, tr::lng_dlg_filter(), &st::menuIconSearch, Search::Id());
	add(u"overview"_q, tr::extras_SettingsPageOverview(),
		&st::menuIconSettings, MainId());
	Ui::AddSkip(inner, st::settingsPageSmallSkip);
	Ui::AddSubsectionTitle(inner, tr::extras_SettingsPagePersonal());
	if (!controller()->session().supportMode()) {
		add(u"account"_q, tr::lng_settings_my_account(),
			&st::menuIconProfile, InformationId());
	}
	add(u"notifications"_q, tr::lng_settings_section_notify(),
		&st::menuIconNotifications, NotificationsId());
	add(u"privacy"_q, tr::lng_settings_section_privacy(),
		&st::menuIconLock, PrivacySecurityId());
	add(u"chat"_q, tr::lng_settings_section_chat_settings(),
		&st::menuIconChatBubble, ChatId());
	Ui::AddSkip(inner, st::settingsPageSmallSkip);
	Ui::AddSubsectionTitle(inner, tr::extras_SettingsPagePreferences());
	add(u"folders"_q, tr::lng_settings_section_filters(),
		&st::menuIconShowInFolder, FoldersId(), [=] {
			controller()->session().data().chatsFilters().requestSuggested();
			openSection(FoldersId());
		});
	add(u"advanced"_q, tr::lng_settings_advanced(),
		&st::menuIconManage, AdvancedId());
	add(u"calls"_q, tr::lng_settings_section_devices(),
		&st::menuIconUnmute, CallsId());
	add(u"power"_q, tr::lng_settings_power_menu(),
		&st::menuIconPowerUsage, nullptr, [=] {
			controller()->show(Box(PowerSavingBox, PowerSaving::Flags()));
		});
	add(u"language"_q, tr::lng_settings_language(),
		&st::menuIconLanguage, nullptr, [=] {
			_languageRequest = LanguageBox::Show(controller());
		});
	Ui::AddSkip(inner, st::settingsPageSmallSkip);
	add(u"extras"_q, tr::extras_Preferences(),
		&st::menuIconPremium, ExtrasMain::Id());
	Ui::AddSkip(inner, st::settingsPageInset);
	_navigationScroll->widthValue() | rpl::on_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());
}

Type Page::currentSection() const {
	const auto section = _content->controller()->section();
	return (section.type() == Info::Section::Type::Settings)
		? section.settingsType()
		: Type();
}

bool Page::showingNavigation() const {
	return !_wide && currentSection() == MainId() && _navigationOnly;
}

void Page::updateSelection() {
	const auto current = currentSection();
	const auto &registry = Builder::SearchRegistry::Instance();
	for (const auto &[type, button] : _sectionButtons) {
		const auto selected = (type == current)
			|| (type != MainId() && type != Search::Id()
				&& registry.isSectionWithin(current, type));
		button->setSelected(selected);
	}
}

void Page::updateLayout() {
	_wide = width() >= st::settingsPageTwoColumnWidth;
	const auto navigationWidth = _wide ? st::settingsPageSidebarWidth : width();
	const auto navigationVisible = _wide || showingNavigation();
	_navigation->setGeometry(0, 0, navigationWidth, height());
	_navigation->setVisible(navigationVisible);
	_back->moveToLeft(0, 0, navigationWidth);
	_title->resizeToWidth(std::max(navigationWidth - _back->width()
		- st::settingsPageInset, 0));
	_title->moveToLeft(_back->width(),
		(st::settingsPageHeaderHeight - _title->height()) / 2, navigationWidth);
	_navigationScroll->setGeometry(0, st::settingsPageHeaderHeight,
		navigationWidth, std::max(height() - st::settingsPageHeaderHeight, 0));
	const auto left = _wide ? navigationWidth + st::lineWidth : 0;
	_content->updateGeometry({ left, 0, std::max(width() - left, 0), height() },
		false, true, 0, height());
	_content->setVisible(!showingNavigation());
	update();
}

void Page::openSection(Type type) {
	_content->checkBeforeClose([weak = base::make_weak(this), type] {
		if (!weak) {
			return;
		}
		weak->switchSection(type, type != MainId());
	});
}

void Page::switchSection(Type type, bool navigationOnly) {
	_navigationOnly = navigationOnly;
	const auto memento = std::make_shared<Info::Memento>(
		Info::Settings::Tag{ controller()->session().user() }, Info::Section(type));
	_content->showInternal(memento.get(), Window::SectionShow(
		Window::SectionShow::Way::ClearStack, anim::type::instant));
	_content->showFast();
	updateSelection();
	updateLayout();
	doSetInnerFocus();
}

void Page::closePage() {
	_content->checkBeforeClose([weak = base::make_weak(this)] {
		if (!weak) {
			return;
		}
		weak->_closing = true;
		weak->controller()->showBackFromStack();
	});
}

bool Page::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	const auto info = dynamic_cast<Info::Memento*>(memento.get());
	if (!info || info->stackSize() != 1
		|| info->content()->section().type() != Info::Section::Type::Settings) {
		return false;
	}
	const auto root = info->content()->section().settingsType() == MainId();
	if (root) {
		_navigationOnly = controller()->highlightControlId().isEmpty();
	}
	return _content->showInternal(memento, root
		? params.withWay(Window::SectionShow::Way::ClearStack)
		: params);
}

bool Page::showBackInternal() {
	if (_closing) {
		return false;
	}
	if (currentSection() == MainId() && !_wide && !_navigationOnly) {
		_navigationOnly = true;
		updateLayout();
		doSetInnerFocus();
		return true;
	}
	if (_content->closeByBackButton()) {
		return true;
	}
	if (currentSection() != MainId()) {
		_content->checkBeforeClose([=] { switchSection(MainId(), true); });
		return true;
	}
	return false;
}

std::shared_ptr<Window::SectionMemento> Page::createMemento() {
	const auto memento = std::static_pointer_cast<Info::Memento>(
		_content->createMemento());
	memento->setSettingsNavigationVisible(_navigationOnly);
	return memento;
}

bool Page::preventsClose(Fn<void()> &&continueCallback) const {
	return !_closing && _content->preventsClose(std::move(continueCallback));
}

SendMenu::Details Page::sendMenuDetails() const {
	return _content->sendMenuDetails();
}

bool Page::processChosenSticker(ChatHelpers::FileChosen &&chosen) {
	return _content->processChosenSticker(std::move(chosen));
}

rpl::producer<> Page::removeRequests() const {
	return _content->removeRequests();
}

bool Page::floatPlayerHandleWheelEvent(QEvent *e) {
	return showingNavigation() ? false : _content->floatPlayerHandleWheelEvent(e);
}

QRect Page::floatPlayerAvailableRect() {
	return showingNavigation() ? QRect() : _content->floatPlayerAvailableRect();
}

void Page::doSetInnerFocus() {
	if (showingNavigation()) {
		_navigationScroll->setFocus();
	} else {
		_content->setInnerFocus();
	}
}

void Page::showFinishedHook() {
	_content->showFast();
	updateLayout();
}

void Page::resizeEvent(QResizeEvent *e) {
	Window::SectionWidget::resizeEvent(e);
	updateLayout();
}

void Page::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		if (!showBackInternal()) {
			closePage();
		}
		return;
	}
	Window::SectionWidget::keyPressEvent(e);
}

void Page::paintEvent(QPaintEvent *e) {
	Window::SectionWidget::paintEvent(e);
	if (animatingShow()) {
		return;
	}
	auto p = QPainter(this);
	p.fillRect(e->rect(), ExtrasFeatures::WindowMaterial::surfaceColor(
		this, st::windowBg->c));
	if (_wide) {
		p.fillRect(0, 0, st::settingsPageSidebarWidth, height(),
			ExtrasFeatures::WindowMaterial::surfaceColor(this, st::boxDividerBg->c));
		p.fillRect(st::settingsPageSidebarWidth, 0, st::lineWidth, height(),
			st::boxDividerBg);
	}
}

} // namespace Settings
