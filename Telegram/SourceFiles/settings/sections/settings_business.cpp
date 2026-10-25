/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_business.h"
#include "settings/sections/settings_about.h"

#include "api/api_chat_links.h"
#include "api/api_premium.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "boxes/premium_preview_box.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/business/data_business_chatbots.h"
#include "data/business/data_business_info.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/business/settings_away_message.h"
#include "settings/business/settings_chat_intro.h"
#include "settings/business/settings_chat_links.h"
#include "settings/business/settings_chatbots.h"
#include "settings/business/settings_greeting.h"
#include "settings/business/settings_location.h"
#include "settings/business/settings_quick_replies.h"
#include "settings/business/settings_working_hours.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "settings/settings_card_layout.h"
#include "settings/sections/settings_premium.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Builder;

struct Entry {
	const style::icon *icon;
	rpl::producer<QString> title;
	rpl::producer<QString> description;
	PremiumFeature feature = PremiumFeature::BusinessLocation;
};

struct BusinessState {
	QPointer<Ui::SettingsButton> sponsoredButton;
	base::flat_map<PremiumFeature, QPointer<Ui::SettingsButton>> featureButtons;
};

using Order = std::vector<QString>;

[[nodiscard]] Order FallbackOrder() {
	return Order{
		u"greeting_message"_q,
		u"away_message"_q,
		u"quick_replies"_q,
		u"business_hours"_q,
		u"business_location"_q,
		u"business_links"_q,
		u"business_intro"_q,
		u"business_bots"_q,
		u"folder_tags"_q,
	};
}

[[nodiscard]] base::flat_map<QString, Entry> EntryMap() {
	return base::flat_map<QString, Entry>{
		{
			u"business_location"_q,
			Entry{
				&st::settingsBusinessIconLocation,
				tr::lng_business_subtitle_location(),
				tr::lng_business_about_location(),
				PremiumFeature::BusinessLocation,
			},
		},
		{
			u"business_hours"_q,
			Entry{
				&st::settingsBusinessIconHours,
				tr::lng_business_subtitle_opening_hours(),
				tr::lng_business_about_opening_hours(),
				PremiumFeature::BusinessHours,
			},
		},
		{
			u"quick_replies"_q,
			Entry{
				&st::settingsBusinessIconReplies,
				tr::lng_business_subtitle_quick_replies(),
				tr::lng_business_about_quick_replies(),
				PremiumFeature::QuickReplies,
			},
		},
		{
			u"greeting_message"_q,
			Entry{
				&st::settingsBusinessIconGreeting,
				tr::lng_business_subtitle_greeting_messages(),
				tr::lng_business_about_greeting_messages(),
				PremiumFeature::GreetingMessage,
			},
		},
		{
			u"away_message"_q,
			Entry{
				&st::settingsBusinessIconAway,
				tr::lng_business_subtitle_away_messages(),
				tr::lng_business_about_away_messages(),
				PremiumFeature::AwayMessage,
			},
		},
		{
			u"business_bots"_q,
			Entry{
				&st::settingsBusinessIconChatbots,
				tr::lng_business_subtitle_chatbots(),
				tr::lng_business_about_chatbots(),
				PremiumFeature::BusinessBots,
			},
		},
		{
			u"business_intro"_q,
			Entry{
				&st::settingsBusinessIconChatIntro,
				tr::lng_business_subtitle_chat_intro(),
				tr::lng_business_about_chat_intro(),
				PremiumFeature::ChatIntro,
			},
		},
		{
			u"business_links"_q,
			Entry{
				&st::settingsBusinessIconChatLinks,
				tr::lng_business_subtitle_chat_links(),
				tr::lng_business_about_chat_links(),
				PremiumFeature::ChatLinks,
			},
		},
		{
			u"folder_tags"_q,
			Entry{
				&st::settingsPremiumIconTags,
				tr::lng_premium_summary_subtitle_filter_tags(),
				tr::lng_premium_summary_about_filter_tags(),
				PremiumFeature::FilterTags,
			},
		},
	};
}

void AddBusinessSummary(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller,
		std::shared_ptr<BusinessState> state,
		Fn<void(PremiumFeature)> buttonCallback) {
	auto entries = EntryMap();
	const auto order = controller->session().appConfig().get<Order>(
		"business_promo_order", FallbackOrder());
	for (const auto &key : order) {
		const auto i = entries.find(key);
		if (i == entries.end()) {
			continue;
		}
		auto &entry = i->second;
		const auto button = AddButtonWithIcon(content,
			std::move(entry.title), st::settingsButton, { .icon = entry.icon });
		AddSectionRowDetails(button, std::move(entry.description));
		button->setClickedCallback([=, feature = entry.feature] {
			buttonCallback(feature);
		});
		if (state) {
			state->featureButtons[entry.feature] = button;
		}
	}
}

[[nodiscard]] QString FeatureSearchId(PremiumFeature feature) {
	switch (feature) {
	case PremiumFeature::GreetingMessage: return u"business/greeting"_q;
	case PremiumFeature::AwayMessage: return u"business/away"_q;
	case PremiumFeature::QuickReplies: return u"business/replies"_q;
	case PremiumFeature::BusinessHours: return u"business/hours"_q;
	case PremiumFeature::BusinessLocation: return u"business/location"_q;
	case PremiumFeature::ChatLinks: return u"business/links"_q;
	case PremiumFeature::ChatIntro: return u"business/intro"_q;
	case PremiumFeature::BusinessBots: return u"business/bots"_q;
	case PremiumFeature::FilterTags: return u"business/tags"_q;
	default: return QString();
	}
}

[[nodiscard]] QString FeatureSearchTitle(PremiumFeature feature) {
	switch (feature) {
	case PremiumFeature::GreetingMessage:
		return tr::lng_business_subtitle_greeting_messages(tr::now);
	case PremiumFeature::AwayMessage:
		return tr::lng_business_subtitle_away_messages(tr::now);
	case PremiumFeature::QuickReplies:
		return tr::lng_business_subtitle_quick_replies(tr::now);
	case PremiumFeature::BusinessHours:
		return tr::lng_business_subtitle_opening_hours(tr::now);
	case PremiumFeature::BusinessLocation:
		return tr::lng_business_subtitle_location(tr::now);
	case PremiumFeature::ChatLinks:
		return tr::lng_business_subtitle_chat_links(tr::now);
	case PremiumFeature::ChatIntro:
		return tr::lng_business_subtitle_chat_intro(tr::now);
	case PremiumFeature::BusinessBots:
		return tr::lng_business_subtitle_chatbots(tr::now);
	case PremiumFeature::FilterTags:
		return tr::lng_premium_summary_subtitle_filter_tags(tr::now);
	default: return QString();
	}
}

void BuildBusinessFeatures(SectionBuilder &builder) {
	const auto session = builder.session();
	const auto mtpOrder = session->appConfig().get<Order>(
		"business_promo_order",
		FallbackOrder());

	const auto entryMap = EntryMap();
	for (const auto &key : mtpOrder) {
		const auto it = entryMap.find(key);
		if (it == end(entryMap)) {
			continue;
		}
		const auto feature = it->second.feature;
		const auto id = FeatureSearchId(feature);
		if (id.isEmpty()) {
			continue;
		}
		builder.add(nullptr, [feature, id] {
			return SearchEntry{
				.id = id,
				.title = FeatureSearchTitle(feature),
				.keywords = { u"business"_q },
			};
		});
	}
}

void BuildSponsoredSection(
		SectionBuilder &builder,
		std::shared_ptr<BusinessState> state) {
	const auto controller = builder.controller();
	const auto session = builder.session();

	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"business/sponsored"_q,
			.title = tr::lng_business_button_sponsored(tr::now),
			.keywords = { u"ads"_q, u"advertising"_q },
		};
	});

	if (!controller) {
		return;
	}

	builder.addPageContent([controller, session, state](const WidgetContext &ctx) {
		const auto content = ctx.container;
		const auto sponsoredWrap = content->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				content,
				object_ptr<Ui::VerticalLayout>(content)));

		const auto fillSponsoredWrap = [=] {
			while (sponsoredWrap->entity()->count()) {
				delete sponsoredWrap->entity()->widgetAt(0);
			}
			const auto loading = sponsoredWrap->entity()->add(
				object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
					sponsoredWrap->entity(),
					object_ptr<Ui::FlatLabel>(
						sponsoredWrap->entity(),
						tr::lng_contacts_loading())),
				st::boxRowPadding);
			loading->entity()->setTextColorOverride(st::windowSubTextFg->c);

			const auto wrap = sponsoredWrap->entity()->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					sponsoredWrap->entity(),
					object_ptr<Ui::VerticalLayout>(sponsoredWrap->entity())));
			wrap->toggle(false, anim::type::instant);
			const auto inner = wrap->entity();
			AddCardTitle(
				inner,
				tr::lng_business_subtitle_sponsored());
			const auto button = AddButtonWithIcon(
				AddCardGroup(inner),
				tr::lng_business_button_sponsored(),
				st::settingsButtonNoIcon,
				{});
			if (state) {
				state->sponsoredButton = button;
			}

			{
				inner->add(object_ptr<Ui::FlatLabel>(
						inner,
						tr::lng_business_about_sponsored(
							lt_link,
							rpl::combine(
								tr::lng_business_about_sponsored_link(
									lt_emoji,
									rpl::single(Ui::Text::IconEmoji(
										&st::textMoreIconEmoji)),
									tr::rich),
								tr::lng_business_about_sponsored_url()
							) | rpl::map([](TextWithEntities text, QString url) {
								return tr::link(text, url);
							}),
							tr::rich),
						st::settingsCardHint),
					st::settingsCardHintPadding);
			}

			const auto api = inner->lifetime().make_state<Api::SponsoredToggle>(
				session);

			api->toggled(
			) | rpl::on_next([=](bool enabled) {
				button->toggleOn(rpl::single(enabled));
				wrap->toggle(true, anim::type::instant);
				loading->toggle(false, anim::type::instant);

				button->toggledChanges(
				) | rpl::on_next([=](bool toggled) {
					api->setToggled(
						toggled
					) | rpl::on_error_done([=](const QString &error) {
						controller->showToast(error);
					}, [] {
					}, button->lifetime());
				}, button->lifetime());
			}, inner->lifetime());

			Ui::ToggleChildrenVisibility(sponsoredWrap->entity(), true);
			sponsoredWrap->entity()->resizeToWidth(content->width());
		};

		Data::AmPremiumValue(session) | rpl::on_next([=](bool isPremium) {
			sponsoredWrap->toggle(isPremium, anim::type::normal);
			if (isPremium) {
				fillSponsoredWrap();
			}
		}, content->lifetime());

	});
}

void BuildBusinessSectionContent(
		SectionBuilder &builder,
		std::shared_ptr<BusinessState> state) {
	const auto controller = builder.controller();
	const auto session = builder.session();

	if (controller) {
		const auto owner = &session->data();
		owner->chatbots().preload();
		owner->businessInfo().preload();
		owner->shortcutMessages().preloadShortcuts();
		owner->session().api().chatLinks().preload();
	}

	if (controller) {
		builder.addPageContent([=](const WidgetContext &ctx) {
			const auto group = AddCardGroup(ctx.container);
			group->add(object_ptr<Ui::FlatLabel>(group,
				tr::lng_business_title(), st::settingsServiceTitle),
				st::settingsServiceTitlePadding);
			group->add(object_ptr<Ui::FlatLabel>(group,
				rpl::conditional(Data::AmPremiumValue(session),
					tr::lng_business_unlocked(tr::marked),
					tr::lng_business_about(tr::marked)), st::settingsCardHint),
				st::settingsServiceAboutPadding);
		});
	}

	BuildBusinessFeatures(builder);

	if (controller) {
		builder.add([controller, session, state](const WidgetContext &ctx) {
			const auto content = ctx.container;
			const auto owner = &session->data();
			const auto showOther = ctx.showOther;

			auto waitingToShow = content->lifetime().make_state<PremiumFeature>(
				PremiumFeature::Business);

			const auto showFeature = [=](PremiumFeature feature) {
				if (feature == PremiumFeature::FilterTags) {
					ShowPremiumPreviewToBuy(controller, feature);
					return;
				}
				showOther([&] {
					switch (feature) {
					case PremiumFeature::AwayMessage: return AwayMessageId();
					case PremiumFeature::BusinessHours: return WorkingHoursId();
					case PremiumFeature::BusinessLocation: return LocationId();
					case PremiumFeature::GreetingMessage: return GreetingId();
					case PremiumFeature::QuickReplies: return QuickRepliesId();
					case PremiumFeature::BusinessBots: return ChatbotsId();
					case PremiumFeature::ChatIntro: return ChatIntroId();
					case PremiumFeature::ChatLinks: return ChatLinksId();
					}
					Unexpected("Feature in showFeature.");
				}());
			};

			const auto isReady = [=](PremiumFeature feature) {
				switch (feature) {
				case PremiumFeature::AwayMessage:
					return owner->businessInfo().awaySettingsLoaded()
						&& owner->shortcutMessages().shortcutsLoaded();
				case PremiumFeature::BusinessHours:
					return owner->session().user()->isFullLoaded()
						&& owner->businessInfo().timezonesLoaded();
				case PremiumFeature::BusinessLocation:
					return owner->session().user()->isFullLoaded();
				case PremiumFeature::GreetingMessage:
					return owner->businessInfo().greetingSettingsLoaded()
						&& owner->shortcutMessages().shortcutsLoaded();
				case PremiumFeature::QuickReplies:
					return owner->shortcutMessages().shortcutsLoaded();
				case PremiumFeature::BusinessBots:
					return owner->chatbots().loaded();
				case PremiumFeature::ChatIntro:
					return owner->session().user()->isFullLoaded();
				case PremiumFeature::ChatLinks:
					return owner->session().api().chatLinks().loaded();
				case PremiumFeature::FilterTags:
					return true;
				}
				Unexpected("Feature in isReady.");
			};

			const auto check = [=] {
				if (*waitingToShow != PremiumFeature::Business
					&& isReady(*waitingToShow)) {
					showFeature(
						std::exchange(*waitingToShow, PremiumFeature::Business));
				}
			};

			rpl::merge(
				owner->businessInfo().awaySettingsChanged(),
				owner->businessInfo().greetingSettingsChanged(),
				owner->businessInfo().timezonesValue() | rpl::to_empty,
				owner->shortcutMessages().shortcutsChanged(),
				owner->chatbots().changes() | rpl::to_empty,
				owner->session().changes().peerUpdates(
					owner->session().user(),
					Data::PeerUpdate::Flag::FullInfo) | rpl::to_empty,
				owner->session().api().chatLinks().loadedUpdates()
			) | rpl::on_next(check, content->lifetime());

			AddBusinessSummary(content, controller, state, [=](PremiumFeature feature) {
				const auto alwaysAvailable
					= (feature == PremiumFeature::BusinessBots);
				if (!alwaysAvailable && !session->premium()) {
					ShowPremiumPreviewToBuy(controller, feature, nullptr);
					return;
				}
				if (!isReady(feature)) {
					*waitingToShow = feature;
					return;
				}
				showFeature(feature);
			});

			return SectionBuilder::WidgetToAdd{};
		});
	}

	BuildSponsoredSection(builder, state);
}

class Business : public Section<Business> {
public:
	Business(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] base::weak_qptr<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;

	void showFinished() override;

private:
	void setupContent();

	std::shared_ptr<BusinessState> _state;
	std::shared_ptr<Ui::RadiobuttonGroup> _radioGroup;


};

Business::Business(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _state(std::make_shared<BusinessState>())
, _radioGroup(std::make_shared<Ui::RadiobuttonGroup>()) {
	setupContent();
	controller->session().api().premium().reload();
}

rpl::producer<QString> Business::title() {
	return tr::lng_business_title();
}

void Business::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto state = _state;

	const SectionBuildMethod buildMethod = [state](
			not_null<Ui::VerticalLayout*> container,
			not_null<Window::SessionController*> controller,
			Fn<void(Type)> showOther,
			rpl::producer<> showFinished) {
		const auto isPaused = Window::PausedIn(
			controller,
			Window::GifPauseReason::Layer);
		auto builder = SectionBuilder(WidgetContext{
			.container = container,
			.controller = controller,
			.showOther = std::move(showOther),
			.isPaused = isPaused,
		});

		BuildBusinessSectionContent(builder, state);
	};

	build(content, buildMethod);

	Ui::ResizeFitChild(this, content);
}

void Business::showFinished() {
	crl::on_main(this, [=] {
		for (const auto &[feature, button] : _state->featureButtons) {
			const auto id = FeatureSearchId(feature);
			if (!id.isEmpty()) {
				controller()->checkHighlightControl(id, button);
			}
		}
		controller()->checkHighlightControl(
			u"business/sponsored"_q,
			_state->sponsoredButton);
	});
}

base::weak_qptr<Ui::RpWidget> Business::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	const auto content = Ui::CreateChild<Ui::RpWidget>(parent.get());
	const auto radioGroup = _radioGroup;
	const auto labelText = content->lifetime().make_state<rpl::variable<QString>>();

	const auto session = &controller()->session();

	auto buttonText = labelText->value();

	const auto subscribe = CreateFlatSubscribeButton({
		controller(),
		content,
		[] { return u"business"_q; },
		std::move(buttonText),
		std::nullopt,
		[=] {
			const auto &options = session->api().premium().subscriptionOptions();
			const auto value = radioGroup->current();
			return (value < options.size() && value >= 0)
				? options[value].botUrl
				: QString();
		},
	});
	{
		const auto callback = [=](int value) {
			auto &api = session->api();
			const auto options = api.premium().subscriptionOptions();
			if (options.empty()) {
				return;
			}
			Assert(value < options.size() && value >= 0);
			auto text = tr::lng_premium_subscribe_button(
				tr::now,
				lt_cost,
				options[value].costPerMonth);
			*labelText = std::move(text);
		};
		radioGroup->changes() | rpl::on_next(callback, subscribe->lifetime());
		session->api().premium().statusTextValue() | rpl::on_next([=] {
			callback(radioGroup->current());
		}, subscribe->lifetime());
	}

	content->widthValue(
	) | rpl::on_next([=](int width) {
		const auto padding = st::settingsPremiumButtonPadding;
		subscribe->setFullWidth(width - padding.left() - padding.right());
	}, subscribe->lifetime());

	rpl::combine(
		subscribe->heightValue(),
		Data::AmPremiumValue(session),
		session->premiumPossibleValue()
	) | rpl::on_next([=](
			int buttonHeight,
			bool premium,
			bool premiumPossible) {
		const auto padding = st::settingsPremiumButtonPadding;
		const auto finalHeight = !premiumPossible
			? 0
			: !premium
			? (padding.top() + buttonHeight + padding.bottom())
			: 0;
		content->resize(content->width(), finalHeight);
		subscribe->moveToLeft(padding.left(), padding.top());
		subscribe->setVisible(!premium && premiumPossible);
	}, subscribe->lifetime());

	return base::make_weak(not_null<Ui::RpWidget*>{ content });
}

const auto kMeta = BuildHelper({
	.id = Business::Id(),
	.parentId = AboutId(),
	.title = &tr::lng_business_title,
	.icon = &st::menuIconShop,
}, [](SectionBuilder &builder) {
	BuildBusinessSectionContent(builder, nullptr);
});

} // namespace

template <>
struct SectionFactory<Business> : AbstractSectionFactory {
	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue
	) const final override {
		return object_ptr<Business>(parent, controller);
	}
	bool hasCustomTopBar() const final override {
		return false;
	}

	[[nodiscard]] static const std::shared_ptr<SectionFactory> &Instance() {
		static const auto result = std::make_shared<SectionFactory>();
		return result;
	}
};

Type BusinessId() {
	return Business::Id();
}

void ShowBusiness(not_null<Window::SessionController*> controller) {
	if (!controller->session().premiumPossible()) {
		controller->show(Box(PremiumUnavailableBox));
		return;
	}
	controller->showSettings(Settings::BusinessId());
}

std::vector<PremiumFeature> BusinessFeaturesOrder(
		not_null<::Main::Session*> session) {
	const auto mtpOrder = session->appConfig().get<Order>(
		"business_promo_order",
		FallbackOrder());
	return ranges::views::all(
		mtpOrder
	) | ranges::views::transform([](const QString &s) {
		if (s == u"greeting_message"_q) {
			return PremiumFeature::GreetingMessage;
		} else if (s == u"away_message"_q) {
			return PremiumFeature::AwayMessage;
		} else if (s == u"quick_replies"_q) {
			return PremiumFeature::QuickReplies;
		} else if (s == u"business_hours"_q) {
			return PremiumFeature::BusinessHours;
		} else if (s == u"business_location"_q) {
			return PremiumFeature::BusinessLocation;
		} else if (s == u"business_links"_q) {
			return PremiumFeature::ChatLinks;
		} else if (s == u"business_intro"_q) {
			return PremiumFeature::ChatIntro;
		} else if (s == u"business_bots"_q) {
			return PremiumFeature::BusinessBots;
		} else if (s == u"folder_tags"_q) {
			return PremiumFeature::FilterTags;
		}
		return PremiumFeature::kCount;
	}) | ranges::views::filter([](PremiumFeature feature) {
		return (feature != PremiumFeature::kCount);
	}) | ranges::to_vector;
}

namespace Builder {

SectionBuildMethod BusinessSection = kMeta.build;

} // namespace Builder
} // namespace Settings
