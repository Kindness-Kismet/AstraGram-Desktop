#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/data/media_cache_manager.h"
#include "extras/data/conversation_cache.h"
#include "extras/data/conversation_cache_serialization.h"
#include "extras/data/sticker_set_cache.h"
#include "extras/utils/rc_manager.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "chat_helpers/stickers_dice_pack.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_media_types.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "history/history.h"
#include "main/main_session.h"
#include "ui/emoji_config.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QFileInfo>

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;
using Category = Extras::MediaCacheCategory;
using ChatType = Extras::MediaCacheChatType;

constexpr auto kCategoryNames = std::array{
	"images", "stickers", "documents", "avatars", "other", "messages",
};
constexpr auto kChatTypes = std::array{
	"private", "groups", "channels", "stories",
};

json historyInfo(const Extras::ConversationCache::DebugHistoryInfo &info) {
	auto ranges = json::array();
	for (const auto &range : info.ranges) {
		ranges.push_back({ { "from", range.from.bare },
			{ "till", range.till.bare }, { "messages", range.messages } });
	}
	return {
		{ "peerId", info.key.peer.value }, { "topicRootId", info.key.topicRootId.bare },
		{ "sublistPeer", info.key.sublistPeer.value }, { "ranges", std::move(ranges) },
		{ "firstId", info.firstId.bare }, { "lastId", info.lastId.bare },
		{ "dialogTop", info.dialogTop.bare }, { "currentTop", info.currentTop.bare },
		{ "previewId", info.previewId.bare }, { "cachedMessages", info.cachedMessages },
		{ "total", info.total }, { "loaded", info.loaded },
		{ "dirty", info.dirty }, { "bottomFresh", info.bottomFresh },
	};
}

Result traceHistory(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: cache.trace-history <peerId|off>"_q);
	}
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session"_q);
	}
	const auto disabled = args.front() == u"off"_q;
	const auto peer = disabled ? nullptr : findPeer(args.front());
	if (!disabled && !peer) {
		return Result::Err(u"unknown peer"_q);
	}
	const auto peerId = peer ? peer->id : PeerId();
	session->conversationCache().setDebugHistoryPeer(peerId);
	return Result::Ok(Compact(json{ { "peerId", peerId.value },
		{ "enabled", !disabled }, { "capacity", 120 } }));
}

Result inspectHistory(const QStringList &args) {
	if (args.size() != 1 && args.size() != 3) {
		return Result::Err(u"usage: cache.history-info <peerId> [topicRootId sublistPeer]"_q);
	}
	const auto session = ActiveSession();
	const auto peer = session ? findPeer(args.front()) : nullptr;
	if (!peer) {
		return Result::Err(u"unknown peer"_q);
	}
	auto key = Extras::HistoryCacheKey{ peer->id };
	if (args.size() == 3) {
		auto topicOk = false;
		auto sublistOk = false;
		key.topicRootId = MsgId(args[1].toLongLong(&topicOk));
		key.sublistPeer = PeerId(args[2].toULongLong(&sublistOk));
		if (!topicOk || key.topicRootId < 0 || !sublistOk
			|| (key.topicRootId && key.sublistPeer)) {
			return Result::Err(u"invalid topic or sublist id"_q);
		}
	}
	const auto job = beginJob("cache-history-info");
	session->lifetime().add([job] { finishJob(job, false, "session closed"); });
	session->conversationCache().inspectHistory(key,
		[job](Extras::ConversationCache::DebugHistoryState state) {
			auto events = json::array();
			for (const auto &event : state.events) {
				const auto &query = event.query;
				events.push_back({
					{ "sequence", event.sequence }, { "stage", event.stage },
					{ "offsetId", query.offsetId.bare }, { "addOffset", query.addOffset },
					{ "limit", query.limit }, { "latest", query.latest },
					{ "allowNearest", query.allowNearest }, { "deleteTill", query.deleteTill.bare },
					{ "info", historyInfo(event.info) },
					{ "returnedFirst", event.returnedFirst.bare },
					{ "returnedLast", event.returnedLast.bare },
					{ "returnedMessages", event.returnedMessages }, { "deleted", event.deleted },
					{ "atTop", event.atTop }, { "atBottom", event.atBottom },
				});
			}
			finishJob(job, true, { { "info", historyInfo(state.info) },
				{ "tracedPeer", state.tracedPeer.value }, { "events", std::move(events) } });
		});
	return jobStarted(job);
}

Result specialStickers(const QStringList &args) {
	const auto session = ActiveSession();
	if (!session || !args.empty()) {
		return Result::Err(u"usage: cache.special-stickers (requires a session)"_q);
	}
	auto result = json::array();
	const auto describe = [&](const QString &name, DocumentData *document) {
		const auto cached = Extras::cachedStickerSet(session, name);
		result.push_back({ { "name", name.toStdString() }, { "cached", cached.has_value() },
			{ "documents", cached ? cached->c_messages_stickerSet().vdocuments().v.size() : 0 },
			{ "mappedDocument", document ? document->id : uint64(0) } });
	};
	for (const auto emoji : { "\xf0\x9f\x8e\xb2", "\xf0\x9f\x8e\xaf", "\xf0\x9f\x8f\x80",
			"\xe2\x9a\xbd", "\xf0\x9f\x8e\xb3", "\xf0\x9f\x8e\xb0" }) {
		const auto text = QString::fromUtf8(emoji);
		describe(u"dice-"_q + text, session->diceStickersPacks().lookup(text, 1));
	}
	auto &gifts = session->giftBoxStickersPacks();
	gifts.load();
	gifts.tonLoad();
	describe(u"premium-gifts"_q, gifts.lookup(3));
	describe(u"ton-gifts"_q, gifts.tonLookup(10));
	return Result::Ok(Compact(json{ { "userId", session->userId().bare }, { "sets", result } }));
}

Result inspectMedia(const QStringList &args) {
	if (args.size() != 2) {
		return Result::Err(u"usage: cache.media-info <peerId> <messageId>"_q);
	}
	const auto item = findMessage(args[0], args[1]);
	if (!item) {
		return Result::Err(u"message not found"_q);
	}
	const auto session = not_null(&item->history()->session());
	const auto &text = item->originalText();
	const auto custom = ranges::find(text.entities,
		EntityType::CustomEmoji, &EntityInText::type);
	const auto customId = custom != text.entities.end()
		? Data::ParseCustomEmojiData(custom->data()) : DocumentId();
	const auto emoji = (item->media() || customId)
		? nullptr
		: Ui::Emoji::Find(text.text);
	const auto emojiSticker = emoji
		? session->emojiStickersPack().stickerForEmoji(emoji).document
		: nullptr;
	const auto dice = dynamic_cast<Data::MediaDice*>(item->media());
	const auto document = dice
		? session->diceStickersPacks().lookup(dice->emoji(), dice->value())
		: item->media() ? item->media()->document()
		: customId ? session->data().document(customId).get() : emojiSticker;
	const auto photo = item->media() ? item->media()->photo() : nullptr;
	auto result = json{
		{ "userId", session->userId().bare }, { "peerId", item->history()->peer->id.value },
		{ "messageId", item->id.bare }, { "emojiAnimationMapped", emojiSticker != nullptr },
		{ "customEmojiId", customId },
		{ "animatedSetBytes", Extras::cachedEmojiSetBytes(session, Extras::EmojiSetType::Animated) },
		{ "interactionSetBytes", Extras::cachedEmojiSetBytes(session, Extras::EmojiSetType::Interactions) },
	};
	if (!document && !photo) {
		result["document"] = nullptr;
		return Result::Ok(Compact(result));
	}
	auto keys = std::vector<std::pair<const char*, Storage::Cache::Key>>();
	if (photo) {
		const auto view = photo->activeMediaView();
		result["photo"] = { { "id", photo->id }, { "loaded", view && view->loaded() } };
		for (const auto &[name, size] : std::array{
			std::pair{ "small", Data::PhotoSize::Small },
			std::pair{ "thumbnail", Data::PhotoSize::Thumbnail },
			std::pair{ "large", Data::PhotoSize::Large } }) {
			keys.emplace_back(name, photo->location(size).file().cacheKey());
		}
	}
	if (document) {
		const auto media = document->activeMediaView();
		const auto sticker = document->sticker();
		const auto path = document->filepath(true);
		result["document"] = {
			{ "id", document->id }, { "size", document->size },
			{ "mime", document->mimeString().toStdString() },
			{ "stickerType", sticker ? json(int(sticker->type)) : json(nullptr) },
			{ "setId", sticker ? sticker->set.id : uint64(0) },
			{ "saveToCache", document->saveToCache() }, { "loading", document->loading() },
			{ "loadOffset", document->loadOffset() }, { "failed", document->status == FileDownloadFailed },
			{ "memoryBytes", media ? media->bytes().size() : 0 },
			{ "mediaLoaded", media && media->loaded() },
			{ "thumbnailLoaded", media && media->thumbnail() != nullptr },
			{ "localFileBytes", path.isEmpty() ? qint64(0) : QFileInfo(path).size() },
		};
		keys.emplace_back("file", document->cacheKey());
		keys.emplace_back("thumbnail", document->thumbnailLocation().file().cacheKey());
		keys.emplace_back("goodThumbnail", document->goodThumbnailCacheKey());
	}
	struct Operation {
		json result;
		int remaining = 0;
	};
	const auto operation = std::make_shared<Operation>();
	operation->result = std::move(result);
	operation->remaining = keys.size();
	const auto job = beginJob("cache-media-info");
	session->lifetime().add([job] { finishJob(job, false, "session closed"); });
	for (const auto &[name, key] : keys) {
		const auto done = Extras::CacheSerialization::onMain<QByteArray>(session,
				[job, operation, name](QByteArray bytes) {
			operation->result["disk"][name] = {
				{ "bytes", bytes.size() }, { "partial", bytes.startsWith("partial:") },
				{ "sha256", bytes.isEmpty() ? std::string()
					: QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString() },
			};
			if (--operation->remaining == 0) {
				finishJob(job, true, std::move(operation->result));
			}
		});
		if (key.valid()) {
			session->data().cache().get(key, done);
		} else {
			done({});
		}
	}
	return jobStarted(job);
}

Result badgeState(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: cache.badges"_q);
	}
	const auto &manager = RCManager::getInstance();
	const auto &roster = manager.roster();
	return Result::Ok(Compact(json{
		{ "restoredFromCache", manager.restoredFromCache() },
		{ "refreshing", manager.refreshing() },
		{ "developers", roster.developers.size() },
		{ "officialChannels", roster.officialChannels.size() },
		{ "supporters", roster.supporters.size() },
		{ "supporterChannels", roster.supporterChannels.size() },
		{ "customBadges", roster.customBadges.size() },
	}));
}

json usage(const Extras::MediaCacheUsageList &list) {
	auto result = json::object();
	for (auto i = 0; i != int(list.size()); ++i) {
		result[kCategoryNames[i]] = {
			{ "bytes", list[i].bytes }, { "count", list[i].count },
		};
	}
	return result;
}

Result mediaStats(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: cache.media-stats"_q);
	}
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session"_q);
	}
	const auto job = beginJob("media-cache-stats");
	session->lifetime().add([job] { finishJob(job, false, "session closed"); });
	session->mediaCache().snapshot([=](Extras::MediaCacheSnapshot stats) {
		if (stats.error.type != Storage::Cache::Error::Type::None) {
			finishJob(job, false, "cache statistics failed");
			return;
		}
		auto chats = json::array();
		for (const auto &chat : stats.chats) {
			chats.push_back({
				{ "peerId", chat.peer.value }, { "bytes", chat.bytes },
				{ "categories", usage(chat.categories) },
			});
		}
		finishJob(job, true, {
			{ "categories", usage(stats.categories) },
			{ "unassigned", usage(stats.unassigned) }, { "chats", chats },
			{ "clearing", session->mediaCache().clearing() },
		});
	});
	return jobStarted(job);
}

Result mediaClear(const QStringList &args) {
	if (args.empty() || args.size() > 2) {
		return Result::Err(u"usage: cache.media-clear <all|category[,category...]> [peerId]"_q);
	}
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session"_q);
	}
	auto mask = Extras::MediaCacheMask();
	for (const auto &name : args.front().split(u',')) {
		if (name == u"all") {
			mask |= Extras::kAllMediaCacheMask;
			continue;
		}
		const auto i = ranges::find(kCategoryNames, name.toStdString());
		if (i == kCategoryNames.end()) {
			return Result::Err(u"unknown cache category"_q);
		}
		mask |= Extras::mediaCacheBit(Category(i - kCategoryNames.begin()));
	}
	const auto peer = args.size() == 2 ? findPeer(args[1]) : nullptr;
	if (args.size() == 2 && !peer) {
		return Result::Err(u"unknown peer"_q);
	}
	const auto job = beginJob("media-cache-clear");
	session->lifetime().add([job] { finishJob(job, false, "session closed"); });
	const auto started = session->mediaCache().clear(mask, peer ? peer->id : PeerId(),
		[=](Extras::MediaCacheClearResult result) {
			finishJob(job, result.error.type == Storage::Cache::Error::Type::None,
				{ { "freedBytes", result.freedBytes }, { "error", int(result.error.type) } });
		});
	if (!started) {
		finishJob(job, false, "cache clearing already in progress");
	}
	return jobStarted(job);
}

Result mediaRetention(const QStringList &args) {
	if (args.empty() || args.size() > 2) {
		return Result::Err(u"usage: cache.media-retention <private|groups|channels|stories> [days]"_q);
	}
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session"_q);
	}
	const auto i = ranges::find(kChatTypes, args.front().toStdString());
	if (i == kChatTypes.end()) {
		return Result::Err(u"unknown chat type"_q);
	}
	const auto type = ChatType(i - kChatTypes.begin());
	if (args.size() == 1) {
		return Result::Ok(QString::number(session->mediaCache().retentionDays(type)));
	}
	auto ok = false;
	const auto days = args[1].toInt(&ok);
	if (!ok || !ranges::contains(std::array{ 0, 1, 2, 7, 30 }, days)) {
		return Result::Err(u"days must be 0, 1, 2, 7 or 30"_q);
	}
	session->mediaCache().setRetentionDays(type, days);
	return Result::Ok(QString::number(days));
}

} // namespace

const HandlerMap &cacheHandlers() {
	static const auto result = HandlerMap{
		{ u"cache.trace-history"_q, &traceHistory },
		{ u"cache.history-info"_q, &inspectHistory },
		{ u"cache.media-info"_q, &inspectMedia },
		{ u"cache.special-stickers"_q, &specialStickers },
		{ u"cache.badges"_q, &badgeState },
		{ u"cache.media-stats"_q, &mediaStats },
		{ u"cache.media-clear"_q, &mediaClear },
		{ u"cache.media-retention"_q, &mediaRetention },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
