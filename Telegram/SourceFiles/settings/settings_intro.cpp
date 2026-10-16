/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_intro.h"

#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_main.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_update.h"
#include "settings/settings_codes.h"
#include "ui/boxes/dialog_layout.h"
#include "ui/basic_click_handlers.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/vertical_list.h"
#include "lang/lang_keys.h"
#include "window/window_controller.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"

namespace Settings {
namespace {

object_ptr<Ui::VerticalLayout> CreateIntroSettings(
		QWidget *parent,
		not_null<Window::Controller*> window) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto content = result.data();
	const auto addSection = [&](auto setup) {
		auto section = object_ptr<Ui::VerticalLayout>(content);
		setup(section.data());
		if (!section->count()) {
			return;
		}
		const auto top = content->count()
			? st::boxMediumSkip
			: st::boxPadding.top();
		content->add(std::move(section), QMargins(0, top, 0, 0));
	};

	addSection([&](auto section) {
		SetupLanguageButton(window, section);
		SetupConnectionType(window, &window->account(), section);
	});
	addSection(SetupUpdate);
	addSection([&](auto section) {
		SetupSystemIntegrationContent(window->sessionController(), section);
	});
	addSection([&](auto section) {
		SetupInterfaceScale(window, section, false);
	});
	addSection([&](auto section) {
		SetupDefaultThemes(window, section);
	});
	if (anim::Disabled()) {
		addSection([&](auto section) {
			SetupAnimations(window, section);
		});
	}
	addSection([](auto section) {
		AddButtonWithIcon(
			section,
			tr::lng_settings_faq(),
			Ui::DialogButtonStyle(section, st::settingsButtonNoIcon)
		)->addClickHandler([] {
			OpenFaq(nullptr);
		});
	});
	Ui::AddSkip(content, st::boxPadding.bottom());
	return result;
}

} // namespace

IntroSettingsBox::IntroSettingsBox(
	QWidget*,
	not_null<Window::Controller*> window)
: _window(window) {
}

void IntroSettingsBox::prepare() {
	setObjectName(u"settings.intro"_q);
	setTitle(tr::lng_menu_settings());
	addButton(tr::lng_box_done(), [=] { closeBox(); });
	const auto content = setInnerWidget(CreateIntroSettings(this, _window));
	content->resizeToWidth(st::boxWideWidth);
	content->heightValue() | rpl::on_next([=](int height) {
		setDimensions(st::boxWideWidth, std::min(height, st::boxMaxListHeight));
	}, lifetime());
	widthValue() | rpl::on_next([=](int width) {
		content->resizeToWidth(width);
	}, lifetime());
}

void IntroSettingsBox::keyPressEvent(QKeyEvent *e) {
	crl::on_main(this, [text = e->text()] {
		CodesFeedString(nullptr, text);
	});
	Ui::BoxContent::keyPressEvent(e);
}

} // namespace Settings
