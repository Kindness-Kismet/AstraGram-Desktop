/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apiwrap.h"

#include "data/data_channel.h"
#include "data/data_histories.h"
#include "data/data_history_messages.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "extras/data/conversation_cache.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

void ApiWrap::requestHistory(
		not_null<History*> history,
		MsgId messageId,
		SliceType slice,
		Fn<void(bool)> done) {
	const auto peer = history->peer;
	const auto key = HistoryRequest{
		peer,
		messageId,
		slice,
	};
	const auto [i, inserted] = _historyRequests.emplace(key);
	if (done) {
		i->second.push_back(std::move(done));
	}
	if (!inserted) {
		return;
	}

	auto query = Extras::HistoryCacheQuery();
	const auto prepared = Api::PrepareHistoryRequest(peer, messageId, slice, &query);
	_session->conversationCache().foregroundHistoryRequested(query.key);
	_session->conversationCache().getHistory(query, [=](
			std::optional<Extras::CachedHistorySlice> cached) {
		const auto fromCache = cached.has_value();
		auto cachedLast = MsgId();
		if (cached) {
			auto parsed = Api::ParseCachedHistoryResult(peer, messageId, slice, query, *cached);
			for (const auto id : parsed.messageIds) {
				accumulate_max(cachedLast, id);
			}
			// 部分缓存的总数可能只是占位，不能据此把现有消息判为完整历史。
			history->messages().addSlice(
				std::move(parsed.messageIds),
				parsed.noSkipRange,
				cached->atTop && cached->atBottom
					? std::optional(parsed.fullCount)
					: std::nullopt);
#ifdef _DEBUG
			_session->conversationCache().traceHistory("viewer-delivered", query,
				nullptr, &*cached);
#endif
		}
		auto &histories = history->owner().histories();
		const auto requestType = Data::Histories::RequestType::History;
		histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
			auto networkQuery = query;
			if (query.latest) {
				const auto last = history->lastServerMessage();
				networkQuery.deleteTill = last
					? std::max(last->id, cachedLast)
					: cachedLast;
			}
#ifdef _DEBUG
			_session->conversationCache().traceHistory("viewer-network-sent", networkQuery);
#endif
			return request(
				std::move(prepared)
			).done([=](const Api::HistoryRequestResult &result) {
#ifdef _DEBUG
				_session->conversationCache().traceHistory("viewer-network-result", networkQuery, &result);
#endif
				const auto callbacks = base::take(_historyRequests[key]);
				_historyRequests.remove(key);
				_session->conversationCache().putHistory(networkQuery, result, fromCache);
				auto parsed = Api::ParseHistoryResult(peer, messageId, slice, result);
				auto fullCount = std::optional(parsed.fullCount);
				if (query.latest) {
					auto responseLast = MsgId();
					parsed.noSkipRange = { ServerMaxMsgId, 0 };
					for (const auto id : parsed.messageIds) {
						accumulate_min(parsed.noSkipRange.from, id);
						accumulate_max(responseLast, id);
					}
					if (parsed.messageIds.size() == parsed.fullCount) {
						parsed.noSkipRange.from = 0;
					}
					const auto last = history->lastServerMessage();
					const auto stale = last && responseLast < last->id;
					parsed.noSkipRange.till = stale ? responseLast : ServerMaxMsgId;
					if (stale) {
						fullCount = std::nullopt;
					}
				}
				history->messages().addSlice(
					std::move(parsed.messageIds),
					parsed.noSkipRange,
					fullCount);
				finish();
				for (const auto &callback : callbacks) {
					callback(true);
				}
			}).fail([=](const MTP::Error &error) {
				const auto callbacks = base::take(_historyRequests[key]);
				_historyRequests.remove(key);
				if (error.type() == u"CHANNEL_PRIVATE"_q
					&& peer->isChannel()
					&& peer->asChannel()->invitePeekExpires()) {
					peer->asChannel()->privateErrorReceived();
				} else if (error.type() == u"CHANNEL_PRIVATE"_q
					|| error.type() == u"CHANNEL_PUBLIC_GROUP_NA"_q
					|| error.type() == u"USER_BANNED_IN_CHANNEL"_q) {
					history->owner().notifyHistoryAccessLost(history);
				}
				finish();
				for (const auto &callback : callbacks) {
					callback(false);
				}
			}).send();
		});
	});
}
