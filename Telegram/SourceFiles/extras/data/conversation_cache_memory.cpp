#include "extras/data/conversation_cache.h"

#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

namespace Extras {
namespace {

constexpr auto kMaxResidentHistories = 12;
constexpr auto kMaxResidentMessages = 20000;
constexpr auto kMaxResidentMessageBytes = int64(16 * 1024 * 1024);
constexpr auto kIdleTimeout = crl::time(120000);
constexpr auto kPruneInterval = crl::time(15000);

} // namespace

int64 ConversationCache::historyMessageBytes(const HistoryEntry &entry) {
	auto result = int64();
	for (const auto &[id, message] : entry.messages) {
		result += tl::count_length(message);
	}
	return result;
}

ConversationCache::MemoryStats ConversationCache::memoryStats() const {
	auto result = MemoryStats{
		.histories = int(_histories.size()),
		.evictions = _memoryEvictions,
		.diskLoads = _historyDiskLoads,
		.maxHistories = kMaxResidentHistories,
		.maxMessages = kMaxResidentMessages,
		.maxMessageBytes = kMaxResidentMessageBytes,
		.idleTimeout = kIdleTimeout,
	};
	for (const auto &[key, entry] : _histories) {
		result.messages += entry.messages.size();
		result.messageBytes += historyMessageBytes(entry);
	}
	return result;
}

void ConversationCache::touchHistory(HistoryEntry &entry) {
	entry.lastUsed = crl::now();
	if (!_memoryTimer.isActive()) {
		_memoryTimer.callOnce(kPruneInterval);
	}
}

void ConversationCache::pruneMemory() {
	_memoryTimer.cancel();
	if (!_enabled || !_ready || _histories.empty()) {
		return;
	}
	_memoryTimer.callOnce(kPruneInterval);
	if (_restoring || _storageClearing) {
		return;
	}
	auto protectedKeys = base::flat_set<HistoryCacheKey>();
	for (const auto window : _session->windows()) {
		const auto active = window->activeChatCurrent();
		const auto history = active.owningHistory();
		if (!history) {
			continue;
		}
		const auto peer = history->peer->id;
		protectedKeys.emplace(HistoryCacheKey{ peer });
		if (const auto topic = active.topic()) {
			protectedKeys.emplace(HistoryCacheKey{ peer, topic->rootId() });
		}
		if (const auto sublist = active.sublist()) {
			protectedKeys.emplace(HistoryCacheKey{ peer, 0, sublist->sublistPeer()->id });
		}
	}
	if (_preloading) {
		protectedKeys.emplace(_preloading->key);
	}
	struct Candidate {
		HistoryCacheKey key;
		crl::time lastUsed;
	};
	auto candidates = std::vector<Candidate>();
	for (const auto &[key, entry] : _histories) {
		if (!entry.loaded || entry.dirty || entry.pendingWrites
			|| !entry.pending.empty() || protectedKeys.contains(key)) {
			continue;
		}
		candidates.push_back({ key, entry.lastUsed });
	}
	if (candidates.empty()) {
		return;
	}
	ranges::sort(candidates, std::less<>(), &Candidate::lastUsed);
	auto usage = memoryStats();
	const auto now = crl::now();
	for (const auto &candidate : candidates) {
		if (usage.histories <= kMaxResidentHistories
			&& usage.messages <= kMaxResidentMessages
			&& usage.messageBytes <= kMaxResidentMessageBytes
			&& now - candidate.lastUsed < kIdleTimeout) {
			break;
		}
		const auto i = _histories.find(candidate.key);
		--usage.histories;
		usage.messages -= i->second.messages.size();
		usage.messageBytes -= historyMessageBytes(i->second);
		// 只释放已保存的内存副本，磁盘索引和消息编号映射继续保留。
		_histories.erase(i);
		++_memoryEvictions;
	}
	if (_histories.empty()) {
		_memoryTimer.cancel();
	}
}

} // namespace Extras
