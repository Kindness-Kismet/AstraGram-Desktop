#include "extras/ui/settings/settings_main.h"

#include "core/version.h"
#include "extras/ui/settings/settings_content.h"
#include "extras/extras_settings.h"
#include "extras/ui/settings/settings_appearance.h"
#include "extras/ui/settings/settings_chats.h"
#include "extras/ui/settings/settings_debug.h"
#include "extras/ui/settings/settings_extras.h"
#include "extras/ui/settings/settings_filters.h"
#include "extras/ui/settings/settings_general.h"
#include "extras/ui/settings/settings_other.h"
#include "extras/ui/settings/settings_transfer.h"
#include "lang_auto.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "styles/style_menu_icons.h"
#include "styles/style_extras_icons.h"
#include "styles/style_settings.h"
#include "ui/widgets/buttons.h"
#include "ui/text/format_values.h"
#include "ui/wrap/vertical_layout.h"
#include "settings.h"

#include <QFileInfo>

namespace Settings {

using namespace Builder;

namespace {

void buildCategories(SectionBuilder &builder) {
	const auto dev = ExtrasSettings::getInstance().devFeaturesEnabled();
	if (dev) {
		builder.addSectionButton({
			.id = u"extras/cat/ghost"_q,
			.title = tr::extras_GhostModeToggle(),
			.targetSection = ExtrasGhost::Id(),
			.icon = { &st::menuIconStealth },
			.description = tr::extras_SettingsGhostDescription(),
			.label = ExtrasSettings::getInstance().useGlobalGhostModeValue(
			) | rpl::map([session = builder.session()](bool) {
				return ExtrasSettings::ghost(session).ghostModeActiveValue();
			}) | rpl::flatten_latest() | rpl::map([](bool enabled) {
				return enabled ? tr::extras_SettingsStateOn(tr::now)
					: tr::extras_SettingsStateOff(tr::now);
			}),
		});
		builder.addSectionButton({
			.id = u"extras/cat/archive"_q,
			.title = tr::extras_SettingsArchiveTitle(),
			.targetSection = ExtrasArchive::Id(),
			.icon = { &st::menuIconArchive },
			.description = tr::extras_SettingsArchiveDescription(),
			.label = rpl::single(Ui::FormatSizeText(
				QFileInfo(cWorkingDir() + u"tdata/extrasdata.db"_q).size())),
		});
		builder.addSectionButton({
			.id = u"extras/cat/filters"_q,
			.title = tr::extras_CategoryFilters(),
			.targetSection = ExtrasFilters::Id(),
			.icon = { &st::menuIconTagFilter },
			.description = tr::extras_SettingsFiltersDescription(),
		});
	}
	builder.addSectionButton({
		.id = u"extras/cat/text"_q,
		.title = tr::extras_SettingsTextTitle(),
		.targetSection = ExtrasText::Id(),
		.icon = { &st::menuIconEdit },
		.description = tr::extras_SettingsTextDescription(),
	});
	builder.addSectionButton({
		.id = u"extras/cat/appearance"_q,
		.title = tr::extras_SettingsAppearanceTitle(),
		.targetSection = ExtrasAppearance::Id(),
		.icon = { &st::menuIconPalette },
		.description = tr::extras_SettingsAppearanceDescription(),
	});

}

const auto kMeta = BuildHelper({
	.id = ExtrasMain::Id(),
	.parentId = MainId(),
	.title = &tr::extras_Preferences,
	.icon = &st::menuIconAstraGram,
}, [](SectionBuilder &builder) {
	buildCategories(builder);
	builder.addSubsectionTitle(tr::extras_SettingsMoreTitle());
	builder.addSectionButton({
		.id = u"extras/cat/general"_q,
		.title = tr::extras_CategoryGeneral(),
		.targetSection = ExtrasGeneral::Id(),
		.icon = { &st::menuIconShowAll },
		.description = tr::extras_SettingsGeneralDescription(),
	});
	builder.addSectionButton({
		.id = u"extras/cat/chats"_q,
		.title = tr::extras_CategoryChats(),
		.targetSection = ExtrasChats::Id(),
		.icon = { &st::menuIconChatBubble },
		.description = tr::extras_SettingsChatsDescription(),
	});
	builder.addSectionButton({
		.id = u"extras/cat/other"_q,
		.title = tr::extras_CategoryOther(),
		.targetSection = ExtrasOther::Id(),
		.icon = { &st::menuIconFave },
		.description = tr::extras_SettingsOtherDescription(),
	});

	addSettingsTransferButtons(builder);

	if (DebugEntryVisible()) {
		builder.addSubsectionTitle(tr::extras_SettingsDebugTitle());
		builder.addSectionButton({
			.id = u"extras/cat/debug"_q,
			.title = tr::extras_SettingsDebugTitle(),
			.targetSection = ExtrasDebug::Id(),
			.icon = { &st::menuIconStats },
		});
	}
	builder.addDividerText(tr::extras_SettingsDescription(
	) | rpl::map([](const QString &description) {
		return u"AstraGram v"_q
			+ QString::fromLatin1(AppVersionStr)
			+ u" · "_q + description;
	}));
});

} // namespace

rpl::producer<QString> ExtrasMain::title() {
	return tr::extras_Preferences();
}

ExtrasMain::ExtrasMain(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void ExtrasMain::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type ExtrasMainId() {
	return ExtrasMain::Id();
}

} // namespace Settings
