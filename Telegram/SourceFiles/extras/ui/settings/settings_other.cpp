#include "extras/ui/settings/settings_other.h"

#include "lang_auto.h"
#include "extras/extras_settings.h"
#include "extras/ui/settings/extras_builder.h"
#include "extras/ui/settings/settings_extras_utils.h"
#include "extras/ui/settings/settings_main.h"
#include "core/application.h"
#include "lang/lang_text_entity.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"


namespace Settings {

using namespace Builder;
using namespace ExtrasBuilder;

namespace {


void BuildOtherThings(SectionBuilder &builder) {
	const auto controller = builder.controller();

	if (ExtrasSettings::getInstance().devFeaturesEnabled()) {
		builder.addButton({
			.id = u"extras/restoreHidden"_q,
			.title = tr::extras_RestoreHiddenFeatures(),
			.icon = { &st::menuIconRestore },
			.onClick = [=] {
				ExtrasSettings::getInstance().setDevFeaturesEnabled(false);
				controller->showToast(tr::extras_DevFeaturesRestored(tr::now));
			},
		});
	}

	builder.addSkip();
	builder.addButton({
		.id = u"extras/registerUrlScheme"_q,
		.title = tr::extras_RegisterURLScheme(),
		.icon = { &st::menuIconLink },
		.onClick = [=] {
			Core::Application::RegisterUrlScheme();
			controller->showToast(tr::lng_box_done(tr::now));
		},
	});
	builder.addButton({
		.id = u"extras/resetSettings"_q,
		.title = tr::extras_ResetSettings(),
		.icon = { &st::menuIconRestore },
		.onClick = [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::extras_ResetSettingsConfirmation(tr::rich),
				.confirmed = [=](Fn<void()> &&close) {
					ExtrasSettings::reset();
					controller->showToast(tr::lng_box_done(tr::now));
					close();
				},
				.confirmText = tr::lng_box_yes(),
			}));
		},
	});
	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = ExtrasOther::Id(),
	.parentId = ExtrasMain::Id(),
	.title = &tr::extras_CategoryOther,
	.icon = &st::menuIconFave,
}, [](SectionBuilder &builder) {
	auto extras = ExtrasSectionBuilder(builder);

	builder.addSkip();
	BuildOtherThings(builder);
});

} // namespace

rpl::producer<QString> ExtrasOther::title() {
	return tr::extras_CategoryOther();
}

ExtrasOther::ExtrasOther(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void ExtrasOther::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type ExtrasOtherId() {
	return ExtrasOther::Id();
}

} // namespace Settings
