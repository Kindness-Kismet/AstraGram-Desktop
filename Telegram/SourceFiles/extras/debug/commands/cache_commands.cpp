#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/data/media_cache_manager.h"
#include "extras/utils/rc_manager.h"
#include "data/data_peer.h"
#include "main/main_session.h"

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
		{ u"cache.badges"_q, &badgeState },
		{ u"cache.media-stats"_q, &mediaStats },
		{ u"cache.media-clear"_q, &mediaClear },
		{ u"cache.media-retention"_q, &mediaRetention },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
