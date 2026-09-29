#include "extras/ui/settings/settings_window_material.h"

#include "extras/extras_settings.h"
#include "extras/features/window_material/window_material.h"
#include "lang_auto.h"
#include "settings/settings_builder.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

namespace Settings {
namespace {

WindowMaterial supportedWindowMaterial(WindowMaterial material) {
	for (const auto mode : ExtrasFeatures::WindowMaterial::availableModes()) {
		if (mode == material) {
			return material;
		}
	}
	return WindowMaterial::Off;
}

QString windowMaterialLabel(WindowMaterial material) {
	switch (material) {
	case WindowMaterial::Off: return tr::extras_WindowMaterialOff(tr::now);
	case WindowMaterial::Mica: return tr::extras_WindowMaterialMica(tr::now);
	case WindowMaterial::Acrylic: return tr::extras_WindowMaterialAcrylic(tr::now);
	case WindowMaterial::Blur: return tr::extras_WindowMaterialBlur(tr::now);
	}
	Unexpected("Invalid window material");
}

} // namespace

void buildWindowMaterial(Builder::SectionBuilder &builder) {
	const auto modes = ExtrasFeatures::WindowMaterial::availableModes();
	if (modes.size() <= 1) {
		return;
	}
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"chat/window-material"_q,
		.altIds = { u"extras/windowMaterial"_q },
		.title = tr::extras_WindowMaterial(),
		.icon = { &st::menuIconNewWindow },
		.label = ExtrasSettings::getInstance().windowMaterialValue()
			| rpl::map(supportedWindowMaterial)
			| rpl::map(windowMaterialLabel),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				auto labels = std::vector<QString>();
				auto selected = 0;
				const auto current = supportedWindowMaterial(
					ExtrasSettings::getInstance().windowMaterial());
				for (const auto mode : modes) {
					if (mode == current) {
						selected = int(labels.size());
					}
					labels.push_back(windowMaterialLabel(mode));
				}
				SingleChoiceBox(box, {
					.title = tr::extras_WindowMaterial(),
					.options = labels,
					.initialSelection = selected,
					.callback = [=](int index) {
						ExtrasSettings::getInstance().setWindowMaterial(modes[index]);
					},
				});
				box->addRow(object_ptr<Ui::FlatLabel>(
					box,
					tr::extras_WindowMaterialDescription(),
					st::boxLabel));
			}));
		},
		.keywords = {
			u"window"_q, u"material"_q, u"mica"_q, u"acrylic"_q, u"blur"_q,
			u"窗口"_q, u"材质"_q, u"云母"_q, u"亚克力"_q, u"磨砂"_q,
		},
	});
}

} // namespace Settings
