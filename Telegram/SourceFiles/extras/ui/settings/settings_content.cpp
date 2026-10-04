#include "extras/ui/settings/settings_content.h"

#include "extras/extras_settings.h"
#include "extras/ui/settings/extras_builder.h"
#include "extras/ui/settings/settings_extras_utils.h"
#include "extras/ui/settings/settings_main.h"
#include "lang_auto.h"
#include "settings/settings_builder.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"

namespace Settings {
using namespace Builder;
using namespace ExtrasBuilder;
namespace {

void buildArchive(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	builder.addSubsectionTitle(tr::extras_SpyEssentialsHeader());

	extras.addSettingToggle({
		.id = u"extras/saveDeletedMessages"_q,
		.title = tr::extras_SaveDeletedMessages(),
		.getter = &ExtrasSettings::saveDeletedMessages,
		.setter = &ExtrasSettings::setSaveDeletedMessages,
	});
	extras.addSettingToggle({
		.id = u"extras/saveMessagesHistory"_q,
		.title = tr::extras_SaveMessagesHistory(),
		.getter = &ExtrasSettings::saveMessagesHistory,
		.setter = &ExtrasSettings::setSaveMessagesHistory,
	});

	extras.addSectionDivider();

	extras.addSettingToggle({
		.id = u"extras/saveForBots"_q,
		.title = tr::extras_MessageSavingSaveForBots(),
		.getter = &ExtrasSettings::saveForBots,
		.setter = &ExtrasSettings::setSaveForBots,
	});
}

void buildText(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	const auto settings = &ExtrasSettings::getInstance();
	const auto controller = builder.controller();
	const auto zalgoButton = builder.addButton({
		.id = u"extras/filterZalgo"_q,
		.title = tr::extras_FilterZalgo(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filterZalgo()),
	});
	if (zalgoButton) {
		zalgoButton->toggledValue(
		) | rpl::filter(
			[=](bool enabled) {
				return (enabled != settings->filterZalgo());
			}
		) | on_next(
			[=](bool enabled) {
				ExtrasSettings::getInstance().setFilterZalgo(enabled);
				ShowRestartPrompt(controller);
			},
			zalgoButton->lifetime());
		extras.addBetaBadge(zalgoButton);
	}

	extras.addSettingToggle({
		.id = u"extras/autoSpaceSending"_q,
		.title = tr::extras_AutoSpaceSending(),
		.getter = &ExtrasSettings::autoSpaceSending,
		.setter = &ExtrasSettings::setAutoSpaceSending,
	});
	extras.addSettingToggle({
		.id = u"extras/autoSpaceEditing"_q,
		.title = tr::extras_AutoSpaceEditing(),
		.getter = &ExtrasSettings::autoSpaceEditing,
		.setter = &ExtrasSettings::setAutoSpaceEditing,
	});
	const auto autoSpaceReceivingButton = builder.addButton({
		.id = u"extras/autoSpaceReceiving"_q,
		.title = tr::extras_AutoSpaceReceiving(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->autoSpaceReceiving()),
	});
	if (autoSpaceReceivingButton) {
		autoSpaceReceivingButton->toggledValue(
		) | rpl::filter(
			[=](bool enabled) {
				return (enabled != settings->autoSpaceReceiving());
			}
		) | on_next(
			[=](bool enabled) {
				ExtrasSettings::getInstance().setAutoSpaceReceiving(enabled);
				ShowRestartPrompt(controller);
			},
			autoSpaceReceivingButton->lifetime());
	}

}

const auto kExtrasArchiveMeta = BuildHelper({
	.id = ExtrasArchive::Id(),
	.parentId = ExtrasMain::Id(),
	.title = &tr::extras_SettingsArchiveTitle,
	.icon = &st::menuIconArchive,
}, [](SectionBuilder &builder) {
	if (!ExtrasSettings::getInstance().devFeaturesEnabled()) {
		return;
	}
	auto extras = ExtrasSectionBuilder(builder);
	buildArchive(builder, extras);
});

const auto kExtrasTextMeta = BuildHelper({
	.id = ExtrasText::Id(),
	.parentId = ExtrasMain::Id(),
	.title = &tr::extras_SettingsTextTitle,
	.icon = &st::menuIconEdit,
}, [](SectionBuilder &builder) {
	auto extras = ExtrasSectionBuilder(builder);
	buildText(builder, extras);
});

} // namespace

ExtrasArchive::ExtrasArchive(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kExtrasArchiveMeta.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> ExtrasArchive::title() {
	return tr::extras_SettingsArchiveTitle();
}

ExtrasText::ExtrasText(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kExtrasTextMeta.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> ExtrasText::title() {
	return tr::extras_SettingsTextTitle();
}

} // namespace Settings
