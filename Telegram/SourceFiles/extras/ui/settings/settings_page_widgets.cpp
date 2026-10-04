#include "extras/ui/settings/settings_page_widgets.h"

#include "boxes/language_box.h"
#include "base/battery_saving.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "extras/ui/settings/settings_main.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "settings/settings_builder.h"
#include "settings/settings_power_saving.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_information.h"
#include "settings/sections/settings_privacy_security.h"
#include "styles/style_extras_settings.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/widgets/settings_toggle.h"
#include "ui/wrap/vertical_layout.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"

namespace Settings {
namespace {

class PageTiles final : public Ui::RpWidget {
public:
	PageTiles(QWidget *parent, Fn<void(Type)> showOther)
	: RpWidget(parent) {
		const auto add = [&](QString id, rpl::producer<QString> title,
				const style::icon *icon, Type type) {
			const auto button = Ui::CreateChild<PageNavigationButton>(
				this, std::move(title), icon, true);
			button->setObjectName(u"settings.page.overview."_q + id);
			button->setClickedCallback([=] { showOther(type); });
			_buttons.push_back(button);
		};
		add(u"account"_q, tr::lng_settings_my_account(),
			&st::menuIconProfile, InformationId());
		add(u"privacy"_q, tr::lng_settings_section_privacy(),
			&st::menuIconLock, PrivacySecurityId());
		add(u"chat"_q, tr::lng_settings_section_chat_settings(),
			&st::menuIconChatBubble, ChatId());
		add(u"advanced"_q, tr::lng_settings_advanced(),
			&st::menuIconManage, AdvancedId());
	}

protected:
	int resizeGetHeight(int width) override {
		const auto columns = width >= st::settingsPageGridTwoColumnWidth ? 2 : 1;
		const auto gap = st::settingsPageSmallSkip;
		const auto tileWidth = std::max((width - (columns - 1) * gap) / columns, 0);
		for (auto i = 0; i != int(_buttons.size()); ++i) {
			const auto button = _buttons[i];
			button->resizeToWidth(tileWidth);
			button->moveToLeft((i % columns) * (tileWidth + gap),
				(i / columns) * (st::settingsPageTileHeight + gap), width);
		}
		const auto rows = (int(_buttons.size()) + columns - 1) / columns;
		return rows * st::settingsPageTileHeight + (rows - 1) * gap;
	}

private:
	std::vector<PageNavigationButton*> _buttons;
};

} // namespace

PageNavigationButton::PageNavigationButton(
	QWidget *parent,
	rpl::producer<QString> title,
	const style::icon *icon,
	bool tile)
: Ui::AbstractButton(parent)
, _icon(icon)
, _tile(tile) {
	setFocusPolicy(Qt::StrongFocus);
	setIsListItem(true);
	std::move(title) | rpl::on_next([=](const QString &text) {
		_title = text;
		setAccessibleName(text);
		setToolTip(text);
		update();
	}, lifetime());
}

void PageNavigationButton::setSelected(bool selected) {
	_selected = selected;
	setProperty("selected", selected);
	update();
}

QString PageNavigationButton::accessibilityName() {
	return _title;
}

int PageNavigationButton::resizeGetHeight(int newWidth) {
	return _tile ? st::settingsPageTileHeight : st::settingsPageRowHeight;
}

void PageNavigationButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.setRenderHint(QPainter::Antialiasing);
	const auto inset = st::settingsPageSmallSkip;
	const auto radius = st::settingsPageRadius;
	auto background = st::windowActiveTextFg->c;
	background.setAlpha(_selected ? 24 : 10);
	if (_tile || _selected || isOver() || hasFocus()) {
		p.setPen(_tile ? QPen(st::boxDividerBg->c, st::lineWidth) : Qt::NoPen);
		p.setBrush(isOver() ? st::lightButtonBgOver->c : background);
		p.drawRoundedRect(rect().adjusted(st::lineWidth, st::lineWidth,
			-st::lineWidth, -st::lineWidth), radius, radius);
	}
	const auto foreground = (_selected || _tile)
		? st::windowActiveTextFg->c : st::windowFg->c;
	const auto iconLeft = _tile ? st::settingsPageInset : inset;
	const auto iconTop = _tile ? st::settingsPageInset
		: (height() - _icon->height()) / 2;
	_icon->paint(p, iconLeft, iconTop, width(), foreground);
	const auto textLeft = _tile ? st::settingsPageInset
		: inset + _icon->width() + st::settingsPageSmallSkip;
	const auto textTop = _tile
		? height() - st::settingsPageInset - st::normalFont->height
		: (height() - st::normalFont->height) / 2;
	p.setPen(st::windowFg);
	p.setFont(st::normalFont);
	p.drawTextLeft(textLeft, textTop, width(), st::normalFont->elided(
		_title, std::max(width() - textLeft - st::settingsPageInset, 0)));
}

void PageNavigationButton::onStateChanged(
		State was,
		StateChangeSource source) {
	Ui::AbstractButton::onStateChanged(was, source);
	update();
}

void buildPageOverview(Builder::SectionBuilder &builder) {
	using namespace Builder;
	const auto controller = builder.controller();
	const auto showOther = builder.showOther();
	builder.addButton({
		.id = u"settings.page.overview.extras"_q,
		.title = tr::extras_Preferences(),
		.st = &st::settingsButtonActive,
		.icon = { &st::menuIconPremium },
		.onClick = [=] { showOther(ExtrasMain::Id()); },
	});
	builder.addSkip(st::settingsPageSmallSkip);
	builder.addSubsectionTitle(tr::extras_SettingsPageQuick());
	builder.add([](const WidgetContext &ctx) {
		return SectionBuilder::WidgetToAdd{
			.widget = object_ptr<PageTiles>(ctx.container, ctx.showOther),
			.margin = { st::settingsPageInset, st::settingsPageSmallSkip,
				st::settingsPageInset, st::settingsPageSmallSkip },
		};
	});
	builder.addSubsectionTitle(tr::extras_SettingsPagePreferences());
	const auto desktop = builder.addToggle({
		.id = u"settings.page.overview.desktop-notifications"_q,
		.title = tr::lng_settings_desktop_notify(),
		.checked = Core::App().settings().desktopNotify(),
	});
	using ChangeType = Window::Notifications::ChangeType;
	desktop->checkedChanges() | rpl::filter([](bool checked) {
		return checked != Core::App().settings().desktopNotify();
	}) | rpl::on_next([](bool checked) {
		Core::App().settings().setDesktopNotify(checked);
		Core::App().saveSettingsDelayed();
		Core::App().notifications().notifySettingsChanged(ChangeType::DesktopEnabled);
	}, desktop->lifetime());
	Core::App().notifications().settingsChanged() | rpl::on_next([=](ChangeType type) {
		if (type == ChangeType::DesktopEnabled) {
			desktop->setChecked(Core::App().settings().desktopNotify());
		}
	}, desktop->lifetime());
	const auto animations = builder.addToggle({
		.id = u"settings.page.overview.animations"_q,
		.title = tr::lng_settings_power_ui(),
		.checked = !PowerSaving::On(PowerSaving::kAnimations),
	});
	const auto synchronizing = animations->lifetime().make_state<bool>(false);
	animations->checkedChanges() | rpl::filter([=] {
		return !*synchronizing;
	}) | rpl::on_next([](bool checked) {
		PowerSaving::Set(checked
			? PowerSaving::Current() & ~PowerSaving::kAnimations
			: PowerSaving::Current() | PowerSaving::kAnimations);
		Core::App().saveSettingsDelayed();
	}, animations->lifetime());
	rpl::merge(
		PowerSaving::Changes(),
		Core::App().batterySaving().value() | rpl::to_empty,
		Core::App().settings().ignoreBatterySavingValue() | rpl::to_empty
	) | rpl::on_next([=] {
		*synchronizing = true;
		animations->setChecked(!PowerSaving::On(PowerSaving::kAnimations));
		animations->setDisabled(PowerSaving::ForceAll());
		*synchronizing = false;
	}, animations->lifetime());
	const auto guard = builder.container()->lifetime().make_state<base::binary_guard>();
	builder.addButton({
		.id = u"main/language"_q,
		.title = tr::lng_settings_language(),
		.icon = { &st::menuIconLanguage },
		.label = rpl::single(Lang::GetInstance().nativeName()) | rpl::then(
			Lang::GetInstance().idChanges() | rpl::map([] {
				return Lang::GetInstance().nativeName();
			})),
		.onClick = [=] { *guard = LanguageBox::Show(controller); },
	});
	builder.addButton({
		.id = u"main/power"_q,
		.title = tr::lng_settings_power_menu(),
		.icon = { &st::menuIconPowerUsage },
		.onClick = [=] {
			controller->show(Box(PowerSavingBox, PowerSaving::Flags()));
		},
	});
}

} // namespace Settings
