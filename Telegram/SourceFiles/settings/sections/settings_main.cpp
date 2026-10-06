/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_main.h"

#include "settings/settings_common_session.h"
#include "settings/settings_card_layout.h"

#include "api/api_cloud_password.h"
#include "api/api_global_privacy.h"
#include "api/api_premium.h"
#include "api/api_sensitive_content.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/platform/base_platform_info.h"
#include "boxes/language_box.h"
#include "boxes/username_box.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/components/promo_suggestions.h"
#include "data/data_chat_filters.h"
#include "data/data_cloud_themes.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "menu/menu_checked_action.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "settings/settings_builder.h"
#include "settings/cloud_password/settings_cloud_password_input.h"
#include "settings/sections/settings_about.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_calls.h"
#include "settings/sections/settings_chat.h"
#include "settings/settings_codes.h"
#include "settings/sections/settings_folders.h"
#include "settings/sections/settings_information.h"
#include "settings/sections/settings_notifications.h"
#include "settings/settings_power_saving.h"
#include "settings/sections/settings_privacy_security.h"
#include "settings/settings_scale_preview.h"
#include "storage/localstorage.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/power_saving.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_extras_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>

// AyuGram includes
#include "extras/ui/settings/settings_main.h"


namespace Settings {
namespace {

using namespace Builder;

constexpr auto kSugValidatePhone = "VALIDATE_PHONE_NUMBER"_cs;

void BuildSectionButtons(SectionBuilder &builder) {
	const auto session = builder.session();
	const auto showOther = builder.showOther();

	builder.addSectionButton({
		.title = tr::extras_Preferences(),
		.targetSection = ExtrasMain::Id(),
		.icon = { &st::menuIconAstraGram },
		.keywords = { u"extras"_q },
	});
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	if (!session->supportMode()) {
		builder.addSectionButton({
			.title = tr::lng_settings_my_account(),
			.targetSection = InformationId(),
			.icon = { &st::menuIconProfile },
			.keywords = { u"profile"_q, u"edit"_q, u"information"_q },
		});
	}

	builder.addSectionButton({
		.title = tr::lng_settings_section_notify(),
		.targetSection = NotificationsId(),
		.icon = { &st::menuIconNotifications },
		.keywords = { u"alerts"_q, u"sounds"_q, u"badge"_q },
	});

	builder.addSectionButton({
		.title = tr::lng_settings_section_privacy(),
		.targetSection = PrivacySecurityId(),
		.icon = { &st::menuIconLock },
		.keywords = { u"security"_q, u"passcode"_q, u"password"_q, u"2fa"_q },
	});
	builder.addDivider();

	builder.addSectionButton({
		.title = tr::lng_settings_section_chat_settings(),
		.targetSection = ChatId(),
		.icon = { &st::menuIconChatBubble },
		.keywords = { u"themes"_q, u"appearance"_q, u"stickers"_q },
	});

	{ // Folders
		const auto preload = [=] {
			session->data().chatsFilters().requestSuggested();
		};
		const auto hasFilters = session->data().chatsFilters().has()
			|| session->settings().dialogsFiltersEnabled();

		auto shownProducer = hasFilters
			? rpl::single(true) | rpl::type_erased
			: (rpl::single(rpl::empty) | rpl::then(
				session->appConfig().refreshed()
			) | rpl::map([=] {
			const auto enabled = session->appConfig().get<bool>(
				u"dialog_filters_enabled"_q,
				false);
			if (enabled) {
				preload();
			}
			return enabled;
		}));

		if (hasFilters) {
			preload();
		}

		builder.addButton({
			.title = tr::lng_settings_section_filters(),
			.icon = { &st::menuIconShowInFolder },
			.onClick = [=] { showOther(FoldersId()); },
			.keywords = { u"filters"_q, u"tabs"_q },
			.shown = std::move(shownProducer),
		});
	}

	builder.addSectionButton({
		.title = tr::lng_settings_advanced(),
		.targetSection = AdvancedId(),
		.icon = { &st::menuIconManage },
		.keywords = { u"performance"_q, u"proxy"_q, u"experimental"_q },
	});

	builder.addSectionButton({
		.title = tr::lng_settings_section_devices(),
		.targetSection = CallsId(),
		.icon = { &st::menuIconUnmute },
		.keywords = { u"sessions"_q, u"calls"_q },
	});
	builder.addDivider();

	builder.addButton({
		.id = u"main/power"_q,
		.title = tr::lng_settings_power_menu(),
		.icon = { &st::menuIconPowerUsage },
		.onClick = [=] {
			showOther(PowerSavingId());
		},
		.keywords = { u"battery"_q, u"animations"_q, u"power"_q, u"saving"_q },
	});

	builder.addButton({
		.id = u"main/language"_q,
		.title = tr::lng_settings_language(),
		.icon = { &st::menuIconLanguage },
		.label = rpl::single(
			Lang::GetInstance().id()
		) | rpl::then(
			Lang::GetInstance().idChanges()
		) | rpl::map([] { return Lang::GetInstance().nativeName(); }),
		.onClick = [=] {
			showOther(LanguageId());
		},
		.keywords = { u"translate"_q, u"localization"_q, u"language"_q },
	});
}

void BuildAppSection(SectionBuilder &builder) {
	builder.addDivider();
	builder.addSectionButton({
		.id = u"main/about"_q,
		.title = tr::lng_menu_about(),
		.targetSection = AboutId(),
		.icon = { &st::menuIconInfo },
		.keywords = { u"about"_q, u"version"_q },
	});

	builder.addSkip();
}

void BuildValidationSuggestions(SectionBuilder &builder) {
	builder.addPageContent([](const WidgetContext &ctx) {
		const auto controller = ctx.controller.get();
		const auto showOther = ctx.showOther;
		SetupValidatePhoneNumberSuggestion(controller, ctx.container, showOther);
	});

	builder.addPageContent([](const WidgetContext &ctx) {
		const auto controller = ctx.controller.get();
		const auto showOther = ctx.showOther;
		SetupValidatePasswordSuggestion(controller, ctx.container, showOther);
	});
}

extern const SectionBuildMethod kMainSection;

class Main final : public Section<Main> {
public:
	Main(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	void fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) override;

protected:
	void keyPressEvent(QKeyEvent *e) override;

private:
	void setupContent();

};

Main::Main(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> Main::title() {
	return tr::lng_menu_settings();
}

void Main::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	FillAccountMenu(controller(), addAction, [=](Type type) { showOther(type); });
}

void Main::keyPressEvent(QKeyEvent *e) {
	crl::on_main(this, [=, text = e->text()]{
		CodesFeedString(controller(), text);
	});
	return Section::keyPressEvent(e);
}

void Main::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto session = &controller()->session();
	build(content, kMainSection);

	Ui::ResizeFitChild(this, content);

	session->api().cloudPassword().reload();
	session->api().reloadContactSignupSilent();
	session->api().sensitiveContent().reload();
	session->api().globalPrivacy().reload();
	session->api().premium().reload();
	session->data().cloudThemes().refresh();
}

const auto kMeta = BuildHelper({
	.id = Main::Id(),
	.parentId = nullptr,
	.title = &tr::lng_menu_settings,
	.icon = &st::menuIconSettings,
}, [](SectionBuilder &builder) {
	builder.addDivider();
	builder.addSkip();

	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"main/profile-photo"_q,
			.title = tr::lng_profile_set_photo_for(tr::now),
			.keywords = { u"photo"_q, u"avatar"_q, u"picture"_q, u"profile"_q },
			.icon = { &st::menuIconProfile },
			.deeplink = u"tg://settings/profile-photo"_q,
		};
	});

	BuildValidationSuggestions(builder);
	BuildSectionButtons(builder);

	builder.addSkip();

	BuildAppSection(builder);
});

const SectionBuildMethod kMainSection = kMeta.build;

} // namespace

void SetupLanguageButton(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container) {
	const auto button = AddButtonWithLabel(
		container,
		tr::lng_settings_language(),
		rpl::single(
			Lang::GetInstance().id()
		) | rpl::then(
			Lang::GetInstance().idChanges()
		) | rpl::map([] { return Lang::GetInstance().nativeName(); }),
		st::settingsButton,
		{ &st::menuIconLanguage });
	const auto guard = Ui::CreateChild<base::binary_guard>(button.get());
	button->addClickHandler([=] {
		const auto m = button->clickModifiers();
		if ((m & Qt::ShiftModifier) && (m & Qt::AltModifier)) {
			Lang::CurrentCloudManager().switchToLanguage({ u"#custom"_q });
		} else {
			*guard = LanguageBox::Show(window->sessionController());
		}
	});
}

void SetupValidatePhoneNumberSuggestion(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	if (!controller->session().promoSuggestions().current(
			kSugValidatePhone.utf8())) {
		return;
	}
	const auto mainWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = AddCardGroup(mainWrap->entity());
	Ui::AddSubsectionTitle(
		content,
		tr::lng_settings_suggestion_phone_number_title(
			lt_phone,
			rpl::single(
				Ui::FormatPhone(controller->session().user()->phone()))),
		QMargins(
			st::boxRowPadding.left()
				- st::defaultSubsectionTitlePadding.left(),
			0,
			0,
			0));
	const auto label = content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_settings_suggestion_phone_number_about(
				lt_link,
				tr::lng_collectible_learn_more(tr::url(
					tr::lng_settings_suggestion_phone_number_about_link(
						tr::now))),
				tr::marked),
			st::boxLabel),
		st::boxRowPadding);
	label->setClickHandlerFilter([=, weak = base::make_weak(controller)](
			const auto &...) {
		UrlClickHandler::Open(
			tr::lng_settings_suggestion_phone_number_about_link(tr::now),
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = weak,
			}));
		return false;
	});

	Ui::AddSkip(content);
	Ui::AddSkip(content);

	const auto wrap = content->add(
		object_ptr<Ui::FixedHeightWidget>(
			content,
			st::inviteLinkButton.height),
		st::inviteLinkButtonsPadding);
	const auto yes = Ui::CreateChild<Ui::RoundButton>(
		wrap,
		tr::lng_box_yes(),
		st::inviteLinkButton);
	yes->setFullRadius(true);
	yes->setClickedCallback([=] {
		controller->session().promoSuggestions().dismiss(
			kSugValidatePhone.utf8());
		mainWrap->toggle(false, anim::type::normal);
	});
	const auto no = Ui::CreateChild<Ui::RoundButton>(
		wrap,
		tr::lng_box_no(),
		st::inviteLinkButton);
	no->setFullRadius(true);
	no->setClickedCallback([=] {
		const auto sharedLabel = std::make_shared<base::weak_qptr<Ui::FlatLabel>>();
		const auto height = st::boxLabel.style.font->height;
		const auto customEmojiFactory = [=](
			QStringView data,
			const Ui::Text::MarkedContext &context
		) -> std::unique_ptr<Ui::Text::CustomEmoji> {
			auto repaint = [=] {
				if (*sharedLabel) {
					(*sharedLabel)->update();
				}
			};
			return Lottie::MakeEmoji(
				{ .name = u"change_number"_q, .sizeOverride = Size(height) },
				std::move(repaint));
		};

		controller->uiShow()->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
			*sharedLabel = box->verticalLayout()->add(
				object_ptr<Ui::FlatLabel>(
					box->verticalLayout(),
					tr::lng_settings_suggestion_phone_number_change(
						lt_emoji,
						rpl::single(Ui::Text::SingleCustomEmoji(u"@"_q)),
						tr::marked),
					st::boxLabel,
					st::defaultPopupMenu,
					Ui::Text::MarkedContext{
						.customEmojiFactory = customEmojiFactory,
					}),
				st::boxPadding);
		}));
	});

	wrap->widthValue() | rpl::on_next([=](int width) {
		const auto buttonWidth = (width - st::inviteLinkButtonsSkip) / 2;
		yes->setFullWidth(buttonWidth);
		no->setFullWidth(buttonWidth);
		yes->moveToLeft(0, 0, width);
		no->moveToRight(0, 0, width);
	}, wrap->lifetime());
	Ui::AddSkip(content, st::settingsCardRowInset);
}

void SetupValidatePasswordSuggestion(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	if (!controller->session().promoSuggestions().current(
			Data::PromoSuggestions::SugValidatePassword())
		|| controller->session().promoSuggestions().current(
			kSugValidatePhone.utf8())) {
		return;
	}
	const auto mainWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = AddCardGroup(mainWrap->entity());
	Ui::AddSubsectionTitle(
		content,
		tr::lng_settings_suggestion_password_title(),
		QMargins(
			st::boxRowPadding.left()
				- st::defaultSubsectionTitlePadding.left(),
			0,
			0,
			0));
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_settings_suggestion_password_about(),
			st::boxLabel),
		st::boxRowPadding);

	Ui::AddSkip(content);
	Ui::AddSkip(content);

	const auto wrap = content->add(
		object_ptr<Ui::FixedHeightWidget>(
			content,
			st::inviteLinkButton.height),
		st::inviteLinkButtonsPadding);
	const auto yes = Ui::CreateChild<Ui::RoundButton>(
		wrap,
		tr::lng_settings_suggestion_password_yes(),
		st::inviteLinkButton);
	yes->setFullRadius(true);
	yes->setClickedCallback([=] {
		controller->session().promoSuggestions().dismiss(
			Data::PromoSuggestions::SugValidatePassword());
		mainWrap->toggle(false, anim::type::normal);
	});
	const auto no = Ui::CreateChild<Ui::RoundButton>(
		wrap,
		tr::lng_settings_suggestion_password_no(),
		st::inviteLinkButton);
	no->setFullRadius(true);
	no->setClickedCallback([=] {
		showOther(Settings::CloudPasswordSuggestionInputId());
	});

	wrap->widthValue() | rpl::on_next([=](int width) {
		const auto buttonWidth = (width - st::inviteLinkButtonsSkip) / 2;
		yes->setFullWidth(buttonWidth);
		no->setFullWidth(buttonWidth);
		yes->moveToLeft(0, 0, width);
		no->moveToRight(0, 0, width);
	}, wrap->lifetime());
	Ui::AddSkip(content, st::settingsCardRowInset);
}

bool HasInterfaceScale() {
	return true;
}

void SetupInterfaceScale(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container,
		bool icon) {
	if (!HasInterfaceScale()) {
		return;
	}

	const auto toggled = Ui::CreateChild<rpl::event_stream<bool>>(
		container.get());

	const auto switched = (cConfigScale() == style::kScaleAuto);
	const auto button = AddButtonWithIcon(
		container,
		tr::lng_settings_default_scale(),
		icon ? st::settingsButton : st::settingsButtonNoIcon,
		{ icon ? &st::menuIconShowInChat : nullptr }
	)->toggleOn(toggled->events_starting_with_copy(switched));

	const auto ratio = style::DevicePixelRatio();
	const auto scaleMin = style::kScaleMin;
	const auto scaleMax = style::MaxScaleForRatio(ratio);
	const auto scaleConfig = cConfigScale();
	const auto step = 5;
	Assert(!((scaleMax - scaleMin) % step));
	auto values = std::vector<int>();
	for (auto i = scaleMin; i != scaleMax; i += step) {
		values.push_back(i);
		if (scaleConfig > i && scaleConfig < i + step) {
			values.push_back(scaleConfig);
		}
	}
	values.push_back(scaleMax);
	const auto valuesCount = int(values.size());

	const auto valueFromScale = [=](int scale) {
		scale = cEvalScale(scale);
		auto result = 0;
		for (const auto value : values) {
			if (scale == value) {
				break;
			}
			++result;
		}
		return ((result == valuesCount) ? (result - 1) : result)
			/ float64(valuesCount - 1);
	};
	auto sliderWithLabel = MakeSliderWithLabel(
		container,
		st::settingsScale,
		st::settingsScaleLabel,
		st::normalFont->spacew * 2,
		st::settingsScaleLabel.style.font->width("300%"),
		true);
	container->add(
		std::move(sliderWithLabel.widget),
		icon ? st::settingsScalePadding : st::settingsBigScalePadding);
	const auto slider = sliderWithLabel.slider;
	const auto label = sliderWithLabel.label;
	slider->setAccessibleName(tr::lng_settings_scale(tr::now));

	const auto updateLabel = [=](int scale) {
		const auto labelText = [&](int scale) {
			if constexpr (Platform::IsMac()) {
				return QString::number(scale) + '%';
			} else {
				const auto handle = window->widget()->windowHandle();
				const auto ratio = handle->devicePixelRatio();
				return QString::number(base::SafeRound(scale * ratio)) + '%';
			}
		};
		label->setText(labelText(cEvalScale(scale)));
	};
	updateLabel(cConfigScale());

	const auto inSetScale = container->lifetime().make_state<bool>();
	const auto setScale = [=](int scale, const auto &repeatSetScale) -> void {
		if (*inSetScale) {
			return;
		}
		*inSetScale = true;
		const auto guard = gsl::finally([=] { *inSetScale = false; });

		updateLabel(scale);
		toggled->fire(scale == style::kScaleAuto);
		slider->setValue(valueFromScale(scale));
		if (cEvalScale(scale) != cEvalScale(cConfigScale())) {
			const auto confirmed = crl::guard(button, [=] {
				cSetConfigScale(scale);
				Local::writeSettings();
				Core::Restart();
			});
			const auto cancelled = crl::guard(button, [=](Fn<void()> close) {
				base::call_delayed(
					st::defaultSettingsSlider.duration,
					button,
					[=] { repeatSetScale(cConfigScale(), repeatSetScale); });
				close();
			});
			window->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = confirmed,
				.cancelled = cancelled,
				.confirmText = tr::lng_settings_restart_now(),
			}));
		} else if (scale != cConfigScale()) {
			cSetConfigScale(scale);
			Local::writeSettings();
		}
	};

	const auto shown = container->lifetime().make_state<bool>();
	const auto togglePreview = SetupScalePreview(window, slider);
	const auto toggleForScale = [=](int scale) {
		scale = cEvalScale(scale);
		const auto show = *shown
			? ScalePreviewShow::Update
			: ScalePreviewShow::Show;
		*shown = true;
		for (auto i = 0; i != valuesCount; ++i) {
			if (values[i] <= scale
				&& (i + 1 == valuesCount || values[i + 1] > scale)) {
				const auto x = (slider->width() * i) / (valuesCount - 1);
				togglePreview(show, scale, x);
				return;
			}
		}
		togglePreview(show, scale, slider->width() / 2);
	};
	const auto toggleHidePreview = [=] {
		togglePreview(ScalePreviewShow::Hide, 0, 0);
		*shown = false;
	};

	slider->setPseudoDiscrete(
		valuesCount,
		[=](int index) { return values[index]; },
		cConfigScale(),
		[=](int scale) { updateLabel(scale); toggleForScale(scale); },
		[=](int scale) { toggleHidePreview(); setScale(scale, setScale); });

	button->toggledValue(
	) | rpl::map([](bool checked) {
		return checked ? style::kScaleAuto : cEvalScale(cConfigScale());
	}) | rpl::on_next([=](int scale) {
		setScale(scale, setScale);
	}, button->lifetime());

	if (!icon) {
		Ui::AddSkip(container, st::settingsThumbSkip);
	}
}

Type MainId() {
	return Main::Id();
}

void OpenFaq(base::weak_ptr<Window::SessionController> weak) {
	UrlClickHandler::Open(
		tr::lng_settings_faq_link(tr::now),
		QVariant::fromValue(ClickHandlerContext{
			.sessionWindow = weak,
			.ignoreIv = true,
		}));
}

void OpenAskQuestionConfirm(not_null<Window::SessionController*> window) {
	const auto requestId = std::make_shared<mtpRequestId>();
	const auto sure = [=](Fn<void()> close) {
		if (*requestId) {
			return;
		}
		*requestId = window->session().api().request(
			MTPhelp_GetSupport()
		).done(crl::guard(window, [=](const MTPhelp_Support &result) {
			*requestId = 0;
			result.match([&](const MTPDhelp_support &data) {
				auto &owner = window->session().data();
				if (const auto user = owner.processUser(data.vuser())) {
					window->showPeerHistory(user);
				}
			});
			close();
		})).fail([=] {
			*requestId = 0;
			close();
		}).send();
	};
	window->show(Ui::MakeConfirmBox({
		.text = tr::lng_settings_ask_sure(),
		.confirmed = sure,
		.cancelled = [=](Fn<void()> close) {
			OpenFaq(window);
			close();
		},
		.confirmText = tr::lng_settings_ask_ok(),
		.cancelText = tr::lng_settings_faq_button(),
		.strictCancel = true,
	}));
}

void FillAccountMenu(
		not_null<Window::SessionController*> controller,
		const Ui::Menu::MenuCallback &addAction,
		Fn<void(Type)> showOther) {
	const auto &list = Core::App().domain().accounts();
	if (list.size() < Core::App().domain().maxAccounts()) {
		addAction(tr::lng_menu_add_account(tr::now), [=] {
			Core::App().setActivePrimaryWindow(&controller->window());
			Core::App().domain().addActivated(MTP::Environment{});
		}, &st::menuIconAddAccount);
	}
	if (!controller->session().supportMode()) {
		addAction(
			tr::lng_settings_information(tr::now),
			[=] { showOther(InformationId()); },
			&st::menuIconEdit);
	}
	const auto window = &controller->window();
	const auto logout = addAction({
		.text = tr::lng_settings_logout(tr::now),
		.handler = [=] { window->showLogoutConfirmation(); },
		.icon = &st::menuIconLeaveAttention,
		.isAttention = true,
	});
	logout->setProperty("highlight-control-id", u"settings/log-out"_q);
}

} // namespace Settings
