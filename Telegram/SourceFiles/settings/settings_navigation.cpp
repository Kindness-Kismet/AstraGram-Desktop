#include "settings/settings_navigation.h"
#include "extras/features/window_material/window_material.h"

#include "boxes/about_box.h"
#include "boxes/star_gift_box.h"
#include "api/api_credits.h"
#include "data/components/credits.h"
#include "settings/sections/settings_business.h"
#include "settings/sections/settings_credits.h"
#include "settings/sections/settings_premium.h"
#include "core/application.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "main/main_app_config.h"
#include "main/main_session_settings.h"
#include "extras/ui/settings/settings_main.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_calls.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_folders.h"
#include "settings/sections/settings_information.h"
#include "settings/sections/settings_language.h"
#include "settings/sections/settings_main.h"
#include "settings/sections/settings_notifications.h"
#include "settings/sections/settings_privacy_security.h"
#include "settings/settings_power_saving.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_extras_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

int scaled(int value) {
	return style::ConvertScale(value);
}

class AccountHeader final : public Ui::AbstractButton {
public:
	AccountHeader(QWidget *parent, not_null<UserData*> user)
	: AbstractButton(parent)
	, _user(user)
	, _userpic(user->createUserpicView()) {
		_user->loadUserpic();
		setAccessibleName(user->name());
		_user->session().changes().peerUpdates(_user,
			Data::PeerUpdate::Flag::Name | Data::PeerUpdate::Flag::Username
				| Data::PeerUpdate::Flag::Photo
		) | rpl::on_next([=] {
			_user->loadUserpic();
			setAccessibleName(_user->name());
			update();
		}, lifetime());
		_user->session().downloaderTaskFinished() | rpl::on_next([=] {
			update();
		}, lifetime());
	}

protected:
	int resizeGetHeight(int width) override {
		return scaled(88);
	}
	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		_user->paintUserpicLeft(p, _userpic, scaled(10), scaled(16), width(), scaled(56), true);
		const auto left = scaled(78);
		const auto textWidth = width() - left - scaled(12);
		p.setPen(st::windowBoldFg);
		p.setFont(st::semiboldFont);
		p.drawText(QRect(left, scaled(21), textWidth, scaled(23)),
			Qt::AlignVCenter, st::semiboldFont->elided(_user->name(), textWidth));
		p.setPen(st::windowSubTextFg);
		p.setFont(st::settingsExperimentalAbout.style.font);
		const auto subtitle = _user->username().isEmpty()
			? QString::number(peerToUser(_user->id).bare)
			: '@' + _user->username();
		p.drawText(QRect(left, scaled(47), textWidth, scaled(19)),
			Qt::AlignVCenter, st::settingsExperimentalAbout.style.font->elided(subtitle, textWidth));
	}

private:
	const not_null<UserData*> _user;
	Ui::PeerUserpicView _userpic;
};

} // namespace

class Navigation::Item final : public Ui::AbstractButton {
public:
	Item(QWidget *parent, rpl::producer<QString> title, const style::icon &icon)
	: AbstractButton(parent)
	, _icon(icon) {
		std::move(title) | rpl::on_next([=](QString text) {
			_title = std::move(text);
			setAccessibleName(_title);
			update();
		}, lifetime());
	}
	void setActive(bool active) {
		_active = active;
		update();
	}

protected:
	int resizeGetHeight(int width) override {
		return scaled(40);
	}
	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		if (_active || isOver() || isDown()) {
			p.setBrush(_active ? st::windowBgActive
				: isDown() ? st::windowBgRipple : st::windowBgOver);
			p.drawRoundedRect(rect(), scaled(10), scaled(10));
		}
		_icon.paint(p, scaled(10), (height() - _icon.height()) / 2,
			width(), (_active ? st::windowFgActive : st::windowFg)->c);
		p.setFont(st::normalFont);
		p.setPen(_active ? st::windowFgActive : st::windowFg);
		const auto left = scaled(44);
		const auto available = width() - left - scaled(10);
		p.drawText(QRect(left, 0, available, height()), Qt::AlignVCenter,
			st::normalFont->elided(_title, available));
	}
	void onStateChanged(State was, StateChangeSource source) override {
		update();
	}

private:
	const style::icon &_icon;
	QString _title;
	bool _active = false;
};

Navigation::Navigation(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Fn<void(Type)> navigate,
	Fn<void()> close)
: RpWidget(parent)
, _navigate(std::move(navigate))
, _header(this)
, _scroll(this, st::defaultScrollArea) {
	setObjectName(u"settings-navigation"_q);
	ExtrasFeatures::WindowMaterial::watchSurface(this);
	const auto home = Ui::CreateChild<Ui::AbstractButton>(_header.data());
	home->setObjectName(u"settings-overview"_q);
	home->setAccessibleName(tr::lng_menu_settings(tr::now));
	home->setClickedCallback([=] { _navigate(MainId()); });
	const auto title = Ui::CreateChild<Ui::FlatLabel>(home, tr::lng_menu_settings(), st::boxTitle);
	title->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto exit = Ui::CreateChild<Ui::IconButton>(_header.data(), st::settingsPageClose);
	exit->setObjectName(u"settings-close"_q);
	exit->setAccessibleName(tr::lng_close(tr::now));
	exit->setClickedCallback(std::move(close));
	const auto accountMenu = Ui::CreateChild<Ui::IconButton>(
		_header.data(), st::settingsPageMenu);
	accountMenu->setObjectName(u"settings-account-menu"_q);
	accountMenu->setAccessibleName(tr::lng_sr_profile_menu(tr::now));
	accountMenu->setClickedCallback([=] {
		_accountMenu = base::make_unique_q<Ui::PopupMenu>(accountMenu, st::popupMenuWithIcons);
		const auto menu = _accountMenu.get();
		FillAccountMenu(controller, Ui::Menu::CreateAddActionCallback(menu), [=](Type type) {
			_navigate(type);
		});
		menu->popup(accountMenu->mapToGlobal(QPoint(0, accountMenu->height())));
	});
	_header->sizeValue() | rpl::on_next([=](QSize size) {
		home->setGeometry(scaled(20), 0, title->width(), size.height());
		title->moveToLeft(0, (size.height() - title->height()) / 2);
		exit->moveToRight(0, (size.height() - exit->height()) / 2);
		accountMenu->moveToRight(exit->width(), (size.height() - accountMenu->height()) / 2);
	}, lifetime());

	_list = _scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this));
	const auto account = _list->add(object_ptr<AccountHeader>(
		_list, controller->session().user()), { scaled(8), scaled(8), scaled(8), scaled(8) });
	account->setObjectName(u"settings-account-header"_q);
	account->setClickedCallback([=] { _navigate(InformationId()); });

	addSeparator();
	addItem(tr::extras_Preferences(), st::menuIconAstraGram, u"extras"_q, ExtrasMain::Id());
	addSeparator();
	if (!controller->session().supportMode()) {
		addItem(tr::lng_settings_my_account(), st::menuIconProfile, u"account"_q, InformationId());
	}
	addItem(tr::lng_settings_section_notify(), st::menuIconNotifications, u"notifications"_q, NotificationsId());
	addItem(tr::lng_settings_section_privacy(), st::menuIconLock, u"privacy"_q, PrivacySecurityId());
	addItem(tr::lng_settings_section_chat_settings(), st::menuIconChatBubble, u"chat"_q, ChatId());
	const auto session = &controller->session();
	const auto hasFilters = session->data().chatsFilters().has()
		|| session->settings().dialogsFiltersEnabled();
	if (hasFilters) {
		session->data().chatsFilters().requestSuggested();
	}
	auto foldersShown = hasFilters
		? rpl::single(true) | rpl::type_erased
		: (rpl::single(rpl::empty) | rpl::then(
			session->appConfig().refreshed()
		) | rpl::map([=] {
			const auto enabled = session->appConfig().get<bool>(
				u"dialog_filters_enabled"_q, false);
			if (enabled) {
				session->data().chatsFilters().requestSuggested();
			}
			return enabled;
		}));
	addItem(tr::lng_settings_section_filters(), st::menuIconShowInFolder,
		u"folders"_q, FoldersId(), nullptr, std::move(foldersShown));
	addItem(tr::lng_settings_advanced(), st::menuIconManage, u"advanced"_q, AdvancedId());
	addItem(tr::lng_settings_section_devices(), st::menuIconUnmute, u"calls"_q, CallsId());
	addItem(tr::lng_settings_power_menu(), st::menuIconPowerUsage, u"power"_q, PowerSavingId());
	addItem(tr::lng_settings_language(), st::menuIconLanguage, u"language"_q, LanguageId());
	if (session->premiumPossible()) {
		addSeparator();
		addItem(tr::lng_premium_summary_title(), st::menuIconPremium, u"premium"_q, PremiumId(), [=] {
			controller->setPremiumRef("settings");
			_navigate(PremiumId());
		});
		addItem(tr::lng_settings_credits(), st::menuIconStar, u"credits"_q, CreditsId(), [=] {
			controller->setPremiumRef("settings");
			_navigate(CreditsId());
		});
		session->credits().load();
		session->credits().tonLoad();
		addItem(tr::lng_settings_currency(), st::menuIconTon, u"currency"_q, CurrencyId(), [=] {
			controller->setPremiumRef("settings");
			_navigate(CurrencyId());
		}, session->credits().tonBalanceValue() | rpl::map([](CreditsAmount amount) {
			return !amount.empty();
		}));
		addItem(tr::lng_business_title(), st::menuIconShop, u"business"_q, BusinessId());
		if (session->premiumCanBuy()) {
			addItem(tr::lng_settings_gift_premium(), st::menuIconGiftPremium, u"gift"_q, nullptr, [=] {
				Ui::ChooseStarGiftRecipient(controller);
			});
		}
	}
	addSeparator();
	addItem(tr::lng_settings_faq(), st::menuIconFaq, u"faq"_q, nullptr, [=] {
		OpenFaq(base::make_weak(controller));
	});
	addItem(tr::lng_settings_ask_question(), st::menuIconDiscussion, u"ask-question"_q, nullptr, [=] {
		OpenAskQuestionConfirm(controller);
	});
	addSeparator();
	addItem(tr::lng_menu_about(), st::menuIconInfo, u"about"_q, nullptr, [=] {
		controller->show(Box(AboutBox, controller.get()));
	});
	Ui::AddSkip(_list, scaled(12));
	_scroll->widthValue() | rpl::on_next([=](int width) {
		_list->resizeToWidth(width);
	}, lifetime());
}

Navigation::~Navigation() {
	// 菜单恢复焦点时，导航控件必须尚未进入 QWidget 析构。
	if (_accountMenu) {
		_accountMenu->hideMenu(true);
	}
}

void Navigation::addSeparator() {
	Ui::AddSkip(_list, scaled(12));
}

void Navigation::addItem(
	rpl::producer<QString> title,
	const style::icon &icon,
	const QString &name,
	Type type,
		Fn<void()> callback,
		rpl::producer<bool> shown) {
	auto owned = object_ptr<Item>(_list, std::move(title), icon);
	const auto item = owned.data();
	if (shown) {
		const auto wrap = _list->add(object_ptr<Ui::SlideWrap<Item>>(
			_list, std::move(owned)), { scaled(8), 0, scaled(8), 0 });
		wrap->toggleOn(std::move(shown));
		wrap->finishAnimating();
	} else {
		_list->add(std::move(owned), { scaled(8), 0, scaled(8), 0 });
	}
	item->setObjectName(u"settings-category-"_q + name);
	item->setClickedCallback(callback ? std::move(callback) : [=] { _navigate(type); });
	_items.emplace_back(std::move(type), item);
}

void Navigation::setActive(Type type) {
	for (const auto &[id, item] : _items) {
		item->setActive(id && id == type);
	}
}

void Navigation::setNarrow(bool narrow) {
	setProperty("narrow", narrow);
	update();
}

void Navigation::resizeEvent(QResizeEvent *e) {
	_header->setGeometry(0, 0, width(), scaled(48));
	_scroll->setGeometry(0, _header->height(), width(), height() - _header->height());
}

void Navigation::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), ExtrasFeatures::WindowMaterial::surfaceColor(
		this, st::dialogsBg->c));
	if (!property("narrow").toBool()
		&& !ExtrasFeatures::WindowMaterial::isActive(this)) {
		p.fillRect(width() - st::lineWidth, 0, st::lineWidth, height(), st::windowDividerFg);
	}
}

} // namespace Settings
