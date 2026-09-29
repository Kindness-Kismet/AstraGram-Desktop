#include "ayu/features/filters/filters_utils.h"

#include "apiwrap.h"
#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database.h"
#include "ayu/features/filters/filters_cache_controller.h"
#include "ayu/utils/telegram_helpers.h"
#include "base/qthelp_url.h"
#include "boxes/abstract_box.h"
#include "core/local_url_handlers.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_text_entity.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toast/toast.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <QByteArray>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <qjsondocument.h>
#include <QString>
#include <thread>
#include <vector>
#include <QtNetwork/QHttpPart>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

constexpr auto BACKUP_VERSION = 2;

enum class PeerResolveHintType {
	Username,
	Invite,
};

struct PeerResolveHint {
	PeerResolveHintType type = PeerResolveHintType::Username;
	QString value;
};

bool HasChanges(const ApplyChanges &changes) {
	return !changes.newFilters.empty()
		|| !changes.removeFiltersById.empty()
		|| !changes.filtersOverrides.empty()
		|| !changes.newExclusions.empty()
		|| !changes.removeExclusions.empty()
		|| !changes.peersToBeResolved.empty();
}

void AppendChangeSummary(
		TextWithEntities &summary,
		TextWithEntities &&line) {
	if (!summary.text.isEmpty()) {
		summary.append('\n');
	}
	summary.append(std::move(line));
}

TextWithEntities ChangeSummaryText(const ApplyChanges &changes) {
	auto result = tr::marked();

	if (!changes.newFilters.empty()) {
		AppendChangeSummary(
			result,
			tr::ayu_FiltersSheetNewFilters(
				tr::now,
				lt_count,
				int(changes.newFilters.size()),
				tr::rich));
	}
	if (!changes.removeFiltersById.empty()) {
		AppendChangeSummary(
			result,
			tr::ayu_FiltersSheetRemovedFilters(
				tr::now,
				lt_count,
				int(changes.removeFiltersById.size()),
				tr::rich));
	}
	if (!changes.filtersOverrides.empty()) {
		AppendChangeSummary(
			result,
			tr::ayu_FiltersSheetUpdatedFilters(
				tr::now,
				lt_count,
				int(changes.filtersOverrides.size()),
				tr::rich));
	}
	if (!changes.newExclusions.empty()) {
		AppendChangeSummary(
			result,
			tr::ayu_FiltersSheetNewExclusions(
				tr::now,
				lt_count,
				int(changes.newExclusions.size()),
				tr::rich));
	}
	if (!changes.removeExclusions.empty()) {
		AppendChangeSummary(
			result,
			tr::ayu_FiltersSheetRemovedExclusions(
				tr::now,
				lt_count,
				int(changes.removeExclusions.size()),
				tr::rich));
	}
	if (!changes.peersToBeResolved.empty()) {
		AppendChangeSummary(
			result,
			tr::ayu_FiltersSheetDialogsToResolve(
				tr::now,
				lt_count,
				int(changes.peersToBeResolved.size()),
				tr::rich));
	}

	return result;
}

std::optional<PeerResolveHint> ParsePeerResolveHint(QString value) {
	value = value.trimmed();
	if (value.isEmpty()) {
		return std::nullopt;
	}

	const auto converted = Core::TryConvertUrlToLocal(value);
	const auto local = converted.isEmpty() ? value : converted;
	const auto delimiter = local.indexOf('?');
	const auto params = (delimiter > 0)
		? qthelp::url_parse_params(
			local.mid(delimiter + 1),
			qthelp::UrlParamNameTransform::ToLower)
		: QMap<QString, QString>();

	if (local.startsWith(u"tg://join"_q, Qt::CaseInsensitive)) {
		const auto invite = params.value(u"invite"_q);
		if (!invite.isEmpty()) {
			return PeerResolveHint{
				.type = PeerResolveHintType::Invite,
				.value = invite,
			};
		}
	} else if (local.startsWith(u"tg://resolve"_q, Qt::CaseInsensitive)) {
		const auto domain = params.value(u"domain"_q);
		if (!domain.isEmpty()) {
			return PeerResolveHint{
				.type = PeerResolveHintType::Username,
				.value = domain,
			};
		}
	}

	if (value.startsWith('@')) {
		value = value.mid(1);
	}
	if (value.startsWith('+')) {
		return PeerResolveHint{
			.type = PeerResolveHintType::Invite,
			.value = value.mid(1),
		};
	}
	if (value.startsWith("joinchat/"_q, Qt::CaseInsensitive)) {
		return PeerResolveHint{
			.type = PeerResolveHintType::Invite,
			.value = value.mid(9),
		};
	}

	const auto slash = value.indexOf('/');
	if (slash >= 0) {
		value = value.left(slash);
	}
	if (value.isEmpty()) {
		return std::nullopt;
	}
	return PeerResolveHint{
		.type = PeerResolveHintType::Username,
		.value = value,
	};
}

std::vector<char> ParseFilterId(QString id) {
	id.remove('-');
	if (id.size() != 32) {
		return {};
	}

	const auto bytes = QByteArray::fromHex(id.toUtf8());
	if (bytes.size() != 16) {
		return {};
	}
	return std::vector(bytes.constData(), bytes.constData() + bytes.size());
}

PeerData *LoadedPeerFromDialogId(not_null<Data::Session*> data, ID dialogId) {
	if (!dialogId) {
		return nullptr;
	}
	const auto bare = (dialogId < 0) ? -dialogId : dialogId;
	if (dialogId > 0) {
		return data->userLoaded(UserId(bare));
	}
	if (const auto channel = data->channelLoaded(ChannelId(bare))) {
		return channel;
	}
	return data->chatLoaded(ChatId(bare));
}

void ResolveFilterBackupPeers(const std::vector<QString> &peerHints) {
	auto session = currentSession();
	auto hintsCopy = peerHints;

	crl::async([=]
	{
		for (const auto &entry : hintsCopy) {
			const auto hint = ParsePeerResolveHint(entry);
			if (!hint || hint->value.isEmpty()) {
				continue;
			}
			auto latch = std::make_shared<TimedCountDownLatch>(1);
			auto floodWait = std::make_shared<std::atomic_bool>(false);

			auto onFail = [=](const MTP::Error &error)
			{
				if (MTP::IsFloodError(error.type())) {
					floodWait->store(true);
				}
				latch->countDown();
			};

			crl::on_main([=]
			{
				if (hint->type == PeerResolveHintType::Username) {
					session->api().request(MTPcontacts_ResolveUsername(
						MTP_flags(0),
						MTP_string(hint->value),
						MTP_string()
					)).done([=](const MTPcontacts_ResolvedPeer &result)
					{
						const auto &data = result.data();
						session->data().processUsers(data.vusers());
						session->data().processChats(data.vchats());
						latch->countDown();
					}).fail(onFail).send();
				} else {
					session->api().checkChatInvite(
						hint->value,
						[=](const MTPChatInvite &invite)
						{
							invite.match([=](const MTPDchatInvite &data)
							{
							}, [=](const MTPDchatInviteAlready &data)
							{
								session->data().processChat(data.vchat());
							}, [=](const MTPDchatInvitePeek &data)
							{
								session->data().processChat(data.vchat());
							});
							latch->countDown();
						},
						onFail);
				}
			});
			latch->await(std::chrono::seconds(20));
			if (floodWait->load()) {
				std::this_thread::sleep_for(std::chrono::seconds(20));
			}
		}
	});
}

void FilterUtils::importFromLink(const QString &link) {
	if (link.isEmpty()) {
		Ui::Toast::Show(tr::ayu_FiltersToastFailFetch(tr::now));
		return;
	}

	const auto request = QNetworkRequest(QUrl(link));
	const auto reply = _manager->get(request);
	const auto failed = std::make_shared<bool>(false);

	connect(
		reply,
		&QNetworkReply::finished,
		this,
		[=]
		{
			if (*failed) {
				reply->deleteLater();
				return;
			}

			const auto responseData = reply->readAll();

			const auto jsonString = QString::fromUtf8(responseData);

			if (jsonString.isNull()) {
				LOG(("FilterUtils: Invalid response."));
				Ui::Toast::Show(tr::ayu_FiltersToastFailImport(tr::now));

				reply->deleteLater();
				return;
			}

			handleResponse(jsonString.toUtf8());
			reply->deleteLater();
		});

	connect(
		reply,
		&QNetworkReply::errorOccurred,
		this,
		[=](QNetworkReply::NetworkError e)
		{
			if (*failed) {
				return;
			}
			*failed = true;
			gotFailure(e);
			reply->deleteLater();
		});
}

void FilterUtils::publishFilters() {
	const auto exported = exportFilters();

	auto multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

	QHttpPart contentPart;
	contentPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"content\""));
	contentPart.setBody(exported.toUtf8());

	QHttpPart syntaxPart;
	syntaxPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"syntax\""));
	syntaxPart.setBody("json");

	QHttpPart titlePart;
	titlePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"title\""));
	titlePart.setBody("AstraGram Filters");

	multiPart->append(contentPart);
	multiPart->append(syntaxPart);
	multiPart->append(titlePart);

	QNetworkRequest request(QUrl("https://dpaste.com/api/v2/"));

	const auto reply = _manager->post(request, multiPart);
	multiPart->setParent(reply);

	connect(
		reply,
		&QNetworkReply::finished,
		this,
		[=]
		{
			const auto error = reply->error();
			const auto location = reply->header(QNetworkRequest::LocationHeader);

			if (error == QNetworkReply::NoError && location.isValid()) {
				auto url = location.toString();
				url.append(".txt");
				QGuiApplication::clipboard()->setText(url);

				Ui::Toast::Show(tr::lng_stickers_copied(tr::now));
			} else {
				LOG(("Failed to publish filters to dpaste, error: %1").arg(reply->errorString()));

				Ui::Toast::Show(tr::ayu_FiltersToastFailPublish(tr::now));
			}
			reply->deleteLater();
		});
}

void FilterUtils::importFromJson(const QByteArray &json) {
	auto error = QJsonParseError{0, QJsonParseError::NoError};
	const auto document = QJsonDocument::fromJson(json, &error);

	if (error.error != QJsonParseError::NoError) {
		Ui::Toast::Show(tr::ayu_FiltersToastFailImport(tr::now));
		LOG(("FilterUtils: Failed to parse JSON, error: %1"
		).arg(error.errorString()));
		return;
	}
	if (!document.isObject()) {
		Ui::Toast::Show(tr::ayu_FiltersToastFailImport(tr::now));
		LOG(("FilterUtils: not an object received in JSON"));
		return;
	}
	const auto changes = prepareChanges(document.object());

	if (!HasChanges(changes)) {
		Ui::Toast::Show(tr::ayu_FiltersToastFailNoChanges(tr::now));
		LOG(("FilterUtils: received empty changes"));
		return;
	}

	auto box = Ui::MakeConfirmBox({
		.text = ChangeSummaryText(changes),
		.confirmed = [=](Fn<void()> close) {
			close();
			try {
				applyChanges(changes);
				Ui::Toast::Show(tr::ayu_FiltersToastSuccess(tr::now));
			} catch (...) {
				LOG(("FilterUtils: Failed to apply import changes"));
				Ui::Toast::Show(tr::ayu_FiltersToastFailImport(tr::now));
			}
		},
		.confirmText = tr::ayu_FiltersMenuImport(),
		.title = tr::ayu_FiltersSheetTitle(),
	});
	Ui::show(std::move(box));
}

struct BackupExclusion
{
	ID dialogId;
	std::vector<char> filterId;

	QJsonObject toJson() const {
		QJsonObject json;
		json["dialogId"] = dialogId;

		// make it look like java's UUID
		auto hexId = QString(QByteArray(filterId.data(), filterId.size()).toHex());
		if (hexId.length() == 32) {
			hexId.insert(8, '-');
			hexId.insert(13, '-');
			hexId.insert(18, '-');
			hexId.insert(23, '-');
		}
		json["filterId"] = hexId;
		return json;
	}
};

QString FilterUtils::exportFilters() {
	auto createJsonArray = [&](const auto &container)
	{
		QJsonArray jsonArray;
		for (const auto &item : container) {
			jsonArray.append(item.toJson());
		}
		return jsonArray;
	};

	QJsonArray filtersArray;
	QJsonObject jsonObject;
	jsonObject["version"] = BACKUP_VERSION;
	const auto filters = AyuDatabase::getAllRegexFilters();

	for (const auto &item : filters) {
		QJsonObject filterJson;
		filterJson["caseInsensitive"] = item.caseInsensitive;
		if (item.dialogId.has_value()) {
			filterJson["dialogId"] = item.dialogId.value();
		} else {
			filterJson["dialogId"] = QJsonValue();
		}
		filterJson["enabled"] = item.enabled;
		filterJson["reversed"] = item.reversed;
		filterJson["text"] = QString::fromStdString(item.text);

		// make it look like java's UUID
		auto hexId = QString(QByteArray(item.id.data(), item.id.size()).toHex());
		if (hexId.length() == 32) {
			hexId.insert(8, '-');
			hexId.insert(13, '-');
			hexId.insert(18, '-');
			hexId.insert(23, '-');
		}
		filterJson["id"] = hexId;
		filtersArray.append(filterJson);
	}
	jsonObject["filters"] = filtersArray;

	const auto excl = AyuDatabase::getAllFiltersExclusions();


	std::vector<BackupExclusion> exclusions;
	exclusions.reserve(excl.size());

	for (const auto &item : excl) {
		exclusions.push_back(BackupExclusion{item.dialogId, item.filterId});
	}

	jsonObject["exclusions"] = createJsonArray(exclusions);
	jsonObject["removeFiltersById"] = QJsonValue();
	jsonObject["removeExclusions"] = QJsonValue();

	QJsonObject peers;
	for (const auto &item : filters) {
		const auto &session = currentSession();
		if (!item.dialogId.has_value()) {
			continue;
		}
		const auto peer = LoadedPeerFromDialogId(&session->data(), item.dialogId.value());
		if (!peer || peer->username().isEmpty()) {
			continue;
		}
		peers[QString::number(item.dialogId.value())] = peer->username();
	}
	jsonObject["peers"] = peers;


	QJsonDocument jsonDoc(jsonObject);
	QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Indented);
	return QString::fromUtf8(jsonData);
}

// for compatibility with Android version

namespace {

[[nodiscard]] int regularMessageType(not_null<const HistoryItem*> item) {
	if (item->richPage()) {
		return 36; // TYPE_ARTICLE
	}
	const auto media = item->media();
	if (!media) {
		return item->isOnlyEmojiAndSpaces()
			? 19 // TYPE_EMOJIS
			: 0; // TYPE_TEXT
	}
	if (const auto invoice = media->invoice()) {
		if (invoice->isPaidMedia) {
			return 29; // TYPE_PAID_MEDIA
		}
		if (Data::HasUnpaidMedia(*invoice)) {
			return 20; // TYPE_EXTENDED_MEDIA_PREVIEW
		}
	}
	if (media->giveawayStart()) {
		return 26; // TYPE_GIVEAWAY
	}
	if (media->giveawayResults()) {
		return 28; // TYPE_GIVEAWAY_RESULTS
	}
	if (dynamic_cast<Data::MediaDice*>(media)) {
		return 15; // TYPE_ANIMATED_STICKER
	}
	if (media->photo()) {
		return 1; // TYPE_PHOTO
	}
	if (media->location()) {
		return 4; // TYPE_GEO
	}
	if (media->sharedContact()) {
		return 12; // TYPE_CONTACT
	}
	if (media->poll() || media->todolist()) {
		return 17; // TYPE_POLL
	}
	if (media->storyId().valid()) {
		if (media->storyMention()) {
			return 24; // TYPE_STORY_MENTION
		}
		return 23; // TYPE_STORY
	}
	if (const auto document = media->document()) {
		if (document->round()) {
			return 5; // TYPE_ROUND_VIDEO
		}
		if (document->isVideoFile()) {
			return 3; // TYPE_VIDEO
		}
		if (document->isVoiceMessage()) {
			return 2; // TYPE_VOICE
		}
		if (document->isAudioFile()) {
			return 14; // TYPE_MUSIC
		}
		if (document->isAnimation()) {
			return 8; // TYPE_GIF
		}
		if (document->sticker()) {
			if (document->isAnimation()) {
				return 15; // TYPE_ANIMATED_STICKER
			}
			return 13; // TYPE_STICKER
		}
		return 9; // TYPE_FILE
	}
	// 游戏、账单、网页预览等其余媒体按文本处理。
	return 0; // TYPE_TEXT
}

// 未列出的服务消息媒体返回空，交给服务消息组件继续判断。
[[nodiscard]] std::optional<int> serviceMediaType(
		not_null<const HistoryItem*> item,
		not_null<Data::Media*> media) {
	if (media->call()) {
		return 16; // TYPE_PHONE_CALL
	}
	if (media->photo()) {
		return item->isUserpicSuggestion()
			? 21 // TYPE_SUGGEST_PHOTO
			: 11; // TYPE_ACTION_PHOTO
	}
	if (media->paper()) {
		return 22; // TYPE_ACTION_WALLPAPER
	}
	const auto gift = media->gift();
	if (!gift) {
		return std::nullopt;
	}
	if (gift->type == Data::GiftType::Premium) {
		return gift->channel
			? 25 // TYPE_GIFT_PREMIUM_CHANNEL
			: 18; // TYPE_GIFT_PREMIUM
	}
	if (gift->type == Data::GiftType::Credits
		|| gift->type == Data::GiftType::StarGift
		|| gift->type == Data::GiftType::Ton) {
		return 30; // TYPE_GIFT_STARS
	}
	if (gift->type == Data::GiftType::ChatTheme) {
		return 31; // TYPE_GIFT_THEME_UPDATE
	}
	if (gift->type == Data::GiftType::BirthdaySuggest) {
		return 32; // TYPE_SUGGEST_BIRTHDAY
	}
	if (gift->type == Data::GiftType::GiftOffer) {
		return 33; // TYPE_GIFT_OFFER
	}
	return std::nullopt;
}

[[nodiscard]] int serviceMessageType(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	if (const auto type = media ? serviceMediaType(item, media) : std::nullopt) {
		return *type;
	}
	if (item->Get<HistoryServiceGiveawayResults>()) {
		return 28; // TYPE_GIVEAWAY_RESULTS
	}
	if (item->Get<HistoryServiceNoForwardsRequest>()) {
		return 35; // TYPE_SHARING_OFFER
	}
	if (item->Get<HistoryServiceCommunityAdded>()) {
		return 37; // TYPE_COMMUNITY_CHANGED
	}
	if (const auto finish = item->Get<HistoryServiceSuggestFinish>();
		finish
		&& finish->refundType != SuggestRefundType::None
		&& !finish->price.empty()) {
		return 34; // TYPE_GIFT_OFFER_REJECTED
	}
	return 10; // TYPE_DATE
}

} // namespace

int typeOfMessage(const HistoryItem *item) {
	if (item->isSponsored()) {
		return 0; // TYPE_TEXT
	}

	if (item->showSimilarChannels()) {
		return 27; // TYPE_JOINED_CHANNEL
	}

	return item->isService()
		? serviceMessageType(item)
		: regularMessageType(item);
}

QString extractSingle(const not_null<HistoryItem*> item) {
	const auto original = item->originalText();
	QString text(original.text);
	for (const auto &entity : original.entities) {
		if (entity.type() == EntityType::Url) {
			text.append("\n");
			text.append(original.text.mid(entity.offset(), entity.length()));
		} else if (entity.type() == EntityType::CustomUrl) {
			text.append("\n");
			text.append(entity.data());
		}
	}
	return text;
}

QString FilterUtils::extractAllText(const not_null<HistoryItem*> item, const Data::Group *group) {
	auto text = QString();
	if (group) {
		for (const auto &groupItem : group->items) {
			const auto res = extractSingle(groupItem).trimmed();
			if (!res.isEmpty()) {
				text.append(res).append("\n");
			}
		}
	} else {
		text = extractSingle(item).trimmed();
	}

	const auto markup = item->Get<HistoryMessageReplyMarkup>();
	if (markup && !markup->data.isNull()) {
		for (const auto &row : markup->data.rows) {
			for (const auto &button : row) {
				text.append("<button>").append(button.text).append(" ").append(qs(button.data)).append("</button>");
				text.append("\n");
			}
		}
	}

	text.append("\n").append("<type>").append(QString::number(typeOfMessage(item))).append("</type>");

	return text;
}

void FilterUtils::handleResponse(const QByteArray &response) {
	try {
		importFromJson(response);
	} catch (...) {
		LOG(("FilterUtils: Failed to apply response"));
		Ui::Toast::Show(tr::ayu_FiltersToastFailImport(tr::now));
	}
}

void FilterUtils::gotFailure(const QNetworkReply::NetworkError &error) {
	LOG(("FilterUtils: Error %1").arg(error));
	Ui::Toast::Show(tr::ayu_FiltersToastFailFetch(tr::now));
}

namespace {

// 在所有已登录账号里查找已加载的会话，找不到返回空。
[[nodiscard]] PeerData *findLoadedPeer(ID dialogId) {
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		const auto session = account->maybeSession();
		if (!session) {
			continue;
		}
		if (const auto peer = LoadedPeerFromDialogId(&session->data(), dialogId)) {
			return peer;
		}
	}
	return nullptr;
}

} // namespace

ApplyChanges FilterUtils::prepareChanges(const QJsonObject &root) {
	const auto version = root.value("version");

	if (version.toInt() > BACKUP_VERSION) {
		return {};
	}


	const auto existingFilters = AyuDatabase::getAllRegexFilters();
	const auto existingExclusions = AyuDatabase::getAllFiltersExclusions();

	std::vector<RegexFilter> filtersOverrides;
	std::map<std::vector<char>, RegexFilter> newFilters;
	std::vector<RegexFilterGlobalExclusion> newExclusions;
	std::vector<std::vector<char>> removeFiltersById;
	std::vector<RegexFilterGlobalExclusion> removeExclusions;
	std::vector<QString> peersToBeResolved;


	for (const auto &filterRef : root.value("filters").toArray()) {
		const auto filter = filterRef.toObject();
		if (filter.isEmpty()) {
			continue;
		}
		RegexFilter regex;
		regex.caseInsensitive = filter.value("caseInsensitive").toBool();

		const auto dialogIdValue = filter.value("dialogId");
		if (!dialogIdValue.isNull()) {
			regex.dialogId = filter.value("dialogId").toVariant().toLongLong();
		} else {
			regex.dialogId = std::nullopt;
		}
		regex.enabled = filter.value("enabled").toBool();

		regex.id = ParseFilterId(filter.value("id").toString());
		if (regex.id.empty()) {
			continue;
		}

		regex.reversed = filter.value("reversed").toBool();
		regex.text = filter.value("text").toString().toStdString();

		const auto it = std::ranges::find_if(
			existingFilters,
			[&regex](const RegexFilter &f) { return f.id == regex.id; });
		if (it == existingFilters.end()) {
			newFilters[regex.id] = std::move(regex);
		} else if (*it != regex) {
			filtersOverrides.push_back(std::move(regex));
		}
	}

	for (const auto &exclusionRef : root.value("exclusions").toArray()) {
		const auto exclusion = exclusionRef.toObject();
		if (exclusion.isEmpty()) {
			continue;
		}
		RegexFilterGlobalExclusion regex;
		regex.dialogId = exclusion.value("dialogId").toVariant().toLongLong();
		regex.filterId = ParseFilterId(exclusion.value("filterId").toString());
		if (regex.filterId.empty()) {
			continue;
		}
		const auto exists = std::ranges::any_of(
			existingExclusions,
			[&regex](const RegexFilterGlobalExclusion &f) {
				return f.dialogId == regex.dialogId && f.filterId == regex.filterId;
			});
		if (!exists) {
			newExclusions.push_back(std::move(regex));
		}
	}

	for (const auto &filterRef : root.value("removeFiltersById").toArray()) {
		const auto filterId = ParseFilterId(filterRef.toString());
		if (filterId.empty()) {
			continue;
		}
		const auto exists = std::ranges::any_of(
			existingFilters,
			[&](const RegexFilter &f) { return f.id == filterId; });
		if (exists) {
			removeFiltersById.push_back(filterId);
		}
	}

	for (const auto &exclusionRef : root.value("removeExclusions").toArray()) {
		const auto exclusionObj = exclusionRef.toObject();
		const qint64 dialogId = exclusionObj.value("dialogId").toVariant().toLongLong();
		const auto filterId = ParseFilterId(exclusionObj.value("filterId").toString());
		if (filterId.empty()) {
			continue;
		}
		const auto exists = std::ranges::any_of(
			existingExclusions,
			[&](const RegexFilterGlobalExclusion &x) {
				return x.filterId == filterId && x.dialogId == dialogId;
			});
		if (!exists) {
			continue;
		}
		RegexFilterGlobalExclusion regex;
		regex.dialogId = dialogId;
		regex.filterId = filterId;
		removeExclusions.push_back(regex);
	}

	const auto peersJson = root.value("peers").toObject();
	for (const auto &dialogIdStr : peersJson.keys()) {
		auto parsed = false;
		const auto dialogId = dialogIdStr.toLongLong(&parsed);
		if (!parsed || !findLoadedPeer(dialogId)) {
			peersToBeResolved.push_back(peersJson.value(dialogIdStr).toString());
		}
	}

	ApplyChanges changes;
	for (auto &filter : newFilters) {
		changes.newFilters.push_back(std::move(filter.second));
	}
	changes.removeFiltersById = std::move(removeFiltersById);
	changes.filtersOverrides = std::move(filtersOverrides);
	changes.newExclusions = std::move(newExclusions);
	changes.removeExclusions = std::move(removeExclusions);
	changes.peersToBeResolved = std::move(peersToBeResolved);

	return changes;
}

void FilterUtils::applyChanges(const ApplyChanges &changes) {
	for (const auto &filter : changes.newFilters) {
		AyuDatabase::addRegexFilter(filter);
	}
	for (const auto &id : changes.removeFiltersById) {
		AyuDatabase::deleteExclusionsByFilterId(id);
		AyuDatabase::deleteFilter(id);
	}
	for (const auto &filter : changes.filtersOverrides) {
		AyuDatabase::updateRegexFilter(filter);
	}
	for (const auto &exclusion : changes.newExclusions) {
		AyuDatabase::addRegexExclusion(exclusion);
	}
	for (const auto &exclusion : changes.removeExclusions) {
		AyuDatabase::deleteExclusion(exclusion.dialogId, exclusion.filterId);
	}
	if (!changes.peersToBeResolved.empty()) {
		ResolveFilterBackupPeers(changes.peersToBeResolved);
	}

	FiltersCacheController::rebuildCache();
	crl::on_main([]
	{
		FiltersCacheController::fireUpdate();
	});
}
