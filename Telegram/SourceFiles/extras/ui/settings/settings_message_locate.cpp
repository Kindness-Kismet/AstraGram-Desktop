#include "extras/ui/settings/settings_message_locate.h"

#include "extras/extras_settings.h"
#include "extras/ui/components/message_locate_preview.h"
#include "extras/ui/settings/extras_builder.h"
#include "lang_auto.h"
#include "settings/settings_builder.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/color_editor.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {
namespace {

QString styleLabel(MessageLocateStyle style) {
	return (style == MessageLocateStyle::Contour)
		? tr::extras_MessageLocateContour(tr::now)
		: tr::extras_MessageLocatePin(tr::now);
}

void chooseColor(not_null<Window::SessionController*> controller) {
	controller->show(Box([](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::extras_MessageLocateColor());
		box->setObjectName(u"messageLocate/colorBox"_q);
		const auto current = ExtrasSettings::getInstance().messageLocateColor();
		const auto editor = box->addRow(object_ptr<ColorEditor>(
			box, ColorEditor::Mode::HSL,
			current.isEmpty() ? st::windowFgActive->c : QColor(current)));
		const auto save = [=] {
			ExtrasSettings::getInstance().setMessageLocateColor(editor->color().name());
			box->closeBox();
		};
		editor->submitRequests() | rpl::on_next(save, editor->lifetime());
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::extras_MessageLocateThemeColor(), [=] {
			ExtrasSettings::getInstance().setMessageLocateColor({});
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void setupBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(tr::extras_MessageLocate());
	box->setObjectName(u"messageLocate/settings"_q);
	box->setWidth(st::boxWideWidth);
	const auto preview = box->addRow(
		object_ptr<MessageLocatePreview>(box, controller), style::margins());
	auto builder = Builder::SectionBuilder(Builder::WidgetContext{
		.container = box->verticalLayout(),
		.controller = controller,
		.cardLayout = false,
	});
	auto extras = ExtrasBuilder::ExtrasSectionBuilder(builder);
	auto &settings = ExtrasSettings::getInstance();
	builder.addButton({
		.id = u"messageLocate/style"_q,
		.title = tr::extras_MessageLocateStyle(),
		.st = &st::settingsButtonNoIcon,
		.label = settings.messageLocateStyleValue() | rpl::map(styleLabel),
		.onClick = [=] {
			controller->show(Box([](not_null<Ui::GenericBox*> choice) {
				choice->setObjectName(u"messageLocate/styleBox"_q);
				SingleChoiceBox(choice, {
					.title = tr::extras_MessageLocateStyle(),
					.options = {
						tr::extras_MessageLocateContour(tr::now),
						tr::extras_MessageLocatePin(tr::now),
					},
					.initialSelection = int(ExtrasSettings::getInstance().messageLocateStyle()),
					.callback = [](int index) {
						ExtrasSettings::getInstance().setMessageLocateStyle(MessageLocateStyle(index));
					},
				});
			}));
		},
	});
	builder.addButton({
		.id = u"messageLocate/color"_q,
		.title = tr::extras_MessageLocateColor(),
		.st = &st::settingsButtonNoIcon,
		.label = settings.messageLocateColorValue() | rpl::map([](QString color) {
			return color.isEmpty() ? tr::extras_MessageLocateThemeColor(tr::now) : color.toUpper();
		}),
		.onClick = [=] { chooseColor(controller); },
	});
	const auto addSlider = [&](QString id, rpl::producer<QString> title,
			int minimum, int maximum, int step, int current,
			void (ExtrasSettings::*setter)(int), bool duration = false) {
		extras.addSlider({
			.id = std::move(id),
			.title = std::move(title),
			.steps = (maximum - minimum) / step + 1,
			.current = (current - minimum) / step,
			.indexToValue = [](int index) { return index; },
			.onChanged = [=](int index) {
				(ExtrasSettings::getInstance().*setter)(minimum + index * step);
			},
			.formatLabel = [=](int index) {
				const auto value = minimum + index * step;
				return duration
					? tr::extras_MessageLocateSeconds(tr::now, lt_value, QString::number(value / 1000., 'f', 1))
					: tr::extras_MessageLocatePixels(tr::now, lt_value, QString::number(value));
			},
		});
	};
	addSlider(u"messageLocate/duration"_q, tr::extras_MessageLocateDuration(),
		800, 4000, 100, settings.messageLocateDuration(), &ExtrasSettings::setMessageLocateDuration, true);
	builder.scope([&] {
		addSlider(u"messageLocate/stroke"_q, tr::extras_MessageLocateStroke(),
			1, 4, 1, settings.messageLocateStroke(), &ExtrasSettings::setMessageLocateStroke);
		addSlider(u"messageLocate/corner"_q, tr::extras_MessageLocateCorner(),
			12, 36, 1, settings.messageLocateCorner(), &ExtrasSettings::setMessageLocateCorner);
	}, settings.messageLocateStyleValue() | rpl::map([](MessageLocateStyle style) {
		return style == MessageLocateStyle::Contour;
	}));
	builder.scope([&] {
		addSlider(u"messageLocate/pinSize"_q, tr::extras_MessageLocatePinSize(),
			12, 32, 1, settings.messageLocatePinSize(), &ExtrasSettings::setMessageLocatePinSize);
		addSlider(u"messageLocate/bounce"_q, tr::extras_MessageLocateBounce(),
			0, 12, 1, settings.messageLocateBounce(), &ExtrasSettings::setMessageLocateBounce);
		builder.addDividerText(tr::extras_MessageLocatePinDescription());
	}, settings.messageLocateStyleValue() | rpl::map([](MessageLocateStyle style) {
		return style == MessageLocateStyle::Pin;
	}));
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	box->addLeftButton(tr::extras_MessageLocateReplay(), [=] { preview->replay(); });
}

} // namespace

void buildMessageLocate(Builder::SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"chat/message-locate"_q,
		.altIds = { u"extras/messageLocateStyle"_q },
		.title = tr::extras_MessageLocate(),
		.icon = { &st::menuIconSearch },
		.label = ExtrasSettings::getInstance().messageLocateStyleValue() | rpl::map(styleLabel),
		.onClick = [=] { controller->show(Box(setupBox, controller)); },
		.keywords = tr::extras_MessageLocateKeywords(tr::now).split(' '),
	});
}

} // namespace Settings
