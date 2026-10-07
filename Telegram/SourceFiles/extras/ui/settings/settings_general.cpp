#include "extras/ui/settings/settings_general.h"

#include "lang_auto.h"
#include "extras/extras_settings.h"
#include "extras/ui/settings/extras_builder.h"
#include "extras/ui/settings/settings_extras_utils.h"
#include "extras/ui/settings/settings_main.h"
#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "lang/lang_text_entity.h"
#include "platform/platform_translate_provider.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace ExtrasBuilder;

namespace {

void BuildTranslator(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	builder.addSubsectionTitle(tr::lng_translate_settings_subtitle());

	auto *settings = &ExtrasSettings::getInstance();

	const auto options = std::vector{
		std::pair(TranslationProvider::Telegram, QString("Telegram")),
		std::pair(TranslationProvider::Google, QString("Google")),
		std::pair(TranslationProvider::Yandex, QString("Yandex")),
	};
	const auto nativeAvailable = Platform::IsTranslateProviderAvailable();
	auto availableOptions = options;
	if (nativeAvailable) {
		availableOptions.push_back(std::pair(
			TranslationProvider::Native,
			[] {
				if constexpr (Platform::IsMac()) {
					return QString("macOS");
				} else if constexpr (Platform::IsWindows()) {
					return QString("Windows");
				} else {
					return QString("Linux");
				}
			}()));
	}
	auto optionLabels = std::vector<QString>();
	optionLabels.reserve(availableOptions.size());
	for (const auto &option : availableOptions) {
		optionLabels.push_back(option.second);
	}

	const auto getIndex = [=](TranslationProvider val) {
		const auto i = ranges::find(
			availableOptions,
			val,
			&std::pair<TranslationProvider, QString>::first);
		return (i != end(availableOptions))
			? int(i - begin(availableOptions))
			: 0;
	};

	auto currentVal = ExtrasSettings::getInstance().translationProviderValue()
		| rpl::map(getIndex)
		| rpl::map([=](int val) { return availableOptions[val].second; });

	const auto button = builder.addButton({
		.id = u"extras/translationProvider"_q,
		.title = tr::extras_TranslationProvider(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentVal),
		.onClick = [=] {
			const auto controller = Core::App().activeWindow()->sessionController();
			if (!controller) {
				return;
			}
			controller->show(Box(
					[=](not_null<Ui::GenericBox*> box) {
						const auto save = [=](int index) {
							const auto option = availableOptions[index].first;
							ExtrasSettings::getInstance().setTranslationProvider(option);

							if constexpr (Platform::IsMac()) {
								if (option == TranslationProvider::Native) {
									controller->showToast(Ui::Toast::Config{
										.text = tr::lng_translate_settings_use_platform_mac_about(tr::now, tr::rich),
										.duration = 6 * crl::time(1000)
									});
								}
							}
						};
						SingleChoiceBox(box, {
							.title = tr::extras_TranslationProvider(),
							.options = optionLabels,
							.initialSelection = getIndex(settings->translationProvider()),
							.callback = save,
						});
					}));
		},
	});
	if (button) {
		extras.addBetaBadge(button, settings->translationProviderValue()
			| rpl::map([](TranslationProvider provider) {
				return provider == TranslationProvider::Google
					|| provider == TranslationProvider::Yandex;
			}));
	}
}

void BuildShowPeerId(SectionBuilder &builder) {
	auto *settings = &ExtrasSettings::getInstance();

	const auto options = std::vector{
		QString(tr::extras_SettingsShowID_Hide(tr::now)),
		QString("Telegram API"),
		QString("Bot API")
	};

	auto currentVal = ExtrasSettings::getInstance().showPeerIdValue()
		| rpl::map([=](PeerIdDisplay val) {
			return options[static_cast<int>(val)];
		});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"extras/showPeerId"_q,
		.altIds = { u"extras/showIdAndDc"_q },
		.title = tr::extras_SettingsShowID(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentVal),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						ExtrasSettings::getInstance().setShowPeerId(
							static_cast<PeerIdDisplay>(index));
					};
					SingleChoiceBox(box, {
						.title = tr::extras_SettingsShowID(),
						.options = options,
						.initialSelection = static_cast<int>(settings->showPeerId()),
						.callback = save,
					});
				}));
		},
	});
}

void BuildQoLToggles(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	auto *settings = &ExtrasSettings::getInstance();

	BuildTranslator(builder, extras);
	extras.addSectionDivider();

	builder.addSubsectionTitle(tr::extras_CategoryGeneral());

	const auto controller = builder.controller();
	extras.addToggle({
		.id = u"extras/disableStories"_q,
		.altIds = { u"extras/hideStories"_q },
		.title = tr::extras_DisableStories(),
		.getter = [=] { return settings->disableStories(); },
		.setter = [=](bool enabled) {
			ExtrasSettings::getInstance().setDisableStories(enabled);
			ShowRestartPrompt(controller);
		},
	});

	extras.addSettingToggle({
		.id = u"extras/disableOpenLinkWarning"_q,
		.title = tr::extras_DisableOpenLinkWarning(),
		.getter = &ExtrasSettings::disableOpenLinkWarning,
		.setter = &ExtrasSettings::setDisableOpenLinkWarning,
	});

	extras.addCollapsibleToggle({
		.id = u"extras/similarChannels"_q,
		.title = tr::extras_DisableSimilarChannels(),
		.checkboxes = {
			NestedEntry{
				tr::extras_CollapseSimilarChannels(tr::now),
				[] { return ExtrasSettings::getInstance().collapseSimilarChannels(); },
				[](bool v) { ExtrasSettings::getInstance().setCollapseSimilarChannels(v); }
			},
			NestedEntry{
				tr::extras_HideSimilarChannelsTab(tr::now),
				[] { return ExtrasSettings::getInstance().hideSimilarChannels(); },
				[](bool v) { ExtrasSettings::getInstance().setHideSimilarChannels(v); }
			}
		},
		.toggledWhenAll = true,
	});

	extras.addSettingToggle({
		.id = u"extras/disableNotificationsDelay"_q,
		.title = tr::extras_DisableNotificationsDelay(),
		.getter = &ExtrasSettings::disableNotificationsDelay,
		.setter = &ExtrasSettings::setDisableNotificationsDelay,
	});

	extras.addSectionDivider();

	extras.addSettingToggle({
		.id = u"extras/improveLinkPreviews"_q,
		.title = tr::extras_ImproveLinkPreviews(),
		.getter = &ExtrasSettings::improveLinkPreviews,
		.setter = &ExtrasSettings::setImproveLinkPreviews,
	});
	extras.addCollapsibleToggle({
		.id = u"extras/confirmations"_q,
		.title = tr::extras_ConfirmationsTitle(),
		.checkboxes = {
			NestedEntry{
				tr::extras_StickerConfirmation(tr::now),
				[] { return ExtrasSettings::getInstance().stickerConfirmation(); },
				[](bool v) { ExtrasSettings::getInstance().setStickerConfirmation(v); }
			},
			NestedEntry{
				tr::extras_GIFConfirmation(tr::now),
				[] { return ExtrasSettings::getInstance().gifConfirmation(); },
				[](bool v) { ExtrasSettings::getInstance().setGifConfirmation(v); }
			},
			NestedEntry{
				tr::extras_VoiceConfirmation(tr::now),
				[] { return ExtrasSettings::getInstance().voiceConfirmation(); },
				[](bool v) { ExtrasSettings::getInstance().setVoiceConfirmation(v); }
			},
			NestedEntry{
				tr::extras_RoundConfirmation(tr::now),
				[] { return ExtrasSettings::getInstance().roundConfirmation(); },
				[](bool v) { ExtrasSettings::getInstance().setRoundConfirmation(v); }
			}
		},
		.toggledWhenAll = false,
	});
	extras.addSettingToggle({
		.id = u"extras/showMessageSeconds"_q,
		.altIds = { u"extras/formatTimeWithSeconds"_q },
		.title = tr::extras_SettingsShowMessageSeconds(),
		.getter = &ExtrasSettings::showMessageSeconds,
		.setter = &ExtrasSettings::setShowMessageSeconds,
	});
	extras.addSettingToggle({
		.id = u"extras/showMessageId"_q,
		.title = tr::extras_SettingsShowMessageId(),
		.getter = &ExtrasSettings::showMessageId,
		.setter = &ExtrasSettings::setShowMessageId,
	});

	BuildShowPeerId(builder);

	extras.addSectionDivider();

	builder.addSubsectionTitle(rpl::single(QString("Webview")));

	extras.addSettingToggle({
		.id = u"extras/spoofWebviewAsAndroid"_q,
		.title = tr::extras_SettingsSpoofWebviewAsAndroid(),
		.getter = &ExtrasSettings::spoofWebviewAsAndroid,
		.setter = &ExtrasSettings::setSpoofWebviewAsAndroid,
	});

	extras.addCollapsibleToggle({
		.id = u"extras/biggerWindow"_q,
		.title = tr::extras_SettingsBiggerWindow(),
		.checkboxes = {
			NestedEntry{
				tr::extras_SettingsIncreaseWebviewHeight(tr::now),
				[] { return ExtrasSettings::getInstance().increaseWebviewHeight(); },
				[](bool v) { ExtrasSettings::getInstance().setIncreaseWebviewHeight(v); }
			},
			NestedEntry{
				tr::extras_SettingsIncreaseWebviewWidth(tr::now),
				[] { return ExtrasSettings::getInstance().increaseWebviewWidth(); },
				[](bool v) { ExtrasSettings::getInstance().setIncreaseWebviewWidth(v); }
			}
		},
		.toggledWhenAll = false,
	});
}

const auto kMeta = BuildHelper({
	.id = ExtrasGeneral::Id(),
	.parentId = ExtrasMain::Id(),
	.title = &tr::extras_CategoryGeneral,
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	auto extras = ExtrasSectionBuilder(builder);

	builder.addSkip();
	BuildQoLToggles(builder, extras);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> ExtrasGeneral::title() {
	return tr::extras_CategoryGeneral();
}

ExtrasGeneral::ExtrasGeneral(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void ExtrasGeneral::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type ExtrasGeneralId() {
	return ExtrasGeneral::Id();
}

} // namespace Settings
