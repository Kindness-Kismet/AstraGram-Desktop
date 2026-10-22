#pragma once

#include "base/timer.h"
#include "storage/cache/storage_cache_types.h"
#include <map>

class History;
enum class NewMessageType;

namespace Main {
class Session;
} // namespace Main

namespace Data {
class Folder;
} // namespace Data

namespace Storage::Cache {
class Database;
} // namespace Storage::Cache

#ifdef _DEBUG
namespace ExtrasDebug::Commands {
class SimulationCacheCheck;
} // namespace ExtrasDebug::Commands
#endif

namespace Extras {

struct HistoryCacheKey {
	PeerId peer;
	MsgId topicRootId = 0;
	PeerId sublistPeer = 0;

	friend inline auto operator<=>(
		HistoryCacheKey,
		HistoryCacheKey) = default;
};

struct HistoryCacheQuery {
	HistoryCacheKey key;
	MsgId offsetId = 0;
	int addOffset = 0;
	int limit = 0;
	bool allowNearest = false;
	bool latest = false;
	MsgId deleteTill = ServerMaxMsgId;
};

struct CachedHistorySlice {
	MTPmessages_Messages messages;
	bool atTop = false;
	bool atBottom = false;
};

class ConversationCache final {
public:
	explicit ConversationCache(not_null<Main::Session*> session);
	~ConversationCache();

	void restore();
	void flush();
	void clear();
	void disable();
	[[nodiscard]] bool hasDialogsSnapshot() const;
	void dialogReceived(const MTPDdialog &dialog);
	void dialogsDone(Data::Folder *folder);
	void messageReceived(const MTPMessage &message, NewMessageType type);
	void messageEdited(const MTPMessage &message);
	void cacheConfirmedMessage(FullMsgId id);
	void reactionsUpdated(FullMsgId id, const MTPMessageReactions &reactions);
	void pollUpdated(const MTPDupdateMessagePoll &update);
	void webPageUpdated(const MTPWebPage &page);
	void messagesDeleted(PeerId peer, const QVector<MTPint> &ids);
	void historyCleared(PeerId peer);
	void restoreMessage(PeerId peer, MsgId id, Fn<void(bool)> done);

	void getHistory(
		const HistoryCacheQuery &query,
		Fn<void(std::optional<CachedHistorySlice>)> done);
	void putHistory(
		const HistoryCacheQuery &query,
		const MTPmessages_Messages &messages,
		bool refreshVisible);

	struct Stats {
		int restoredDialogs = 0;
		int histories = 0;
		int loadedMessages = 0;
		int hits = 0;
		int misses = 0;
		int messageDataHits = 0;
		bool available = false;
	};
	[[nodiscard]] Stats stats() const;
	struct StorageStats {
		int64 bytes = 0;
		int count = 0;
		std::map<PeerId, std::pair<int64, int>> chats;
		Storage::Cache::Error error;
	};
	void storageStats(Fn<void(StorageStats)> done);
	void clearStorage(PeerId peer, Fn<void(Storage::Cache::Error)> done);

#ifdef _DEBUG
	struct DebugRange {
		MsgId from;
		MsgId till;
		int messages = 0;
	};
	struct DebugHistoryInfo {
		HistoryCacheKey key;
		std::vector<DebugRange> ranges;
		MsgId firstId;
		MsgId lastId;
		MsgId dialogTop;
		MsgId currentTop;
		MsgId previewId;
		int cachedMessages = 0;
		int total = 0;
		bool loaded = false;
		bool dirty = false;
		bool bottomFresh = false;
	};
	struct DebugHistoryEvent {
		uint64 sequence = 0;
		const char *stage = "";
		HistoryCacheQuery query;
		DebugHistoryInfo info;
		MsgId returnedFirst;
		MsgId returnedLast;
		int returnedMessages = 0;
		int deleted = 0;
		bool atTop = false;
		bool atBottom = false;
	};
	struct DebugHistoryState {
		DebugHistoryInfo info;
		PeerId tracedPeer;
		std::vector<DebugHistoryEvent> events;
	};
	void setDebugHistoryPeer(PeerId peer);
	void inspectHistory(HistoryCacheKey key, Fn<void(DebugHistoryState)> done);
	void traceHistory(
		const char *stage,
		const HistoryCacheQuery &query,
		const MTPmessages_Messages *response = nullptr,
		const CachedHistorySlice *cached = nullptr,
		int deleted = 0);
#endif

private:
#ifdef _DEBUG
	friend class ExtrasDebug::Commands::SimulationCacheCheck;
	[[nodiscard]] DebugHistoryInfo debugHistoryInfo(HistoryCacheKey key) const;
	PeerId _debugHistoryPeer;
	uint64 _debugHistorySequence = 0;
	std::vector<DebugHistoryEvent> _debugHistoryEvents;
#endif
	struct Range {
		MsgId from = 0;
		MsgId till = 0;
	};
	struct HistoryEntry {
		struct Pending {
			Fn<void(HistoryEntry&)> done;
			Fn<void()> cancelled;
		};
		base::flat_map<MsgId, MTPMessage> messages;
		base::flat_set<PeerId> peers;
		QVector<MTPForumTopic> topics;
		std::vector<Range> ranges;
		std::vector<Pending> pending;
		uint64 generation = 0;
		int count = 0;
		bool loaded = false;
		bool dirty = false;
		bool bottomFresh = false;
	};

	void scheduleSave();
	void preloadUserpic(History *history);
	void saveDialogs();
	void restoreListMetadata();
	void saveIndex();
	void saveHistories();
	void restoreHistoryMetadata(HistoryCacheKey key, const HistoryEntry &entry);
	void loadHistory(
		HistoryCacheKey key,
		Fn<void(HistoryEntry&)> done,
		Fn<void()> cancelled = nullptr);
	void withIndex(Fn<void()> done);
	void applyMessage(const MTPMessage &message, bool isNew);
	void updateCachedMessages(
		PeerId peer,
		Fn<bool(MTPMessage &)> update);
	void deleteMessages(HistoryCacheKey key, const QVector<MTPint> &ids);
	void trimHistory(HistoryCacheKey key, HistoryEntry &entry);
	void restoreMessageFromHistories(
		std::vector<HistoryCacheKey> keys,
		MsgId id,
		Fn<void(bool)> done);
	base::flat_set<PeerId> restorePeers(const QByteArray &bytes);
	[[nodiscard]] QByteArray serializePeers(
		const base::flat_set<PeerId> &peers) const;
	[[nodiscard]] std::optional<CachedHistorySlice> slice(
		const HistoryCacheQuery &query,
		HistoryEntry &entry);
	[[nodiscard]] std::optional<CachedHistorySlice> latestPreview(
		const HistoryCacheQuery &query,
		const HistoryEntry &entry);
	[[nodiscard]] Storage::Cache::Key storageKey(HistoryCacheKey key) const;
	[[nodiscard]] bool cacheable(const MTPMessage &message) const;

	const not_null<Main::Session*> _session;
	const std::unique_ptr<Storage::Cache::Database> _database;
	base::flat_map<PeerId, MTPDialog> _dialogs;
	base::flat_map<PeerId, MTPMessage> _lastMessages;
	base::flat_map<PeerId, MsgId> _previewMessages;
	base::flat_set<FullMsgId> _pendingConfirmedMessages;
	base::flat_map<PeerId, FolderId> _restoredDialogs;
	std::map<HistoryCacheKey, HistoryEntry> _histories;
	base::flat_set<HistoryCacheKey> _historyKeys;
	base::flat_map<MsgId, PeerId> _nonChannelPeers;
	std::vector<Fn<void()>> _afterIndex;
	base::Timer _saveTimer;
	int _restoredCount = 0;
	int _hits = 0;
	int _misses = 0;
	int _messageDataHits = 0;
	bool _enabled = true;
	bool _ready = false;
	bool _restoring = false;
	bool _hasDialogsSnapshot = false;
	bool _indexReady = false;
	bool _indexDirty = false;
	bool _storageClearing = false;
	uint64 _historyGeneration = 0;
	rpl::lifetime _lifetime;
};

} // namespace Extras
