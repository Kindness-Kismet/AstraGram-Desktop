#include "extras/ui/settings/settings_appearance.h"

#include "lang_auto.h"
#include "extras/extras_settings.h"
#include "extras/extras_ui_settings.h"
#include "extras/ui/boxes/font_selector.h"
#include "extras/ui/components/avatar_corners_preview.h"
#include "extras/ui/components/icon_picker.h"
#include "extras/ui/settings/extras_builder.h"
#include "extras/ui/settings/settings_extras_utils.h"
#include "extras/ui/settings/settings_main.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_extras_icons.h"
#include "styles/style_extras_styles.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/painter.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace ExtrasBuilder;

namespace {

void editAppNameBox(not_null<Ui::GenericBox*> box) {
	box->setObjectName(u"customAppNameBox"_q);
	box->setTitle(tr::extras_CustomAppName());
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::SingleLine,
		rpl::single(u"AstraGram"_q),
		ExtrasSettings::getInstance().customAppName()));
	field->setObjectName(u"customAppNameInput"_q);
	field->selectAll();
	box->addRow(object_ptr<Ui::FlatLabel>(
		box, tr::extras_CustomAppNameDescription(), st::boxLabel));
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	const auto save = [=] {
		ExtrasSettings::getInstance().setCustomAppName(field->getLastText());
		box->closeBox();
	};
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->addLeftButton(tr::extras_BoxActionReset(), [=] {
		field->setText(QString());
		field->setFocusFast();
	});
	field->submits() | rpl::on_next(save, box->lifetime());
}

void buildAppName(SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"extras/customAppName"_q,
		.title = tr::extras_CustomAppName(),
		.st = &st::settingsButtonNoIcon,
		.label = ExtrasSettings::getInstance().customAppNameValue()
			| rpl::map([] {
				return ExtrasSettings::getInstance().appDisplayName();
			}),
		.onClick = [=] {
			controller->show(Box(editAppNameBox));
		},
	});
}

bool HasDrawerBots(not_null<Window::SessionController*> controller) {
	// todo: maybe iterate through all accounts
	const auto bots = &controller->session().attachWebView();
	for (const auto &bot : bots->attachBots()) {
		if (!bot.inMainMenu || !bot.media) {
			continue;
		}
		return true;
	}
	return false;
}

void BuildAppIcon(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	builder.addSubsectionTitle({
		.id = u"extras/appIcon"_q,
		.title = tr::extras_AppIconHeader(),
	});

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<IconPicker>(ctx.container),
			.margin = st::settingsButtonNoIcon.padding,
		};
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	builder.addDivider();
	builder.addSkip();
	extras.addSettingToggle({
		.id = u"extras/hideNotificationBadge"_q,
		.title = tr::extras_HideNotificationBadge(),
		.getter = &ExtrasSettings::hideNotificationBadge,
		.setter = &ExtrasSettings::setHideNotificationBadge,
	});
	builder.addSkip();
	builder.addDividerText(tr::extras_HideNotificationBadgeDescription());
	builder.addSkip();
#else
    builder.addDivider();
    builder.addSkip();
#endif
}

void BuildAvatarCorners(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	auto *settings = &ExtrasSettings::getInstance();
	const auto controller = builder.controller();

	const auto mapRadius = [](int val)
	{
		if (val == 0) {
			return tr::extras_AvatarCornersSquare(tr::now).toUpper();
		}
		if (val == ExtrasUiSettings::kMaxAvatarCorners) {
			return tr::extras_AvatarCornersCircle(tr::now).toUpper();
		}
		return QString::number(val);
	};

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto container = ctx.container;
		auto title = object_ptr<Ui::FlatLabel>(
			container,
			tr::extras_AvatarCorners(),
			st::defaultSubsectionTitle);
		const auto titleRaw = title.data();

		const auto badge = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				settings->avatarCornersValue() | rpl::map(mapRadius),
				st::settingsPremiumNewBadge),
			st::extrasBetaBadgePadding);
		badge->show();
		badge->setAttribute(Qt::WA_TransparentForMouseEvents);
		badge->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(badge);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			const auto r = st::extrasBetaBadgePadding.left();
			p.drawRoundedRect(badge->rect(), r, r);
		}, badge->lifetime());

		titleRaw->geometryValue() | rpl::on_next([=](QRect geometry) {
			badge->moveToLeft(
				geometry.x()
					+ titleRaw->textMaxWidth()
					+ st::settingsPremiumNewBadgePosition.x(),
				geometry.y()
					+ (geometry.height() - badge->height()) / 2);
		}, badge->lifetime());

		return {
			.widget = std::move(title),
			.margin = st::defaultSubsectionTitlePadding,
		};
	}, [] {
		return SearchEntry{
			.id = u"extras/avatarCorners"_q,
			.title = tr::extras_AvatarCorners(tr::now),
		};
	});

	auto *previewRaw = static_cast<AvatarCornersPreview*>(nullptr);
	builder.add([&](const Builder::WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto preview = object_ptr<AvatarCornersPreview>(
			ctx.container,
			controller);
		previewRaw = preview.data();
		const auto vMargin = st::settingsButtonNoIcon.padding
			- st::defaultDialogRow.padding;
		return {
			.widget = std::move(preview),
			.margin = QMargins(0, vMargin.top(), 0, vMargin.bottom()),
		};
	});

	extras.addSlider({
		.id = u"extras/avatarCornersSlider"_q,
		.title = rpl::single(QString()),
		.showTitle = false,
		.steps = ExtrasUiSettings::kMaxAvatarCorners + 1,
		.current = settings->avatarCorners(),
		.onChanged = [=](int val) {
			ExtrasSettings::getInstance().setAvatarCorners(val);
			if (previewRaw) {
				previewRaw->update();
			}
		},
		.onFinalChanged = [=](int val) {
			ExtrasSettings::getInstance().setAvatarCorners(val);
			ShowRestartPrompt(controller);
		},
	});

	extras.addSettingToggle({
		.id = u"extras/singleCornerRadius"_q,
		.title = tr::extras_SingleCornerRadius(),
		.getter = &ExtrasSettings::singleCornerRadius,
		.setter = &ExtrasSettings::setSingleCornerRadius,
	});

	builder.addSkip();
	builder.addDividerText(tr::extras_SingleCornerRadiusDescription());
	builder.addSkip();
}

QString horizontalTabStyleLabel(HorizontalTabStyle style) {
	switch (style) {
	case HorizontalTabStyle::Default: return tr::extras_HorizontalTabsDefault(tr::now);
	case HorizontalTabStyle::Outline: return tr::extras_HorizontalTabsOutline(tr::now);
	case HorizontalTabStyle::Solid: return tr::extras_HorizontalTabsSolid(tr::now);
	}
	Unexpected("Invalid horizontal tab style");
}

void buildHorizontalTabStyle(SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"extras/horizontalTabStyle"_q,
		.title = tr::extras_HorizontalTabsStyle(),
		.st = &st::settingsButtonNoIcon,
		.label = ExtrasSettings::getInstance().horizontalTabStyleValue()
			| rpl::map(horizontalTabStyleLabel),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = tr::extras_HorizontalTabsStyle(),
					.options = {
						tr::extras_HorizontalTabsDefault(tr::now),
						tr::extras_HorizontalTabsOutline(tr::now),
						tr::extras_HorizontalTabsSolid(tr::now),
					},
					.initialSelection = int(ExtrasSettings::getInstance().horizontalTabStyle()),
					.callback = [](int index) {
						ExtrasSettings::getInstance().setHorizontalTabStyle(HorizontalTabStyle(index));
					},
				});
				box->addRow(object_ptr<Ui::FlatLabel>(box,
					tr::extras_HorizontalTabsDescription(), st::boxLabel));
			}));
		},
	});
}

void BuildAppearance(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	auto *settings = &ExtrasSettings::getInstance();

	builder.addSubsectionTitle(tr::extras_CategoryAppearance());
	buildAppName(builder);
	buildHorizontalTabStyle(builder);

	extras.addSettingToggle({
		.id = u"extras/hidePremiumStatuses"_q,
		.title = tr::extras_HidePremiumStatuses(),
		.getter = &ExtrasSettings::hidePremiumStatuses,
		.setter = &ExtrasSettings::setHidePremiumStatuses,
	});
	extras.addSettingToggle({
		.id = u"extras/hideProxySettingsIcon"_q,
		.title = tr::extras_HideProxySettingsIcon(),
		.getter = &ExtrasSettings::hideProxySettingsIcon,
		.setter = &ExtrasSettings::setHideProxySettingsIcon,
	});
	extras.addSettingToggle({
		.id = u"extras/showDownloadsButtonInHeader"_q,
		.title = tr::extras_ShowDownloadsButtonInHeader(),
		.getter = &ExtrasSettings::showDownloadsButtonInHeader,
		.setter = &ExtrasSettings::setShowDownloadsButtonInHeader,
	});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"extras/monoFont"_q,
		.title = tr::extras_MonospaceFont(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(
			settings->monoFont().isEmpty()
				? tr::extras_FontDefault(tr::now)
				: settings->monoFont()),
		.onClick = [=] {
			ExtrasUi::FontSelectorBox::Show(
				controller,
				[=](const QString &font) {
					ExtrasSettings::getInstance().setMonoFont(font);
				});
		},
	});

	extras.addSectionDivider();
}

void BuildChatFolders(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	builder.addSubsectionTitle(tr::extras_ChatFoldersHeader());

	extras.addSettingToggle({
		.id = u"extras/hideNotificationCounters"_q,
		.altIds = { u"extras/tabCounter"_q },
		.title = tr::extras_HideNotificationCounters(),
		.getter = &ExtrasSettings::hideNotificationCounters,
		.setter = &ExtrasSettings::setHideNotificationCounters,
	});
	extras.addSettingToggle({
		.id = u"extras/hideAllChatsFolder"_q,
		.altIds = { u"extras/hideAllChats"_q },
		.title = tr::extras_HideAllChats(),
		.getter = &ExtrasSettings::hideAllChatsFolder,
		.setter = &ExtrasSettings::setHideAllChatsFolder,
	});

	extras.addSectionDivider();
}

void BuildTrayElements(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	builder.addSubsectionTitle(tr::extras_TrayElementsHeader());

	extras.addSettingToggle({
		.id = u"extras/showGhostToggleInTray"_q,
		.title = tr::extras_EnableGhostModeTray(),
		.getter = &ExtrasSettings::showGhostToggleInTray,
		.setter = &ExtrasSettings::setShowGhostToggleInTray,
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	extras.addSettingToggle({
		.id = u"extras/showStreamerToggleInTray"_q,
		.title = tr::extras_EnableStreamerModeTray(),
		.getter = &ExtrasSettings::showStreamerToggleInTray,
		.setter = &ExtrasSettings::setShowStreamerToggleInTray,
	});
#endif

	extras.addSectionDivider();
}

void BuildDrawerElements(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	builder.addSubsectionTitle(tr::extras_DrawerElementsHeader());

	extras.addSettingToggle({
		.id = u"extras/showMyProfileInDrawer"_q,
		.title = tr::lng_menu_my_profile(),
		.getter = &ExtrasSettings::showMyProfileInDrawer,
		.setter = &ExtrasSettings::setShowMyProfileInDrawer,
		.icon = { &st::menuIconProfile },
	});

	const auto controller = builder.controller();
	if (controller && HasDrawerBots(controller)) {
		extras.addSettingToggle({
			.id = u"extras/showBotsInDrawer"_q,
			.title = tr::lng_filters_type_bots(),
			.getter = &ExtrasSettings::showBotsInDrawer,
			.setter = &ExtrasSettings::setShowBotsInDrawer,
			.icon = { &st::menuIconBot },
		});
	}

	extras.addSettingToggle({
		.id = u"extras/showNewGroupInDrawer"_q,
		.title = tr::lng_create_group_title(),
		.getter = &ExtrasSettings::showNewGroupInDrawer,
		.setter = &ExtrasSettings::setShowNewGroupInDrawer,
		.icon = { &st::menuIconGroups },
	});
	extras.addSettingToggle({
		.id = u"extras/showNewChannelInDrawer"_q,
		.title = tr::lng_create_channel_title(),
		.getter = &ExtrasSettings::showNewChannelInDrawer,
		.setter = &ExtrasSettings::setShowNewChannelInDrawer,
		.icon = { &st::menuIconChannel },
	});
	extras.addSettingToggle({
		.id = u"extras/showContactsInDrawer"_q,
		.title = tr::lng_menu_contacts(),
		.getter = &ExtrasSettings::showContactsInDrawer,
		.setter = &ExtrasSettings::setShowContactsInDrawer,
		.icon = { &st::menuIconUserShow },
	});
	extras.addSettingToggle({
		.id = u"extras/showCallsInDrawer"_q,
		.title = tr::lng_menu_calls(),
		.getter = &ExtrasSettings::showCallsInDrawer,
		.setter = &ExtrasSettings::setShowCallsInDrawer,
		.icon = { &st::menuIconPhone },
	});
	extras.addSettingToggle({
		.id = u"extras/showSavedMessagesInDrawer"_q,
		.title = tr::lng_saved_messages(),
		.getter = &ExtrasSettings::showSavedMessagesInDrawer,
		.setter = &ExtrasSettings::setShowSavedMessagesInDrawer,
		.icon = { &st::menuIconSavedMessages },
	});
	extras.addSettingToggle({
		.id = u"extras/showArchiveInDrawer"_q,
		.title = tr::extras_ArchiveChatsInMenu(),
		.getter = &ExtrasSettings::showArchiveInDrawer,
		.setter = &ExtrasSettings::setShowArchiveInDrawer,
		.icon = { &st::menuIconArchive },
	});
	extras.addSettingToggle({
		.id = u"extras/showDonationDetailsInDrawer"_q,
		.title = tr::extras_DonationDetails(),
		.getter = &ExtrasSettings::showDonationDetailsInDrawer,
		.setter = &ExtrasSettings::setShowDonationDetailsInDrawer,
		.icon = { &st::menuIconGiftPremium },
	});
	extras.addSettingToggle({
		.id = u"extras/showNightModeToggleInDrawer"_q,
		.title = tr::lng_menu_night_mode(),
		.getter = &ExtrasSettings::showNightModeToggleInDrawer,
		.setter = &ExtrasSettings::setShowNightModeToggleInDrawer,
		.icon = { &st::menuIconNightMode },
	});
	extras.addSettingToggle({
		.id = u"extras/showGhostToggleInDrawer"_q,
		.title = tr::extras_GhostModeToggle(),
		.getter = &ExtrasSettings::showGhostToggleInDrawer,
		.setter = &ExtrasSettings::setShowGhostToggleInDrawer,
		.icon = { &st::extrasGhostIcon },
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	extras.addSettingToggle({
		.id = u"extras/showStreamerToggleInDrawer"_q,
		.title = tr::extras_StreamerModeToggle(),
		.getter = &ExtrasSettings::showStreamerToggleInDrawer,
		.setter = &ExtrasSettings::setShowStreamerToggleInDrawer,
		.icon = { &st::extrasStreamerModeMenuIcon },
	});
#endif

	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = ExtrasAppearance::Id(),
	.parentId = ExtrasMain::Id(),
	.title = &tr::extras_CategoryAppearance,
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	auto extras = ExtrasSectionBuilder(builder);

	builder.addSkip();
	BuildAppIcon(builder, extras);
	BuildAvatarCorners(builder, extras);
	BuildAppearance(builder, extras);
	BuildChatFolders(builder, extras);
	BuildTrayElements(builder, extras);
	BuildDrawerElements(builder, extras);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> ExtrasAppearance::title() {
	return tr::extras_CategoryAppearance();
}

ExtrasAppearance::ExtrasAppearance(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void ExtrasAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type ExtrasAppearanceId() {
	return ExtrasAppearance::Id();
}

} // namespace Settings
