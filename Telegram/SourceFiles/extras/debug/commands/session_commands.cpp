#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/debug/debug_login.h"
#include "extras/data/conversation_cache.h"
#include "extras/data/conversation_cache_serialization.h"

#include "main/main_session.h"
#include "mainwidget.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "history/history.h"
#include "mtproto/mtp_instance.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"

#include <QtCore/QCryptographicHash>

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

// 与登录页共用初始化入口，场景数据随会话创建，不依赖客户端环境变量。
[[nodiscard]] Result enterSimulationMode(const QStringList &args) {
	if (args.size() > 1) {
		return Result::Err(u"usage: simulation.enter [userId]"_q);
	}
	auto userId = int64(999999999);
	if (args.size() == 1) {
		auto ok = false;
		userId = args.front().toLongLong(&ok);
		if (!ok || userId <= 0) {
			return Result::Err(u"expected a positive integer userId"_q);
		}
	}
	if (const auto error = enterSimulation(userId); !error.isEmpty()) {
		return Result::Err(error);
	}
	return Result::Ok(Compact(json{
		{ "userId", ActiveSession()->userId().bare },
		{ "note", "local simulation mode" },
	}));
}

[[nodiscard]] Result cacheState(const QStringList &args) {
	if (args.size() > 1) {
		return Result::Err(u"usage: cache.state [peerId]"_q);
	}
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session"_q);
	}
	const auto stats = session->conversationCache().stats();
	const auto controller = session->tryResolveWindow();
	const auto active = controller ? controller->activeChatsFilterCurrent() : 0;
	auto &filters = session->data().chatsFilters();
	auto result = json{
		{ "available", stats.available },
		{ "restoredDialogs", stats.restoredDialogs },
		{ "cachedHistories", stats.histories },
		{ "loadedMessages", stats.loadedMessages },
		{ "hits", stats.hits },
		{ "misses", stats.misses },
		{ "messageDataHits", stats.messageDataHits },
		{ "filtersLoaded", filters.loaded() },
		{ "activeFilter", active },
		{ "defaultFilter", filters.defaultId() },
		{ "dialogs", session->data().chatsList()->indexed()->size() },
		{ "activeRows", active ? filters.chatsList(active)->indexed()->size()
			: session->data().chatsList()->indexed()->size() },
		{ "connectionState", session->api().instance().dcstate() },
	};
	if (controller) {
		if (const auto list = controller->content()->findChild<QWidget*>(u"dialogsList"_q)) {
			result["rendered"] = {
				{ "firstFilter", list->property("firstPaintFilter").toLongLong() },
				{ "firstRows", list->property("firstPaintRows").toInt() },
				{ "firstWaiting", list->property("firstPaintWaiting").toBool() },
				{ "currentFilter", list->property("paintedFilter").toLongLong() },
				{ "currentRows", list->property("paintedRows").toInt() },
				{ "hiddenAllPaints", list->property("hiddenAllPaints").toInt() },
			};
		}
	}
	if (!args.empty()) {
		const auto peer = findPeer(args.front());
		if (!peer) {
			return Result::Err(u"unknown peer"_q);
		}
		const auto history = session->data().history(peer);
		result["history"] = {
			{ "empty", history->isEmpty() },
			{ "minId", history->minMsgId().bare },
			{ "maxId", history->maxMsgId().bare },
			{ "atTop", history->loadedAtTop() },
			{ "atBottom", history->loadedAtBottom() },
		};
	}
	return Result::Ok(Compact(result));
}

[[nodiscard]] Result cacheRead(const QStringList &args) {
	if (args.empty() || args.size() > 4) {
		return Result::Err(u"usage: cache.read <peerId> [offsetId] [addOffset] [limit]"_q);
	}
	const auto session = ActiveSession();
	const auto peer = session ? findPeer(args.front()) : nullptr;
	if (!peer) {
		return Result::Err(u"unknown peer"_q);
	}
	auto values = std::array<int, 3>{ 0, 0, 50 };
	for (auto i = 1; i < args.size(); ++i) {
		auto ok = false;
		values[i - 1] = args[i].toInt(&ok);
		if (!ok) {
			return Result::Err(u"expected integer query parameters"_q);
		}
	}
	if (values[0] < 0 || values[2] < 1 || values[2] > 1000) {
		return Result::Err(u"offsetId must be nonnegative and limit must be between 1 and 1000"_q);
	}
	const auto job = beginJob("cache-read");
	session->lifetime().add([job] { finishJob(job, false, "session closed"); });
	session->conversationCache().getHistory({
		.key = { peer->id },
		.offsetId = values[0],
		.addOffset = values[1],
		.limit = values[2],
		.allowNearest = values[0] == 0,
	}, [=](std::optional<Extras::CachedHistorySlice> slice) {
		if (!slice) {
			finishJob(job, true, json{ { "hit", false } });
			return;
		}
		auto ids = json::array();
		slice->messages.match([](const MTPDmessages_messagesNotModified &) {
		}, [&](const auto &data) {
			for (const auto &message : data.vmessages().v) {
				ids.push_back(IdFromMessage(message).bare);
			}
		});
		const auto bytes = Extras::CacheSerialization::encode(slice->messages);
		finishJob(job, true, json{
			{ "hit", true }, { "ids", ids },
			{ "atTop", slice->atTop }, { "atBottom", slice->atBottom },
			{ "digest", QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString() },
		});
	});
	return jobStarted(job);
}

[[nodiscard]] Result cacheFlush(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: cache.flush"_q);
	}
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session"_q);
	}
	session->conversationCache().flush();
	return Result::Ok(u"cache writes scheduled"_q);
}

} // namespace

const HandlerMap &SessionHandlers() {
	static const auto result = HandlerMap{
		{ u"simulation.enter"_q, &enterSimulationMode },
		{ u"cache.state"_q, &cacheState },
		{ u"cache.read"_q, &cacheRead },
		{ u"cache.flush"_q, &cacheFlush },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
