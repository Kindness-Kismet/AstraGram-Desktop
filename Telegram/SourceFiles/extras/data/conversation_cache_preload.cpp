#include "extras/data/conversation_cache.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#ifdef _DEBUG
#include "extras/debug/debug_login.h"
#include "settings.h"
#endif

namespace Extras {
namespace {

constexpr auto kVisibleHistories = 10;
constexpr auto kNextDelay = crl::time(250);
constexpr auto kForegroundDelay = crl::time(1500);
constexpr auto kPreloadLifetime = crl::time(60000);

MsgId topMessageId(not_null<History*> history) {
	const auto last = history->lastServerMessage();
	return last ? last->id : MsgId();
}

} // namespace

void ConversationCache::preloadHistories(const std::vector<History*> &histories) {
	auto queries = std::vector<HistoryCacheQuery>();
	for (const auto history : histories) {
		const auto peer = history->peer;
		if (peer->isSecretChat() || peer->isForum() || peer->isMonoforum()) {
			continue;
		}
		const auto around = history->loadAroundId();
		const auto query = HistoryCacheQuery{
			.key = { peer->id },
			.offsetId = around,
			.addOffset = around ? -25 : 0,
			.limit = around ? 50 : 30,
			.latest = !around,
			.deleteTill = topMessageId(history),
		};
		const auto stamp = _preloadedHistories.find(query.key);
		if (stamp != end(_preloadedHistories)
			&& stamp->second.offsetId == around
			&& stamp->second.topId == topMessageId(history)
			&& crl::now() - stamp->second.when < kPreloadLifetime) {
			continue;
		}
		queries.push_back(query);
		if (queries.size() == kVisibleHistories) {
			break;
		}
	}
	if (ranges::equal(queries, _preloadQueries, [](const auto &a, const auto &b) {
		return a.key == b.key && a.offsetId == b.offsetId
			&& a.deleteTill == b.deleteTill;
	})) {
		return;
	}
	_preloadQueries = std::move(queries);
	if (_preloading && ranges::none_of(_preloadQueries, [&](const auto &query) {
		return query.key == _preloading->key
			&& query.offsetId == _preloading->offsetId
			&& query.deleteTill == _preloading->deleteTill;
	})) {
		cancelPreload();
	}
	_preloadTimer.callOnce(std::max(kNextDelay, _preloadAfter - crl::now()));
}

void ConversationCache::cancelPreload() {
	++_preloadGeneration;
	if (_preloadRequest) {
		_session->data().histories().cancelRequest(base::take(_preloadRequest));
		++_preloadCancellations;
	}
	_preloading.reset();
}

void ConversationCache::foregroundHistoryRequested(HistoryCacheKey key) {
	cancelPreload();
	// 前台历史请求先处理，后台预取不参与首屏等待。
	_preloadAfter = crl::now() + kForegroundDelay;
	if (const auto history = _session->data().historyLoaded(key.peer)) {
		_preloadedHistories[key] = {
			history->loadAroundId(), topMessageId(history), crl::now(), false };
	}
	_preloadQueries.erase(ranges::remove_if(_preloadQueries, [&](const auto &query) {
		return query.key == key;
	}), end(_preloadQueries));
	_preloadTimer.callOnce(kForegroundDelay);
}

void ConversationCache::finishPreload(const HistoryCacheQuery &query, bool ready) {
	_preloadRequest = 0;
	_preloading.reset();
	if (ready) {
		_preloadedHistories[query.key] = {
			query.offsetId, query.deleteTill, crl::now() };
	}
	_preloadQueries.erase(ranges::remove_if(_preloadQueries, [&](const auto &other) {
		return other.key == query.key;
	}), end(_preloadQueries));
	_preloadTimer.callOnce(std::max(kNextDelay, _preloadAfter - crl::now()));
}

void ConversationCache::preloadNext() {
#ifdef _DEBUG
	if (ExtrasDebug::isSimulationSession(_session.get())) {
		return;
	}
#endif
	if (!_enabled || _storageClearing || _preloading || _preloadReading
		|| _preloadQueries.empty()) {
		return;
	}
	if (crl::now() < _preloadAfter) {
		_preloadTimer.callOnce(_preloadAfter - crl::now());
		return;
	}
	if (_session->data().histories().hasHistoryRequests()
		|| _session->api().historyRequestsPending()) {
		_preloadTimer.callOnce(kForegroundDelay);
		return;
	}
	const auto query = _preloadQueries.front();
	_preloading = query;
	_preloadReading = true;
	const auto generation = ++_preloadGeneration;
	getHistory(query, [=](std::optional<CachedHistorySlice> cached) {
		_preloadReading = false;
		if (generation != _preloadGeneration) {
			_preloadTimer.callOnce(std::max(kNextDelay, _preloadAfter - crl::now()));
			return;
		}
		auto count = 0;
		if (cached) {
			cached->messages.match([](const MTPDmessages_messagesNotModified &) {
			}, [&](const auto &data) { count = data.vmessages().v.size(); });
		}
		if (cached && (count >= query.limit || (cached->atTop && cached->atBottom))) {
			++_preloadCacheHits;
			finishPreload(query, true);
			return;
		}
#ifdef _DEBUG
		if (cDebugOffline()) {
			finishPreload(query, false);
			return;
		}
#endif
		if (_session->api().instance().dcstate() != 2) {
			_preloadAfter = crl::now() + kForegroundDelay;
			_preloading.reset();
			_preloadTimer.callOnce(kForegroundDelay);
			return;
		}
		auto networkQuery = query;
		networkQuery.deleteTill = topMessageId(_session->data().history(query.key.peer));
		if (cached) {
			cached->messages.match([](const MTPDmessages_messagesNotModified &) {
			}, [&](const auto &data) {
				for (const auto &message : data.vmessages().v) {
					accumulate_max(networkQuery.deleteTill, IdFromMessage(message));
				}
			});
		}
		// 后台只有一个网络请求，每个小页完成后至少间隔一秒。
		_preloadAfter = crl::now() + 1000;
		++_preloadNetworkRequests;
#ifdef _DEBUG
		traceHistory("preload-network-sent", networkQuery);
#endif
		_preloadRequest = _session->data().histories().sendRequest(
			_session->data().history(query.key.peer), Data::Histories::RequestType::History,
			[=](Fn<void()> finish) {
		return _session->api().request(MTPmessages_GetHistory(
			_session->data().peer(query.key.peer)->input(),
			MTP_int(query.offsetId), MTP_int(0), MTP_int(query.addOffset),
			MTP_int(query.limit), MTP_int(0), MTP_int(0), MTP_long(0)
		)).done([=](const MTPmessages_Messages &result) {
			finish();
			if (generation != _preloadGeneration) {
				return;
			}
#ifdef _DEBUG
			traceHistory("preload-network-result", networkQuery, &result);
#endif
			putHistory(networkQuery, result, false);
			_preloadAfter = std::max(_preloadAfter, crl::now() + 1000);
			finishPreload(networkQuery, true);
		}).fail([=](const MTP::Error &) {
			finish();
			if (generation != _preloadGeneration) {
				return;
			}
			_preloadAfter = crl::now() + kPreloadLifetime;
			finishPreload(query, false);
		}).handleFloodErrors().send();
		});
	});
}

ConversationCache::PreloadStats ConversationCache::preloadStats() const {
	auto result = PreloadStats{
		.queued = int(_preloadQueries.size()),
		.cacheHits = _preloadCacheHits,
		.networkRequests = _preloadNetworkRequests,
		.cancellations = _preloadCancellations,
		.active = _preloading ? _preloading->key.peer : PeerId(),
	};
	for (const auto &[key, stamp] : _preloadedHistories) {
		if (stamp.ready) {
			result.ready.push_back(key.peer);
		}
	}
	return result;
}

} // namespace Extras
