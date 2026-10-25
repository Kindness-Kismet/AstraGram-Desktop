/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_premium.h"
#include "settings/sections/settings_about.h"

#include "boxes/premium_preview_box.h"
#include "boxes/sticker_set_box.h"
#include "chat_helpers/stickers_lottie.h" // LottiePlayerFromDocument.
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/local_url_handlers.h" // Core::TryConvertUrlToLocal.
#include "core/ui_integration.h" // TextContext.
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_emoji_statuses.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h" // SerializeCustomEmojiId.
#include "data/stickers/data_stickers.h"
#include "history/view/media/history_view_sticker.h" // EmojiSize.
#include "history/view/media/history_view_sticker_player.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_card_layout.h"
#include "settings/settings_common_session.h"
#include "ui/abstract_button.h"
#include "ui/basic_click_handlers.h"
#include "ui/effects/gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h" // Ui::RadiobuttonGroup.
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/vertical_list.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "api/api_premium.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_settings_premium.h"
#include "styles/style_widgets.h"

// AyuGram includes
#include "extras/extras_settings.h"

namespace Settings {
namespace {

using namespace Builder;

[[nodiscard]] Data::PremiumSubscriptionOptions SubscriptionOptionsForRows(
		Data::PremiumSubscriptionOptions result) {
	for (auto &option : result) {
		const auto perYear = option.costPerYear;
		const auto perMonth = option.costPerMonth;

		option.costPerYear = tr::lng_premium_gift_per(
			tr::now,
			lt_cost,
			perMonth);
		option.costPerMonth = tr::lng_premium_subscribe_total(
			tr::now,
			lt_cost,
			perYear);

		if (option.duration == tr::lng_months(tr::now, lt_count, 1)) {
			option.costPerMonth = QString();
			option.costNoDiscount = QString();
			option.duration = tr::lng_premium_subscribe_months_1(tr::now);
		} else if (option.duration == tr::lng_months(tr::now, lt_count, 6)) {
			option.duration = tr::lng_premium_subscribe_months_6(tr::now);
		} else if (option.duration == tr::lng_years(tr::now, lt_count, 1)) {
			option.duration = tr::lng_premium_subscribe_months_12(tr::now);
		} else if (option.duration == tr::lng_years(tr::now, lt_count, 2)) {
			option.duration = tr::lng_premium_subscribe_months_24(tr::now);
		}
	}
	return result;
}

namespace Ref {
namespace Gift {

struct Data {
	PeerId peerId;
	int days = 0;
	bool me = false;

	explicit operator bool() const {
		return peerId != 0;
	}
};

[[nodiscard]] QString Serialize(const Data &gift) {
	return QString::number(gift.peerId.value)
		+ ':'
		+ QString::number(gift.days)
		+ ':'
		+ QString::number(gift.me ? 1 : 0);
}

[[nodiscard]] Data Parse(QStringView data) {
	const auto components = data.split(':');
	if (components.size() != 3) {
		return {};
	}
	return {
		.peerId = PeerId(components[0].toULongLong()),
		.days = components[1].toInt(),
		.me = (components[2].toInt() == 1),
	};
}

} // namespace Gift

namespace EmojiStatus {

struct Data {
	PeerId peerId;

	explicit operator bool() const {
		return peerId != 0;
	}
};

[[nodiscard]] QString Serialize(const Data &gift) {
	return QString("profile_:%1").arg(QString::number(gift.peerId.value));
}

[[nodiscard]] Data Parse(QStringView data) {
	if (data.startsWith(u"profile_:"_q)) {
		const auto components = data.split(':');
		if (components.size() != 2) {
			return {};
		}
		return {
			.peerId = PeerId(components[1].toULongLong()),
		};
	}
	return {};
}

} // namespace EmojiStatus

namespace PremiumGift {

struct Data {
	DocumentId documentId = 0;
	int perUserTotal = 0;

	explicit operator bool() const {
		return documentId != 0;
	}
};

[[nodiscard]] QString Serialize(const Data &gift) {
	return u"premiumgift_:%1,peruser_:%2"_q
		.arg(gift.documentId)
		.arg(gift.perUserTotal);
}

[[nodiscard]] Data Parse(QStringView data) {
	if (data.startsWith(u"premiumgift_:"_q)) {
		const auto components = data.split(',');
		if (components.size() != 2) {
			return {};
		}
		const auto first = components[0].split(':');
		const auto second = components[1].split(':');
		if (first.size() != 2 || second.size() != 2) {
			return {};
		}
		return {
			.documentId = DocumentId(first[1].toULongLong()),
			.perUserTotal = second[1].toInt(),
		};
	}
	return {};
}

} // namespace PremiumGift

} // namespace Ref

struct Entry {
	const style::icon *icon;
	rpl::producer<QString> title;
	rpl::producer<QString> description;
	PremiumFeature section = PremiumFeature::DoubleLimits;
};

struct PremiumState {
	QString ref;
	std::shared_ptr<Ui::RadiobuttonGroup> radioGroup;
	base::flat_map<QString, QPointer<Ui::AbstractButton>> featureButtons;

};

using Order = std::vector<QString>;

[[nodiscard]] Order FallbackOrder() {
	return Order{
		u"stories"_q,
		u"more_upload"_q,
		u"double_limits"_q,
		u"last_seen"_q,
		u"voice_to_text"_q,
		u"faster_download"_q,
		u"translations"_q,
		u"animated_emoji"_q,
		u"emoji_status"_q,
		u"saved_tags"_q,
		//u"peer_colors"_q,
		u"wallpapers"_q,
		u"profile_badge"_q,
		u"message_privacy"_q,
		u"advanced_chat_management"_q,
		u"no_ads"_q,
		//u"app_icons"_q,
		u"infinite_reactions"_q,
		u"animated_userpics"_q,
		u"premium_stickers"_q,
		u"business"_q,
		u"effects"_q,
		u"ai_compose"_q,
		u"rich_formatting"_q,
	};
}

[[nodiscard]] base::flat_map<QString, Entry> EntryMap() {
	return base::flat_map<QString, Entry>{
		{
			u"saved_tags"_q,
			Entry{
				&st::settingsPremiumIconTags,
				tr::lng_premium_summary_subtitle_tags_for_messages(),
				tr::lng_premium_summary_about_tags_for_messages(),
				PremiumFeature::TagsForMessages,
			},
		},
		{
			u"last_seen"_q,
			Entry{
				&st::settingsPremiumIconLastSeen,
				tr::lng_premium_summary_subtitle_last_seen(),
				tr::lng_premium_summary_about_last_seen(),
				PremiumFeature::LastSeen,
			},
		},
		{
			u"message_privacy"_q,
			Entry{
				&st::settingsPremiumIconPrivacy,
				tr::lng_premium_summary_subtitle_message_privacy(),
				tr::lng_premium_summary_about_message_privacy(),
				PremiumFeature::MessagePrivacy,
			},
		},
		{
			u"wallpapers"_q,
			Entry{
				&st::settingsPremiumIconWallpapers,
				tr::lng_premium_summary_subtitle_wallpapers(),
				tr::lng_premium_summary_about_wallpapers(),
				PremiumFeature::Wallpapers,
			},
		},
		{
			u"peer_colors"_q,
			Entry{
				&st::settingsPremiumIconPeerColors,
				tr::lng_premium_summary_subtitle_peer_colors(),
				tr::lng_premium_summary_about_peer_colors(),
				PremiumFeature::PeerColors,
			},
		},
		{
			u"stories"_q,
			Entry{
				&st::settingsPremiumIconStories,
				tr::lng_premium_summary_subtitle_stories(),
				tr::lng_premium_summary_about_stories(),
				PremiumFeature::Stories,
			},
		},
		{
			u"double_limits"_q,
			Entry{
				&st::settingsPremiumIconDouble,
				tr::lng_premium_summary_subtitle_double_limits(),
				tr::lng_premium_summary_about_double_limits(),
				PremiumFeature::DoubleLimits,
			},
		},
		{
			u"more_upload"_q,
			Entry{
				&st::settingsPremiumIconFiles,
				tr::lng_premium_summary_subtitle_more_upload(),
				tr::lng_premium_summary_about_more_upload(),
				PremiumFeature::MoreUpload,
			},
		},
		{
			u"faster_download"_q,
			Entry{
				&st::settingsPremiumIconSpeed,
				tr::lng_premium_summary_subtitle_faster_download(),
				tr::lng_premium_summary_about_faster_download(),
				PremiumFeature::FasterDownload,
			},
		},
		{
			u"voice_to_text"_q,
			Entry{
				&st::settingsPremiumIconVoice,
				tr::lng_premium_summary_subtitle_voice_to_text(),
				tr::lng_premium_summary_about_voice_to_text(),
				PremiumFeature::VoiceToText,
			},
		},
		{
			u"no_ads"_q,
			Entry{
				&st::settingsPremiumIconChannelsOff,
				tr::lng_premium_summary_subtitle_no_ads(),
				tr::lng_premium_summary_about_no_ads(),
				PremiumFeature::NoAds,
			},
		},
		{
			u"emoji_status"_q,
			Entry{
				&st::settingsPremiumIconStatus,
				tr::lng_premium_summary_subtitle_emoji_status(),
				tr::lng_premium_summary_about_emoji_status(),
				PremiumFeature::EmojiStatus,
			},
		},
		{
			u"infinite_reactions"_q,
			Entry{
				&st::settingsPremiumIconLike,
				tr::lng_premium_summary_subtitle_infinite_reactions(),
				tr::lng_premium_summary_about_infinite_reactions(),
				PremiumFeature::InfiniteReactions,
			},
		},
		{
			u"premium_stickers"_q,
			Entry{
				&st::settingsIconStickers,
				tr::lng_premium_summary_subtitle_premium_stickers(),
				tr::lng_premium_summary_about_premium_stickers(),
				PremiumFeature::Stickers,
			},
		},
		{
			u"animated_emoji"_q,
			Entry{
				&st::settingsIconEmoji,
				tr::lng_premium_summary_subtitle_animated_emoji(),
				tr::lng_premium_summary_about_animated_emoji(),
				PremiumFeature::AnimatedEmoji,
			},
		},
		{
			u"advanced_chat_management"_q,
			Entry{
				&st::settingsIconChat,
				tr::lng_premium_summary_subtitle_advanced_chat_management(),
				tr::lng_premium_summary_about_advanced_chat_management(),
				PremiumFeature::AdvancedChatManagement,
			},
		},
		{
			u"profile_badge"_q,
			Entry{
				&st::settingsPremiumIconStar,
				tr::lng_premium_summary_subtitle_profile_badge(),
				tr::lng_premium_summary_about_profile_badge(),
				PremiumFeature::ProfileBadge,
			},
		},
		{
			u"animated_userpics"_q,
			Entry{
				&st::settingsPremiumIconPlay,
				tr::lng_premium_summary_subtitle_animated_userpics(),
				tr::lng_premium_summary_about_animated_userpics(),
				PremiumFeature::AnimatedUserpics,
			},
		},
		{
			u"translations"_q,
			Entry{
				&st::settingsPremiumIconTranslations,
				tr::lng_premium_summary_subtitle_translation(),
				tr::lng_premium_summary_about_translation(),
				PremiumFeature::RealTimeTranslation,
			},
		},
		{
			u"business"_q,
			Entry{
				&st::settingsPremiumIconBusiness,
				tr::lng_premium_summary_subtitle_business(),
				tr::lng_premium_summary_about_business(),
				PremiumFeature::Business,
			},
		},
		{
			u"effects"_q,
			Entry{
				&st::settingsPremiumIconEffects,
				tr::lng_premium_summary_subtitle_effects(),
				tr::lng_premium_summary_about_effects(),
				PremiumFeature::Effects,
			},
		},
		{
			u"todo"_q,
			Entry{
				&st::settingsPremiumIconChecklist,
				tr::lng_premium_summary_subtitle_todo_lists(),
				tr::lng_premium_summary_about_todo_lists(),
				PremiumFeature::TodoLists,
			},
		},
		{
			u"no_forwards"_q,
			Entry{
				&st::settingsPremiumIconNoForwards,
				tr::lng_premium_summary_subtitle_no_forwards(),
				tr::lng_premium_summary_about_no_forwards(),
				PremiumFeature::NoForwards,
			},
		},
		{
			u"ai_compose"_q,
			Entry{
				&st::settingsPremiumIconAiCompose,
				tr::lng_premium_summary_subtitle_ai_compose(),
				tr::lng_premium_summary_about_ai_compose(),
				PremiumFeature::AiCompose,
			},
		},
		{
			u"rich_formatting"_q,
			Entry{
				&st::settingsPremiumIconRich,
				tr::lng_premium_summary_subtitle_rich_formatting(),
				tr::lng_premium_summary_about_rich_formatting(),
				PremiumFeature::RichFormatting,
			},
		},
	};
}

void SendAppLog(
		not_null<::Main::Session*> session,
		const QString &type,
		const MTPJSONValue &data) {
	const auto now = double(base::unixtime::now())
		+ (QTime::currentTime().msec() / 1000.);
	session->api().request(MTPhelp_SaveAppLog(
		MTP_vector<MTPInputAppEvent>(1, MTP_inputAppEvent(
			MTP_double(now),
			MTP_string(type),
			MTP_long(0),
			data
		))
	)).send();
}

[[nodiscard]] QString ResolveRef(const QString &ref) {
	return ref.isEmpty() ? "settings" : ref;
}

void SendScreenShow(
		not_null<Window::SessionController*> controller,
		const std::vector<QString> &order,
		const QString &ref) {
	auto list = QVector<MTPJSONValue>();
	list.reserve(order.size());
	for (const auto &element : order) {
		list.push_back(MTP_jsonString(MTP_string(element)));
	}
	auto values = QVector<MTPJSONObjectValue>{
		MTP_jsonObjectValue(
			MTP_string("premium_promo_order"),
			MTP_jsonArray(MTP_vector<MTPJSONValue>(std::move(list)))),
		MTP_jsonObjectValue(
			MTP_string("source"),
			MTP_jsonString(MTP_string(ResolveRef(ref)))),
	};
	const auto data = MTP_jsonObject(
		MTP_vector<MTPJSONObjectValue>(std::move(values)));
	SendAppLog(
		&controller->session(),
		"premium.promo_screen_show",
		data);
}

void SendScreenAccept(not_null<Window::SessionController*> controller) {
	SendAppLog(
		&controller->session(),
		"premium.promo_screen_accept",
		MTP_jsonNull());
}

class EmojiStatusTopBar final {
public:
	EmojiStatusTopBar(
		not_null<DocumentData*> document,
		Fn<void(QRect)> callback,
		QSizeF size);

	void setCenter(QPointF position);
	void setPaused(bool paused);
	void paint(QPainter &p);

private:
	QRectF _rect;
	std::shared_ptr<Data::DocumentMedia> _media;
	std::unique_ptr<HistoryView::StickerPlayer> _player;
	bool _paused = false;
	rpl::lifetime _lifetime;

};

EmojiStatusTopBar::EmojiStatusTopBar(
	not_null<DocumentData*> document,
	Fn<void(QRect)> callback,
	QSizeF size)
: _rect(QPointF(), size) {
	const auto sticker = document->sticker();
	Assert(sticker != nullptr);
	_media = document->createMediaView();
	_media->checkStickerLarge();
	_media->goodThumbnailWanted();

	rpl::single() | rpl::then(
		document->owner().session().downloaderTaskFinished()
	) | rpl::on_next([=] {
		if (!_media->loaded()) {
			return;
		}
		_lifetime.destroy();
		if (sticker->isLottie()) {
			_player = std::make_unique<HistoryView::LottiePlayer>(
				ChatHelpers::LottiePlayerFromDocument(
				_media.get(),
				ChatHelpers::StickerLottieSize::EmojiInteractionReserved7, //
				size.toSize(),
				Lottie::Quality::High));
		} else if (sticker->isWebm()) {
			_player = std::make_unique<HistoryView::WebmPlayer>(
				_media->owner()->location(),
				_media->bytes(),
				size.toSize());
		} else if (sticker) {
			_player = std::make_unique<HistoryView::StaticStickerPlayer>(
				_media->owner()->location(),
				_media->bytes(),
				size.toSize());
		}
		if (_player) {
			_player->setRepaintCallback([=] { callback(_rect.toRect()); });
		} else {
			callback(_rect.toRect());
		}
	}, _lifetime);
}

void EmojiStatusTopBar::setCenter(QPointF position) {
	const auto size = _rect.size();
	const auto shift = QPointF(size.width() / 2., size.height() / 2.);
	_rect = QRectF(QPointF(position - shift), QPointF(position + shift));
}

void EmojiStatusTopBar::setPaused(bool paused) {
	_paused = paused;
}

void EmojiStatusTopBar::paint(QPainter &p) {
	if (_player && _player->ready()) {
		const auto frame = _player->frame(
			_rect.size().toSize(),
			(_media->owner()->emojiUsesTextColor()
				? st::profileVerifiedCheckBg->c
				: QColor(0, 0, 0, 0)),
			false,
			crl::now(),
			_paused || On(PowerSaving::kEmojiStatus));

		p.drawImage(_rect.toRect(), frame.image);
		if (!_paused) {
			_player->markFrameShown();
		}
	}
}

[[nodiscard]] QString GetFeatureTitle(const QString &key) {
	if (key == u"saved_tags"_q) {
		return tr::lng_premium_summary_subtitle_tags_for_messages(tr::now);
	} else if (key == u"last_seen"_q) {
		return tr::lng_premium_summary_subtitle_last_seen(tr::now);
	} else if (key == u"message_privacy"_q) {
		return tr::lng_premium_summary_subtitle_message_privacy(tr::now);
	} else if (key == u"wallpapers"_q) {
		return tr::lng_premium_summary_subtitle_wallpapers(tr::now);
	} else if (key == u"peer_colors"_q) {
		return tr::lng_premium_summary_subtitle_peer_colors(tr::now);
	} else if (key == u"stories"_q) {
		return tr::lng_premium_summary_subtitle_stories(tr::now);
	} else if (key == u"double_limits"_q) {
		return tr::lng_premium_summary_subtitle_double_limits(tr::now);
	} else if (key == u"more_upload"_q) {
		return tr::lng_premium_summary_subtitle_more_upload(tr::now);
	} else if (key == u"faster_download"_q) {
		return tr::lng_premium_summary_subtitle_faster_download(tr::now);
	} else if (key == u"voice_to_text"_q) {
		return tr::lng_premium_summary_subtitle_voice_to_text(tr::now);
	} else if (key == u"no_ads"_q) {
		return tr::lng_premium_summary_subtitle_no_ads(tr::now);
	} else if (key == u"emoji_status"_q) {
		return tr::lng_premium_summary_subtitle_emoji_status(tr::now);
	} else if (key == u"infinite_reactions"_q) {
		return tr::lng_premium_summary_subtitle_infinite_reactions(tr::now);
	} else if (key == u"premium_stickers"_q) {
		return tr::lng_premium_summary_subtitle_premium_stickers(tr::now);
	} else if (key == u"animated_emoji"_q) {
		return tr::lng_premium_summary_subtitle_animated_emoji(tr::now);
	} else if (key == u"advanced_chat_management"_q) {
		return tr::lng_premium_summary_subtitle_advanced_chat_management(tr::now);
	} else if (key == u"profile_badge"_q) {
		return tr::lng_premium_summary_subtitle_profile_badge(tr::now);
	} else if (key == u"animated_userpics"_q) {
		return tr::lng_premium_summary_subtitle_animated_userpics(tr::now);
	} else if (key == u"translations"_q) {
		return tr::lng_premium_summary_subtitle_translation(tr::now);
	} else if (key == u"business"_q) {
		return tr::lng_premium_summary_subtitle_business(tr::now);
	} else if (key == u"effects"_q) {
		return tr::lng_premium_summary_subtitle_effects(tr::now);
	} else if (key == u"todo"_q) {
		return tr::lng_premium_summary_subtitle_todo_lists(tr::now);
	} else if (key == u"no_forwards"_q) {
		return tr::lng_premium_summary_subtitle_no_forwards(tr::now);
	} else if (key == u"ai_compose"_q) {
		return tr::lng_premium_summary_subtitle_ai_compose(tr::now);
	} else if (key == u"rich_formatting"_q) {
		return tr::lng_premium_summary_subtitle_rich_formatting(tr::now);
	}
	return QString();
}

void BuildPremiumFeatures(SectionBuilder &builder) {
	const auto session = builder.session();
	const auto mtpOrder = session->appConfig().get<Order>(
		"premium_promo_order",
		FallbackOrder());

	for (const auto &key : mtpOrder) {
		const auto title = GetFeatureTitle(key);
		if (title.isEmpty()) {
			continue;
		}
		builder.add(nullptr, [key, title] {
			return SearchEntry{
				.id = u"premium/"_q + key,
				.title = title,
			};
		});
	}
}

void SetupSubscriptionOptions(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		const QString &ref,
		std::shared_ptr<Ui::RadiobuttonGroup> radioGroup) {
	if (Ref::EmojiStatus::Parse(ref) || Ref::Gift::Parse(ref)) {
		return;
	}
	const auto options = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container, object_ptr<Ui::VerticalLayout>(container)));
	const auto apiPremium = &controller->session().api().premium();
	apiPremium->statusTextValue() | rpl::on_next([=] {
		const auto outer = options->entity();
		while (outer->count()) {
			delete outer->widgetAt(0);
		}
		const auto plans = SubscriptionOptionsForRows(apiPremium->subscriptionOptions());
		if (!plans.empty()
			&& (!radioGroup->hasValue() || radioGroup->current() >= int(plans.size()))) {
			radioGroup->setValue(0);
		}
		if (plans.size() < 2) {
			return;
		}
		const auto content = AddCardGroup(outer);
		for (auto i = 0; i != int(plans.size()); ++i) {
			const auto &plan = plans[i];
			const auto button = AddButtonWithIcon(content,
				rpl::single(plan.duration), st::settingsButtonNoIcon, {});
			button->setObjectName(u"premium/option/"_q + QString::number(i));
			auto padding = button->st().padding;
			padding.setLeft(st::settingsCardRowInset + st::defaultRadio.diameter
				+ st::settingsCardTextGap);
			button->setPaddingOverride(padding);
			const auto radio = Ui::CreateChild<Ui::Radiobutton>(button,
				radioGroup, i, QString(), st::settingsSendType);
			radio->setObjectName(u"premium/plan/"_q + QString::number(i));
			radio->setAccessibleName(plan.duration);
			radio->setAttribute(Qt::WA_TransparentForMouseEvents);
			button->setClickedCallback([=] { radioGroup->setValue(i); });
			const auto description = plan.costPerYear
				+ (plan.costPerMonth.isEmpty() ? QString() : u" · "_q + plan.costPerMonth)
				+ (plan.discount.isEmpty() ? QString() : u" · "_q + plan.discount);
			const auto label = Ui::CreateChild<Ui::FlatLabel>(button,
				description, st::settingsCardRowDescription);
			label->setAttribute(Qt::WA_TransparentForMouseEvents);
			button->widthValue() | rpl::on_next([=](int width) {
				label->resizeToWidth(std::max(width - padding.left() - padding.right(), 1));
				label->moveToLeft(padding.left(), st::settingsCardDescriptionTop);
			}, label->lifetime());
			label->heightValue() | rpl::on_next([=](int height) mutable {
				padding.setBottom(st::settingsCardDescriptionTop + height
					+ st::settingsCardRowInset - padding.top() - button->st().height);
				button->setPaddingOverride(padding);
			}, label->lifetime());
			button->heightValue() | rpl::on_next([=](int height) {
				radio->moveToLeft(st::settingsCardRowInset,
					(height - radio->height()) / 2);
			}, radio->lifetime());
			radio->show();
			label->show();
		}
	}, options->lifetime());
	options->toggleOn(rpl::combine(
		Data::AmPremiumValue(&controller->session()),
		apiPremium->statusTextValue() | rpl::map([=] {
			return apiPremium->subscriptionOptions().size() < 2;
		})
	) | rpl::map([](bool premium, bool noOptions) {
		return !premium && !noOptions;
	}), anim::type::instant);
}

void AddSummaryPremium(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller,
		const QString &ref,
		Fn<void(PremiumFeature)> buttonCallback,
		std::shared_ptr<PremiumState> state) {
	auto entryMap = EntryMap();
	const auto order = controller->session().appConfig().get<Order>(
		"premium_promo_order", FallbackOrder());
	for (const auto &key : order) {
		const auto i = entryMap.find(key);
		if (i == entryMap.end()) {
			continue;
		}
		auto &entry = i->second;
		const auto button = AddButtonWithIcon(content,
			std::move(entry.title), st::settingsButton, { .icon = entry.icon });
		button->setObjectName(u"premium/"_q + key);
		AddSectionRowDetails(button, std::move(entry.description));
		button->setClickedCallback([=, feature = entry.section] {
			buttonCallback(feature);
		});
		if (state) {
			state->featureButtons[key] = button;
		}
	}
	SendScreenShow(controller, order, ref);
}

void BuildPremiumSectionContent(
		SectionBuilder &builder,
		std::shared_ptr<PremiumState> state) {
	const auto controller = builder.controller();

	if (controller && state) {
		builder.addPageContent([controller, state](const WidgetContext &ctx) {
			SetupSubscriptionOptions(
				ctx.container,
				controller,
				state->ref,
				state->radioGroup);
		});
		builder.add([controller, state](const WidgetContext &ctx) {
			auto buttonCallback = [controller](PremiumFeature section) {
				ShowPremiumPreviewToBuy(controller, section, nullptr);
			};
			AddSummaryPremium(
				ctx.container,
				controller,
				state->ref,
				std::move(buttonCallback),
				state);

			return SectionBuilder::WidgetToAdd{};
		});
	}

	BuildPremiumFeatures(builder);
}

class Premium : public Section<Premium> {
public:
	Premium(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] base::weak_qptr<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;

	void showFinished() override;

private:
	void setupContent();
	void addOverview(not_null<Ui::VerticalLayout*> container);

	std::shared_ptr<PremiumState> _state;



};

Premium::Premium(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _state(std::make_shared<PremiumState>(PremiumState{
	.ref = ResolveRef(controller->premiumRef()),
	.radioGroup = std::make_shared<Ui::RadiobuttonGroup>(),
})) {
	setupContent();
	controller->session().api().premium().reload();
}

rpl::producer<QString> Premium::title() {
	return tr::lng_premium_summary_title();
}

void Premium::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto state = _state;

	const SectionBuildMethod buildMethod = [this, state](
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

		builder.addPageContent([=](const WidgetContext &ctx) {
			addOverview(ctx.container);
		});
		BuildPremiumSectionContent(builder, state);
	};

	build(content, buildMethod);

	Ui::ResizeFitChild(this, content);
}

void Premium::addOverview(not_null<Ui::VerticalLayout*> container) {
	const auto group = AddCardGroup(container);
	rpl::producer<QString> heading = tr::lng_premium_summary_title();
	auto about = [&]() -> rpl::producer<TextWithEntities> {
		const auto gift = Ref::Gift::Parse(_state->ref);
		if (gift) {
			auto &data = controller()->session().data();
			if (const auto peer = data.peer(gift.peerId)) {
				const auto months = gift.days / 30;
				return (gift.me
					? (months
						? tr::lng_premium_summary_subtitle_gift_me
						: tr::lng_premium_summary_subtitle_gift_days_me)
					: (months
						? tr::lng_premium_summary_subtitle_gift
						: tr::lng_premium_summary_subtitle_gift_days))(
						lt_count,
						rpl::single(float64(months ? months : gift.days)),
						lt_user,
						rpl::single(tr::bold(peer->name())),
						tr::rich);
			}
		}

		const auto &settings = ExtrasSettings::getInstance();
		if (settings.localPremium()) {
			return tr::extras_LocalPremiumNotice(tr::rich);
		}

		return rpl::conditional(
			Data::AmPremiumValue(&controller()->session()),
			controller()->session().api().premium().statusTextValue(
			) | rpl::map([](TextWithEntities text) {
				return text.text.isEmpty()
					? tr::lng_premium_summary_user_about(tr::now, tr::rich)
					: text;
			}),
			tr::lng_premium_summary_top_about(tr::rich));
	}();

	if (const auto gift = Ref::PremiumGift::Parse(_state->ref)) {
		heading = tr::lng_gift_premium_title();
		about = tr::lng_gift_premium_text(
			lt_count, rpl::single(gift.perUserTotal * 1.), tr::rich);
	} else if (const auto status = Ref::EmojiStatus::Parse(_state->ref)) {
		const auto peer = controller()->session().data().peer(status.peerId);
		heading = Info::Profile::NameValue(peer);
		about = tr::lng_premium_emoji_status_about(tr::rich);
	}
	group->add(object_ptr<Ui::FlatLabel>(group, std::move(heading),
		st::settingsServiceTitle), st::settingsServiceTitlePadding);
	group->add(object_ptr<Ui::FlatLabel>(group,
		std::move(about), st::settingsCardHint, st::defaultPopupMenu,
		Core::TextContext({ .session = &controller()->session() })),
		st::settingsServiceAboutPadding);
	if (const auto status = Ref::EmojiStatus::Parse(_state->ref)) {
		const auto session = &controller()->session();
		const auto peer = session->data().peer(status.peerId);
		const auto details = group->add(object_ptr<Ui::VerticalLayout>(group));
		Info::Profile::EmojiStatusIdValue(peer) | rpl::on_next([=](EmojiStatusId id) {
			while (details->count()) {
				delete details->widgetAt(0);
			}
			const auto documentId = id.collectible ? id.collectible->documentId : id.documentId;
			if (!documentId) {
				return;
			}
			const auto document = session->data().document(documentId);
			const auto sticker = document->sticker();
			if (!sticker) {
				return;
			}
			const auto &sets = session->data().stickers().sets();
			const auto set = sets.find(sticker->set.id);
			if (set == sets.end()) {
				return;
			}
			const auto button = AddButtonWithIcon(details,
				rpl::single(set->second->title), st::settingsButtonNoIcon, {});
			AddSectionRowDetails(button);
			button->setClickedCallback([=, identifier = sticker->set] {
				controller()->show(Box<StickerSetBox>(controller()->uiShow(),
					identifier, Data::StickersType::Emoji));
			});
		}, details->lifetime());
	}
}

void Premium::showFinished() {
	crl::on_main(this, [=] {
		for (const auto &[key, button] : _state->featureButtons) {
			const auto id = u"premium/"_q + key;
			controller()->checkHighlightControl(id, button);
		}
	});
}

base::weak_qptr<Ui::RpWidget> Premium::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	if (Ref::Gift::Parse(_state->ref)) {
		return nullptr;
	}
	const auto content = Ui::CreateChild<Ui::RpWidget>(parent.get());
	const auto state = _state;
	const auto labelText = content->lifetime().make_state<rpl::variable<QString>>();

	const auto emojiStatusData = Ref::EmojiStatus::Parse(state->ref);
	const auto session = &controller()->session();

	auto buttonText = [&]() -> std::optional<rpl::producer<QString>> {
		if (emojiStatusData) {
			auto &data = session->data();
			if (const auto peer = data.peer(emojiStatusData.peerId)) {
				return Info::Profile::EmojiStatusIdValue(
					peer
				) | rpl::map([=](EmojiStatusId id) {
					return id
						? tr::lng_premium_emoji_status_button()
						: labelText->value();
				}) | rpl::flatten_latest();
			}
		}
		return labelText->value();
	}();

	const auto subscribe = CreateFlatSubscribeButton({
		controller(),
		content,
		[ref = state->ref] { return ref; },
		std::move(buttonText),
		std::nullopt,
		[=] {
			const auto &options = session->api().premium().subscriptionOptions();
			const auto value = state->radioGroup->current();
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
		state->radioGroup->changes() | rpl::on_next(callback, subscribe->lifetime());
		session->api().premium().statusTextValue() | rpl::on_next([=] {
			callback(state->radioGroup->current());
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
	.id = Premium::Id(),
	.parentId = AboutId(),
	.title = &tr::lng_premium_summary_title,
	.icon = &st::menuIconPremium,
}, [](SectionBuilder &builder) {
	BuildPremiumSectionContent(builder, nullptr);
});

} // namespace

template <>
struct SectionFactory<Premium> : AbstractSectionFactory {
	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue
	) const final override {
		return object_ptr<Premium>(parent, controller);
	}
	bool hasCustomTopBar() const final override {
		return false;
	}

	[[nodiscard]] static const std::shared_ptr<SectionFactory> &Instance() {
		static const auto result = std::make_shared<SectionFactory>();
		return result;
	}
};

Type PremiumId() {
	return Premium::Id();
}

void ShowPremium(not_null<::Main::Session*> session, const QString &ref) {
	const auto active = Core::App().activeWindow();
	const auto controller = (active && active->isPrimary())
		? active->sessionController()
		: nullptr;
	if (controller && session == &controller->session()) {
		ShowPremium(controller, ref);
	} else {
		for (const auto &controller : session->windows()) {
			if (controller->window().isPrimary()) {
				ShowPremium(controller, ref);
			}
		}
	}
}

void ShowPremium(
		not_null<Window::SessionController*> controller,
		const QString &ref) {
	controller->window().activate();
	if (!controller->session().premiumPossible()) {
		controller->show(Box(PremiumUnavailableBox));
		return;
	}
	controller->setPremiumRef(ref);
	controller->showSettings(Settings::PremiumId());
}

void ShowGiftPremium(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		int days,
		bool me) {
	ShowPremium(controller, Ref::Gift::Serialize({ peer->id, days, me }));
}

void ShowEmojiStatusPremium(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	if (const auto unique = peer->emojiStatusId().collectible.get()) {
		Core::ResolveAndShowUniqueGift(controller->uiShow(), unique->slug);
	} else {
		ShowPremium(controller, Ref::EmojiStatus::Serialize({ peer->id }));
	}
}

void ShowPremiumGiftPremium(
		not_null<Window::SessionController*> controller,
		const Data::StarGift &gift) {
	ShowPremium(controller, Ref::PremiumGift::Serialize({
		.documentId = gift.document->id,
		.perUserTotal = gift.perUserTotal,
	}));
}

void StartPremiumPayment(
		not_null<Window::SessionController*> controller,
		const QString &ref) {
	const auto session = &controller->session();
	const auto username = session->appConfig().get<QString>(
		u"premium_bot_username"_q,
		QString());
	const auto slug = session->appConfig().get<QString>(
		u"premium_invoice_slug"_q,
		QString());
	if (!username.isEmpty()) {
		controller->showPeerByLink(Window::PeerByLinkInfo{
			.usernameOrId = username,
			.resolveType = Window::ResolveType::BotStart,
			.startToken = ref,
			.startAutoSubmit = true,
		});
	} else if (!slug.isEmpty()) {
		UrlClickHandler::Open("https://t.me/$" + slug);
	}
}

QString LookupPremiumRef(PremiumFeature section) {
	for (const auto &[ref, entry] : EntryMap()) {
		if (entry.section == section) {
			return ref;
		}
	}
	return QString();
}

void ShowPremiumPromoToast(
		std::shared_ptr<ChatHelpers::Show> show,
		TextWithEntities textWithLink,
		const QString &ref) {
	ShowPremiumPromoToast(show, [=](
			not_null<::Main::Session*> session) {
		Expects(&show->session() == session);

		return show->resolveWindow();
	}, std::move(textWithLink), ref);
}

void ShowPremiumPromoToast(
		std::shared_ptr<::Main::SessionShow> show,
		Fn<Window::SessionController*(
			not_null<::Main::Session*>)> resolveWindow,
		TextWithEntities textWithLink,
		const QString &ref) {
	using WeakToast = base::weak_ptr<Ui::Toast::Instance>;
	const auto toast = std::make_shared<WeakToast>();
	(*toast) = show->showToast({
		.text = std::move(textWithLink),
		.filter = crl::guard(&show->session(), [=](
				const ClickHandlerPtr &handler,
				Qt::MouseButton button) {
			if (button != Qt::LeftButton) {
				return false;
			}
			const auto url = handler ? handler->url() : QString();
			if (!url.isEmpty() && !url.startsWith(u"internal:"_q)) {
				if (const auto strong = toast->get()) {
					strong->hideAnimated();
					(*toast) = nullptr;
				}
				return true;
			}
			if (const auto strong = toast->get()) {
				strong->hideAnimated();
				(*toast) = nullptr;
				if (const auto controller = resolveWindow(
						&show->session())) {
					Settings::ShowPremium(controller, ref);
				}
			}
			return true;
		}),
		.icon = &st::settingsToastStarIcon,
		.adaptive = true,
		.duration = Ui::Toast::kDefaultDuration * 2,
	});
}

not_null<Ui::RoundButton*> CreateLockedButton(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		const style::RoundButton &st,
		rpl::producer<bool> locked) {
	const auto result = Ui::CreateChild<Ui::RoundButton>(
		parent.get(),
		rpl::single(QString()),
		st);

	const auto labelSt = result->lifetime().make_state<style::FlatLabel>(
		st::defaultFlatLabel);
	labelSt->style.font = st.style.font;
	labelSt->textFg = st.textFg;

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		result,
		std::move(text),
		*labelSt);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto icon = Ui::CreateChild<Ui::RpWidget>(result);
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->resize(st::stickersPremiumLock.size());
	icon->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(icon);
		st::stickersPremiumLock.paint(p, 0, 0, icon->width());
	}, icon->lifetime());

	rpl::combine(
		result->widthValue(),
		label->widthValue(),
		std::move(locked)
	) | rpl::on_next([=](int outer, int inner, bool locked) {
		if (locked) {
			icon->show();
			inner += icon->width();
			label->move(
				(outer - inner) / 2 + icon->width(),
				st::similarChannelsLock.textTop);
			icon->move(
				(outer - inner) / 2,
				st::similarChannelsLock.textTop);
		} else {
			icon->hide();
			label->move(
				(outer - inner) / 2,
				st::similarChannelsLock.textTop);
		}
	}, result->lifetime());

	return result;
}

not_null<Ui::GradientButton*> CreateSubscribeButton(
		SubscribeButtonArgs &&args) {
	Expects(args.show || args.controller);

	auto show = args.show ? std::move(args.show) : args.controller->uiShow();
	auto resolve = [show](not_null<::Main::Session*> session) {
		Expects(session == &show->session());

		return show->resolveWindow();
	};
	return CreateSubscribeButton(
		std::move(show),
		std::move(resolve),
		std::move(args));
}

namespace {

rpl::producer<QString> SetupSubscribeButton(
		not_null<Ui::AbstractButton*> result,
		std::shared_ptr<::Main::SessionShow> show,
		Fn<Window::SessionController*(not_null<::Main::Session*>)> resolveWindow,
		SubscribeButtonArgs &&args) {
	result->setClickedCallback([
			show,
			resolveWindow,
			promo = args.showPromo,
			computeRef = args.computeRef,
			computeBotUrl = args.computeBotUrl] {
		const auto window = resolveWindow(
			&show->session());
		if (!window) {
			return;
		}
		if (promo) {
			Settings::ShowPremium(window, computeRef());
			return;
		}
		window->window().activate();
		const auto url = computeBotUrl ? computeBotUrl() : QString();
		if (!url.isEmpty()) {
			const auto local = Core::TryConvertUrlToLocal(url);
			if (local.isEmpty()) {
				return;
			}
			UrlClickHandler::Open(
				local,
				QVariant::fromValue(ClickHandlerContext{
					.sessionWindow = base::make_weak(window),
					.botStartAutoSubmit = true,
				}));
		} else {
			SendScreenAccept(window);
			StartPremiumPayment(window, computeRef());
		}
	});

	const auto premium = &show->session().api().premium();
	premium->reload();
	const auto computeCost = [=] {
		const auto amount = premium->monthlyAmount();
		const auto currency = premium->monthlyCurrency();
		const auto valid = (amount > 0) && !currency.isEmpty();
		return Ui::FillAmountAndCurrency(
			valid ? amount : 500,
			valid ? currency : "USD");
	};

	return args.text
		? base::take(*args.text)
		: tr::lng_premium_summary_button(
			lt_cost, premium->statusTextValue() | rpl::map(computeCost));
}

} // namespace

not_null<Ui::RoundButton*> CreateFlatSubscribeButton(SubscribeButtonArgs &&args) {
	Expects(args.show || args.controller);
	auto show = args.show ? std::move(args.show) : args.controller->uiShow();
	auto resolve = [show](not_null<::Main::Session*> session) {
		Expects(session == &show->session());
		return show->resolveWindow();
	};
	const auto result = Ui::CreateChild<Ui::RoundButton>(
		args.parent.get(), rpl::never<QString>(), st::settingsServiceButton);
	result->setText(rpl::combine(
		SetupSubscribeButton(result, show, resolve, std::move(args)),
		tr::lng_channel_invite_subscription_button()
	) | rpl::map([](QString text, QString subscribe) {
		return text.isEmpty() ? subscribe : text;
	}));
	return result;
}

not_null<Ui::GradientButton*> CreateSubscribeButton(
		std::shared_ptr<::Main::SessionShow> show,
		Fn<Window::SessionController*(
			not_null<::Main::Session*>)> resolveWindow,
		SubscribeButtonArgs &&args) {
	const auto result = Ui::CreateChild<Ui::GradientButton>(
		args.parent.get(),
		args.gradientStops
			? base::take(*args.gradientStops)
			: Ui::Premium::ButtonGradientStops());

	auto text = SetupSubscribeButton(result, show, resolveWindow, std::move(args));

	const auto &st = st::premiumPreviewBox.button;
	result->resize(args.parent->width(), st.height);

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		result, std::move(text), st::premiumPreviewButtonLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(result->widthValue(), label->widthValue()
	) | rpl::on_next([=](int outer, int width) {
		label->moveToLeft((outer - width) / 2,
			st::premiumPreviewBox.button.textTop, outer);
	}, label->lifetime());
	return result;
}

std::vector<PremiumFeature> PremiumFeaturesOrder(
		not_null<::Main::Session*> session) {
	const auto mtpOrder = session->appConfig().get<Order>(
		"premium_promo_order",
		FallbackOrder());
	return ranges::views::all(
		mtpOrder
	) | ranges::views::transform([](const QString &s) {
		if (s == u"more_upload"_q) {
			return PremiumFeature::MoreUpload;
		} else if (s == u"faster_download"_q) {
			return PremiumFeature::FasterDownload;
		} else if (s == u"voice_to_text"_q) {
			return PremiumFeature::VoiceToText;
		} else if (s == u"no_ads"_q) {
			return PremiumFeature::NoAds;
		} else if (s == u"emoji_status"_q) {
			return PremiumFeature::EmojiStatus;
		} else if (s == u"infinite_reactions"_q) {
			return PremiumFeature::InfiniteReactions;
		} else if (s == u"saved_tags"_q) {
			return PremiumFeature::TagsForMessages;
		} else if (s == u"last_seen"_q) {
			return PremiumFeature::LastSeen;
		} else if (s == u"message_privacy"_q) {
			return PremiumFeature::MessagePrivacy;
		} else if (s == u"premium_stickers"_q) {
			return PremiumFeature::Stickers;
		} else if (s == u"animated_emoji"_q) {
			return PremiumFeature::AnimatedEmoji;
		} else if (s == u"advanced_chat_management"_q) {
			return PremiumFeature::AdvancedChatManagement;
		} else if (s == u"profile_badge"_q) {
			return PremiumFeature::ProfileBadge;
		} else if (s == u"animated_userpics"_q) {
			return PremiumFeature::AnimatedUserpics;
		} else if (s == u"translations"_q) {
			return PremiumFeature::RealTimeTranslation;
		} else if (s == u"wallpapers"_q) {
			return PremiumFeature::Wallpapers;
		} else if (s == u"effects"_q) {
			return PremiumFeature::Effects;
		} else if (s == u"todo"_q) {
			return PremiumFeature::TodoLists;
		} else if (s == u"peer_colors"_q) {
			return PremiumFeature::PeerColors;
		} else if (s == u"gifts"_q) {
			return PremiumFeature::Gifts;
		} else if (s == u"no_forwards"_q) {
			return PremiumFeature::NoForwards;
		} else if (s == u"ai_compose"_q) {
			return PremiumFeature::AiCompose;
		} else if (s == u"rich_formatting"_q) {
			return PremiumFeature::RichFormatting;
		}
		return PremiumFeature::kCount;
	}) | ranges::views::filter([](PremiumFeature type) {
		return (type != PremiumFeature::kCount);
	}) | ranges::to_vector;
}

std::unique_ptr<Ui::RpWidget> MakeEmojiStatusPreview(
		not_null<QWidget*> parent,
		not_null<DocumentData*> document) {
	auto result = std::make_unique<Ui::RpWidget>(parent);

	const auto raw = result.get();
	const auto size = HistoryView::Sticker::EmojiSize();
	const auto emoji = raw->lifetime().make_state<EmojiStatusTopBar>(
		document,
		[=](QRect r) { raw->update(std::move(r)); },
		size);
	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		emoji->paint(p);
	}, raw->lifetime());

	raw->sizeValue() | rpl::on_next([=](QSize size) {
		emoji->setCenter(QPointF(size.width() / 2., size.height() / 2.));
	}, raw->lifetime());

	return result;
}

} // namespace Settings
