#include "extras/ui/settings/settings_main.h"

#include "settings/sections/settings_main.h"
#include "lang_auto.h"
#include "extras/extras_settings.h"
#include "extras/ui/extras_logo.h"
#include "extras/ui/settings/settings_appearance.h"
#include "extras/ui/settings/settings_extras.h"
#include "extras/ui/settings/settings_chats.h"
#include "extras/ui/settings/settings_debug.h"
#include "extras/ui/settings/settings_filters.h"
#include "extras/ui/settings/settings_general.h"
#include "extras/ui/settings/settings_other.h"
#include "core/version.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_extras_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;

namespace {

void BuildLogo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto logo = object_ptr<Ui::RpWidget>(ctx.container);
		const auto logoRaw = logo.data();
		logoRaw->resize(
			QSize(st::settingsCloudPasswordIconSize,
				st::settingsCloudPasswordIconSize));
		logoRaw->setNaturalWidth(st::settingsCloudPasswordIconSize);
		logoRaw->paintRequest(
		) | rpl::on_next([=] {
			const auto image = ExtrasAssets::currentAppLogoPad();
			if (image.isNull()) {
				return;
			}
			auto p = QPainter(logoRaw);
			const auto size = st::settingsCloudPasswordIconSize;
			const auto scaled = image.scaled(
				size * style::DevicePixelRatio(),
				size * style::DevicePixelRatio(),
				Qt::KeepAspectRatio,
				Qt::SmoothTransformation);
			p.drawImage(QRect(0, 0, size, size), scaled);
		}, logoRaw->lifetime());
		return { .widget = std::move(logo), .align = style::al_top };
	});
}

void BuildVersionInfo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(
					QString("AstraGram v")
					+ QString::fromLatin1(AppVersionStr)),
				st::boxTitle),
			.align = style::al_top,
		};
	});

	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				tr::extras_SettingsDescription(),
				st::centeredBoxLabel),
			.align = style::al_top,
		};
	});
}

void BuildCategories(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSkip();
	builder.addSkip();
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(tr::extras_CategoriesHeader());

	const auto dev = ExtrasSettings::getInstance().devFeaturesEnabled();
	if (dev) {
		builder.addSectionButton({
			.id = u"extras/cat/ghost"_q,
			.title = rpl::single(QString("AstraGram")),
			.targetSection = ExtrasGhost::Id(),
			.icon = { &st::menuIconGroupReactions },
		});
		builder.addSectionButton({
			.id = u"extras/cat/filters"_q,
			.title = tr::extras_CategoryFilters(),
			.targetSection = ExtrasFilters::Id(),
			.icon = { &st::menuIconTagFilter },
		});
	}
	builder.addSectionButton({
		.id = u"extras/cat/general"_q,
		.title = tr::extras_CategoryGeneral(),
		.targetSection = ExtrasGeneral::Id(),
		.icon = { &st::menuIconShowAll },
	});
	builder.addSectionButton({
		.id = u"extras/cat/appearance"_q,
		.title = tr::extras_CategoryAppearance(),
		.targetSection = ExtrasAppearance::Id(),
		.icon = { &st::menuIconPalette },
	});
	builder.addSectionButton({
		.id = u"extras/cat/chats"_q,
		.title = tr::extras_CategoryChats(),
		.targetSection = ExtrasChats::Id(),
		.icon = { &st::menuIconChatBubble },
	});
	builder.addSectionButton({
		.id = u"extras/cat/other"_q,
		.title = tr::extras_CategoryOther(),
		.targetSection = ExtrasOther::Id(),
		.icon = { &st::menuIconFave },
	});
	// Debug 构建常驻，Release 只在用户开启调试日志后出现；两种情况都在打开
	// 设置时确定，切换调试日志需要重新进入本页才生效。
	if (DebugEntryVisible()) {
		builder.addSectionButton({
			.id = u"extras/cat/debug"_q,
			.title = rpl::single(u"Debug"_q),
			.targetSection = ExtrasDebug::Id(),
			.icon = { &st::menuIconStats },
		});
	}
}

const auto kMeta = BuildHelper({
	.id = ExtrasMain::Id(),
	.parentId = MainId(),
	.title = &tr::extras_Preferences,
	.icon = &st::menuIconPremium,
}, [](SectionBuilder &builder) {
	BuildLogo(builder);
	builder.addSkip();
	BuildVersionInfo(builder);
	BuildCategories(builder);
});

} // namespace

rpl::producer<QString> ExtrasMain::title() {
	return rpl::single(QString(""));
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
