/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_power_saving.h"

#include "base/battery_saving.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/power_saving.h"
#include "ui/vertical_list.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "settings/settings_builder.h"
#include "settings/sections/settings_main.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

constexpr auto kForceDisableTooltipDuration = 3 * crl::time(1000);

class PowerSavingSection final : public Section<PowerSavingSection> {
public:
	PowerSavingSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	rpl::producer<QString> title() override {
		return tr::lng_settings_power_title();
	}
};

const auto kPowerSavingPage = Builder::BuildHelper({
	.id = PowerSavingSection::Id(),
	.parentId = MainId(),
	.title = &tr::lng_settings_power_title,
	.icon = &st::menuIconPowerUsage,
}, [](Builder::SectionBuilder &builder) {
	builder.addDividerText(tr::lng_settings_power_subtitle());
	builder.add([](const Builder::WidgetContext &ctx) {
		const auto id = ctx.controller->highlightControlId();
		const auto prefix = u"power/flags/"_q;
		const auto flags = id.startsWith(prefix)
			? PowerSaving::Flags::from_raw(id.mid(prefix.size()).toUInt())
			: PowerSaving::Flags();
		auto disabled = rpl::combine(
			Core::App().batterySaving().value(),
			Core::App().settings().ignoreBatterySavingValue()
		) | rpl::map([](bool saving, bool ignore) {
			return (saving && !ignore)
				? tr::lng_settings_power_turn_off()
				: rpl::single(QString());
		}) | rpl::flatten_latest();
		auto controls = CreateEditPowerSaving(
			ctx.container,
			PowerSaving::kAll & ~PowerSaving::Current(),
			std::move(disabled),
			flags);
		std::move(controls.changes) | rpl::on_next([](PowerSaving::Flags enabled) {
			if (PowerSaving::ForceAll()) {
				return;
			}
			PowerSaving::Set(PowerSaving::kAll & ~enabled);
			Core::App().saveSettingsDelayed();
		}, controls.widget->lifetime());
		if (flags && controls.highlightWidget && ctx.highlights) {
			ctx.highlights->push_back({ id, { controls.highlightWidget.data(), {} } });
		}
		return Builder::SectionBuilder::WidgetToAdd{
			.widget = std::move(controls.widget),
		};
	});
	if (!Core::App().batterySaving().enabled().has_value()) {
		return;
	}
	builder.addDivider();
	const auto automatic = builder.addButton({
		.id = u"power/automatic"_q,
		.title = tr::lng_settings_power_auto(),
		.st = &st::settingsCardToggleButton,
		.icon = { &st::menuIconPowerUsage },
		.toggled = Core::App().settings().ignoreBatterySavingValue(
			) | rpl::map(rpl::mappers::_1 == false),
	});
	if (automatic) {
		automatic->toggledChanges() | rpl::on_next([](bool enabled) {
			Core::App().settings().setIgnoreBatterySavingValue(!enabled);
			Core::App().saveSettingsDelayed();
		}, automatic->lifetime());
	}
	builder.addDividerText(tr::lng_settings_power_auto_about());
});

PowerSavingSection::PowerSavingSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kPowerSavingPage.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace

Type PowerSavingId() {
	return PowerSavingSection::Id();
}

void ShowPowerSaving(
		not_null<Window::SessionController*> controller,
		PowerSaving::Flags highlightFlags) {
	if (highlightFlags) {
		controller->setHighlightControlId(
			u"power/flags/"_q + QString::number(highlightFlags.value()));
	}
	ShowSettingsLayer(controller, PowerSavingId());
}

void PowerSavingBox(
		not_null<Ui::GenericBox*> box,
		PowerSaving::Flags highlightFlags) {
	box->setStyle(st::layerBox);
	box->setTitle(tr::lng_settings_power_title());
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();
	const auto ignore = Core::App().settings().ignoreBatterySaving();
	const auto batterySaving = Core::App().batterySaving().enabled();

	// Force top shadow visibility.
	box->setPinnedToTopContent(
		object_ptr<Ui::FixedHeightWidget>(box, st::lineWidth));

	const auto subtitle = Ui::AddSubsectionTitle(
		container,
		tr::lng_settings_power_subtitle(),
		st::powerSavingSubtitlePadding);

	struct State {
		rpl::variable<QString> forceDisabledMessage;
	};
	const auto state = container->lifetime().make_state<State>();
	state->forceDisabledMessage = (batterySaving.value_or(false) && !ignore)
		? tr::lng_settings_power_turn_off(tr::now)
		: QString();

	auto [checkboxes, getResult, changes, highlightWidget] = CreateEditPowerSaving(
		box,
		PowerSaving::kAll & ~PowerSaving::Current(),
		state->forceDisabledMessage.value(),
		highlightFlags);

	const auto controlsRaw = checkboxes.data();
	const auto highlightWidgetRaw = highlightWidget.data();
	box->addRow(std::move(checkboxes), style::margins());

	auto automatic = (Ui::SettingsButton*)nullptr;
	if (batterySaving.has_value()) {
		Ui::AddSkip(container);
		Ui::AddDivider(container);
		Ui::AddSkip(container);
		automatic = container->add(object_ptr<Ui::SettingsButton>(
			container,
			tr::lng_settings_power_auto(),
			st::powerSavingButtonNoIcon
		))->toggleOn(rpl::single(!ignore));
		Ui::AddSkip(container);
		Ui::AddDividerText(container, tr::lng_settings_power_auto_about());

		state->forceDisabledMessage = rpl::combine(
			automatic->toggledValue(),
			Core::App().batterySaving().value()
		) | rpl::map([=](bool dontIgnore, bool saving) {
			return (saving && dontIgnore)
				? tr::lng_settings_power_turn_off()
				: rpl::single(QString());
		}) | rpl::flatten_latest();

		const auto show = box->uiShow();
		const auto disabler = Ui::CreateChild<Ui::AbstractButton>(
			container.get());
		disabler->setClickedCallback([=] {
			show->showToast(
				tr::lng_settings_power_turn_off(tr::now),
				kForceDisableTooltipDuration);
		});
		disabler->paintRequest() | rpl::on_next([=](QRect clip) {
			auto color = st::boxBg->c;
			color.setAlpha(96);
			QPainter(disabler).fillRect(clip, color);
		}, disabler->lifetime());
		rpl::combine(
			subtitle->geometryValue(),
			controlsRaw->geometryValue()
		) | rpl::on_next([=](QRect subtitle, QRect controls) {
			disabler->setGeometry(subtitle.united(controls));
		}, disabler->lifetime());
		disabler->showOn(state->forceDisabledMessage.value(
		) | rpl::map([=](const QString &value) {
			return !value.isEmpty();
		}));
	}

	box->addButton(tr::lng_settings_save(), [=, collect = getResult] {
		const auto ignore = automatic
			? !automatic->toggled()
			: Core::App().settings().ignoreBatterySaving();
		const auto batterySaving = Core::App().batterySaving().enabled();
		if (ignore || !batterySaving.value_or(false)) {
			Set(PowerSaving::kAll & ~collect());
		}
		Core::App().settings().setIgnoreBatterySavingValue(ignore);
		Core::App().saveSettingsDelayed();
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	if (highlightWidgetRaw) {
		box->showFinishes(
		) | rpl::take(1) | rpl::on_next([=] {
			HighlightWidget(highlightWidgetRaw);
		}, box->lifetime());
	}
}

EditFlagsDescriptor<PowerSaving::Flags> PowerSavingLabels() {
	using namespace PowerSaving;
	using Label = EditFlagsLabel<Flags>;

	auto stickers = std::vector<Label>{
		{
			kStickersPanel,
			tr::lng_settings_power_stickers_panel(tr::now),
			&st::menuIconStickers,
		},
		{ kStickersChat, tr::lng_settings_power_stickers_chat(tr::now) },
	};
	auto emoji = std::vector<Label>{
		{
			kEmojiPanel,
			tr::lng_settings_power_emoji_panel(tr::now),
			&st::menuIconEmoji,
		},
		{ kEmojiReactions, tr::lng_settings_power_emoji_reactions(tr::now) },
		{ kEmojiChat, tr::lng_settings_power_emoji_chat(tr::now) },
		{ kEmojiStatus, tr::lng_settings_power_emoji_status(tr::now) },
	};
	auto chat = std::vector<Label>{
		{
			kChatBackground,
			tr::lng_settings_power_chat_background(tr::now),
			&st::menuIconChatBubble,
		},
		{ kChatSpoiler, tr::lng_settings_power_chat_spoiler(tr::now) },
		{ kChatEffects, tr::lng_settings_power_chat_effects(tr::now) },
	};
	auto calls = std::vector<Label>{
		{
			kCalls,
			tr::lng_settings_power_calls(tr::now),
			&st::menuIconPhone,
		},
	};
	auto animations = std::vector<Label>{
		{
			kAnimations,
			tr::lng_settings_power_ui(tr::now),
			&st::menuIconStartStream,
		},
	};
	return { .labels = {
		{ tr::lng_settings_power_stickers(), std::move(stickers) },
		{ tr::lng_settings_power_emoji(), std::move(emoji) },
		{ tr::lng_settings_power_chat(), std::move(chat) },
		{ std::nullopt, std::move(calls) },
		{ std::nullopt, std::move(animations) },
	}, .st = &st::powerSavingButton };
}

} // namespace Settings
