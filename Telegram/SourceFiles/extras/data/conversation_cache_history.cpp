#include "extras/data/conversation_cache.h"

#include "extras/data/conversation_cache_serialization.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_forum.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_history_messages.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/cache/storage_cache_database.h"

namespace Extras {
namespace {

using namespace CacheSerialization;

template <typename Range>
void mergeRange(std::vector<Range> &stored, Range range) {
	stored.push_back(range);
	ranges::sort(stored, {}, &Range::from);
	auto merged = std::vector<Range>();
	for (const auto part : stored) {
		if (!merged.empty() && part.from <= merged.back().till + 1) {
			merged.back().till = std::max(merged.back().till, part.till);
		} else {
			merged.push_back(part);
		}
	}
	stored = std::move(merged);
}

void collectForwardedPeers(
		base::flat_set<PeerId> &peers,
		const MTPDmessageFwdHeader &header) {
	for (const auto peer : {
			header.vfrom_id(),
			header.vsaved_from_peer(),
			header.vsaved_from_id() }) {
		if (peer) {
			peers.emplace(peerFromMTP(*peer));
		}
	}
}

void collectMessagePeers(
		base::flat_set<PeerId> &peers,
		const MTPMessage &message) {
	message.match([](const MTPDmessageEmpty &) {
	}, [&](const auto &data) {
		if (const auto from = data.vfrom_id()) {
			peers.emplace(peerFromMTP(*from));
		}
		if constexpr (std::is_same_v<std::decay_t<decltype(data)>, MTPDmessage>) {
			if (const auto bot = data.vvia_bot_id()) {
				peers.emplace(peerFromUser(UserId(bot->v)));
			}
			if (const auto forwarded = data.vfwd_from()) {
				collectForwardedPeers(peers, forwarded->data());
			}
		}
	});
}

[[nodiscard]] MTPmessages_Messages makeSlice(
		const QVector<MTPMessage> &messages,
		const QVector<MTPForumTopic> &topics,
		int count) {
	return MTP_messages_messagesSlice(
		MTP_flags(0), MTP_int(count), MTPint(), MTPint(), MTPSearchPostsFlood(),
		MTP_vector<MTPMessage>(messages), MTP_vector<MTPForumTopic>(topics),
		MTP_vector<MTPChat>(), MTP_vector<MTPUser>());
}

} // namespace

#ifdef _DEBUG
ConversationCache::DebugHistoryInfo ConversationCache::debugHistoryInfo(
		HistoryCacheKey key) const {
	auto result = DebugHistoryInfo{ .key = key };
	const auto found = _histories.find(key);
	if (found != end(_histories)) {
		const auto &entry = found->second;
		result.loaded = entry.loaded;
		result.dirty = entry.dirty;
		result.bottomFresh = entry.bottomFresh;
		result.total = entry.count;
		result.cachedMessages = entry.messages.size();
		if (!entry.messages.empty()) {
			result.firstId = entry.messages.begin()->first;
			result.lastId = entry.messages.rbegin()->first;
		}
		for (const auto range : entry.ranges) {
			const auto first = entry.messages.lower_bound(range.from);
			const auto last = entry.messages.upper_bound(range.till);
			result.ranges.push_back({ range.from, range.till,
				int(std::distance(first, last)) });
		}
	}
	if (const auto dialog = _dialogs.find(key.peer); dialog != end(_dialogs)) {
		result.dialogTop = MsgId(dialog->second.c_dialog().vtop_message().v);
	}
	if (const auto history = _session->data().historyLoaded(key.peer)) {
		if (const auto item = history->lastServerMessage()) {
			result.currentTop = item->id;
		}
	}
	if (const auto preview = _previewMessages.find(key.peer);
		preview != end(_previewMessages)) {
		result.previewId = preview->second;
	}
	return result;
}

void ConversationCache::setDebugHistoryPeer(PeerId peer) {
	_debugHistoryPeer = peer;
	_debugHistorySequence = 0;
	_debugHistoryEvents.clear();
}

void ConversationCache::inspectHistory(
		HistoryCacheKey key,
		Fn<void(DebugHistoryState)> done) {
	withIndex([=] {
		loadHistory(key, [=](HistoryEntry &) {
			auto events = std::vector<DebugHistoryEvent>();
			for (const auto &event : _debugHistoryEvents) {
				if (event.query.key == key) {
					events.push_back(event);
				}
			}
			done({ debugHistoryInfo(key), _debugHistoryPeer, std::move(events) });
		});
	});
}

void ConversationCache::traceHistory(
		const char *stage,
		const HistoryCacheQuery &query,
		const MTPmessages_Messages *response,
		const CachedHistorySlice *cached,
		int deleted) {
	if (!_debugHistoryPeer || query.key.peer != _debugHistoryPeer) {
		return;
	}
	auto event = DebugHistoryEvent{
		.sequence = ++_debugHistorySequence,
		.stage = stage,
		.query = query,
		.info = debugHistoryInfo(query.key),
		.deleted = deleted,
	};
	if (const auto data = cached ? &cached->messages : response) {
		data->match([](const MTPDmessages_messagesNotModified &) {
		}, [&](const auto &data) {
			event.returnedMessages = data.vmessages().v.size();
			for (const auto &message : data.vmessages().v) {
				const auto id = IdFromMessage(message);
				if (!event.returnedFirst || id < event.returnedFirst) {
					event.returnedFirst = id;
				}
				accumulate_max(event.returnedLast, id);
			}
		});
	}
	if (cached) {
		event.atTop = cached->atTop;
		event.atBottom = cached->atBottom;
	}
	// 只保留当前账号指定会话的最近 120 条诊断，不记录正文或媒体内容。
	if (_debugHistoryEvents.size() == 120) {
		_debugHistoryEvents.erase(_debugHistoryEvents.begin());
	}
	LOG(("Conversation cache: %1 peer=%2 offset=%3 add=%4 limit=%5 returned=%6 cached=%7 deleted=%8")
		.arg(stage).arg(query.key.peer.value).arg(query.offsetId.bare)
		.arg(query.addOffset).arg(query.limit).arg(event.returnedMessages)
		.arg(event.info.cachedMessages).arg(deleted));
	_debugHistoryEvents.push_back(std::move(event));
}
#endif

Storage::Cache::Key ConversationCache::storageKey(HistoryCacheKey key) const {
	return {
		SerializePeerId(key.peer),
		key.sublistPeer
			? (SerializePeerId(key.sublistPeer) | (uint64(1) << 63))
			: uint64(key.topicRootId.bare),
	};
}

bool ConversationCache::cacheable(const MTPMessage &message) const {
	if (!IsServerMsgId(IdFromMessage(message))) {
		return false;
	}
	return message.match([](const MTPDmessageEmpty &) {
		return false;
	}, [&](const auto &data) {
		if (const auto ttl = data.vttl_period()) {
			if (data.vdate().v + TimeId(ttl->v) <= base::unixtime::now()) {
				return false;
			}
		}
		if constexpr (std::is_same_v<std::decay_t<decltype(data)>, MTPDmessage>) {
			if (const auto media = data.vmedia()) {
				const auto ephemeral = media->match([](const MTPDmessageMediaPhoto &data) {
					return bool(data.vttl_seconds());
				}, [](const MTPDmessageMediaDocument &data) {
					return bool(data.vttl_seconds());
				}, [](const auto &) { return false; });
				if (ephemeral) {
					return false;
				}
			}
		}
		return true;
	});
}

void ConversationCache::loadHistory(
		HistoryCacheKey key,
		Fn<void(HistoryEntry&)> done,
		Fn<void()> cancelled) {
	auto &entry = _histories[key];
	if (entry.loaded) {
		done(entry);
		return;
	}
	entry.pending.push_back({ std::move(done), std::move(cancelled) });
	if (entry.pending.size() > 1) {
		return;
	}
	entry.generation = ++_historyGeneration;
	const auto generation = entry.generation;
	_database->get(storageKey(key), onMain<QByteArray>(_session, [=](QByteArray bytes) {
		const auto found = _histories.find(key);
		if (found == _histories.end() || found->second.generation != generation) {
			return;
		}
		auto &entry = found->second;
		QDataStream stream(bytes);
		if (!bytes.isEmpty() && readHeader(stream, _session->userId())) {
			auto peers = QByteArray();
			auto payload = QByteArray();
			auto ranges = qint32();
			stream >> peers >> payload >> ranges;
			for (auto i = 0; i < ranges && !stream.atEnd(); ++i) {
				auto from = qint64();
				auto till = qint64();
				stream >> from >> till;
				entry.ranges.push_back({ MsgId(from), MsgId(till) });
			}
			const auto parsed = decode<MTPmessages_Messages>(payload);
			if (stream.status() == QDataStream::Ok && stream.atEnd()
				&& parsed && parsed->type() == mtpc_messages_messagesSlice) {
				entry.peers = restorePeers(peers);
				const auto &data = parsed->c_messages_messagesSlice();
				entry.count = data.vcount().v;
				entry.topics = data.vtopics().v;
				for (const auto &message : data.vmessages().v) {
					if (cacheable(message)) {
						entry.messages.emplace(IdFromMessage(message), message);
					}
				}
			} else {
				entry.ranges.clear();
			}
		}
		entry.loaded = true;
		restoreHistoryMetadata(key, entry);
#ifdef _DEBUG
		traceHistory("loaded", { .key = key });
#endif
		for (auto &callback : base::take(entry.pending)) {
			callback.done(entry);
		}
	}));
}

void ConversationCache::restoreHistoryMetadata(
		HistoryCacheKey key,
		const HistoryEntry &entry) {
	auto &owner = _session->data();
	const auto peer = owner.peer(key.peer);
	if (const auto forum = peer->forum()) {
		auto missing = QVector<MTPForumTopic>();
		for (const auto &topic : entry.topics) {
			if (topic.type() != mtpc_forumTopic) {
				continue;
			}
			const auto &data = topic.c_forumTopic();
			if (forum->topicFor(data.vid().v)) {
				continue;
			}
			const auto message = entry.messages.find(data.vtop_message().v);
			if (message != end(entry.messages)) {
				owner.addNewMessage(message->second, {}, NewMessageType::Existing);
			}
			missing.push_back(topic);
		}
		peer->processTopics(MTP_vector<MTPForumTopic>(missing));
	}
	if (key.sublistPeer && !entry.messages.empty()) {
		const auto saved = peer->isSelf() ? &owner.savedMessages() : peer->monoforum();
		if (saved && !saved->sublistLoaded(owner.peer(key.sublistPeer))) {
			if (const auto item = owner.addNewMessage(
					entry.messages.rbegin()->second, {}, NewMessageType::Existing)) {
				saved->sublist(owner.peer(key.sublistPeer))->applyMaybeLast(item);
			}
		}
	}
}

void ConversationCache::getHistory(
		const HistoryCacheQuery &query,
		Fn<void(std::optional<CachedHistorySlice>)> done) {
	// 即使数据已在内存中，也等调用方保存请求编号后再回调。
	crl::on_main(_session, [=] {
		if (!_enabled || _storageClearing) {
			done(std::nullopt);
			return;
		}
		withIndex([=] {
			loadHistory(query.key, [=](HistoryEntry &entry) {
#ifdef _DEBUG
				traceHistory("read-before", query);
#endif
				auto result = slice(query, entry);
				result ? ++_hits : ++_misses;
#ifdef _DEBUG
				traceHistory(result ? "read-hit" : "read-miss", query,
					nullptr, result ? &*result : nullptr);
#endif
				done(std::move(result));
			}, [=] { done(std::nullopt); });
		});
	});
}

void ConversationCache::restoreMessage(PeerId peer, MsgId id, Fn<void(bool)> done) {
	crl::on_main(_session, [=] {
		if (!_enabled || _storageClearing) {
			done(false);
			return;
		}
		withIndex([=] {
			const auto known = _nonChannelPeers.find(id);
			const auto target = peer ? peer
				: known != end(_nonChannelPeers) ? known->second : PeerId();
			if (const auto last = _lastMessages.find(target);
				last != end(_lastMessages) && IdFromMessage(last->second) == id
				&& cacheable(last->second)) {
				const auto restored = _session->data().addNewMessage(
					last->second, {}, NewMessageType::Existing) != nullptr;
				_messageDataHits += int(restored);
				done(restored);
				return;
			}
			auto keys = std::vector<HistoryCacheKey>();
			for (const auto key : _historyKeys) {
				if (key.peer == target) {
					keys.push_back(key);
				}
			}
			restoreMessageFromHistories(std::move(keys), id, std::move(done));
		});
	});
}

void ConversationCache::restoreMessageFromHistories(
		std::vector<HistoryCacheKey> keys,
		MsgId id,
		Fn<void(bool)> done) {
	if (keys.empty()) {
		done(false);
		return;
	}
	const auto key = keys.back();
	keys.pop_back();
	loadHistory(key, [=](HistoryEntry &entry) {
		const auto i = entry.messages.find(id);
		if (i != end(entry.messages) && cacheable(i->second)) {
			const auto restored = _session->data().addNewMessage(
				i->second, {}, NewMessageType::Existing) != nullptr;
			_messageDataHits += int(restored);
			done(restored);
			return;
		}
		restoreMessageFromHistories(std::move(keys), id, std::move(done));
	}, [=] { done(false); });
}

std::optional<CachedHistorySlice> ConversationCache::latestPreview(
		const HistoryCacheQuery &query,
		const HistoryEntry &entry) {
	if (query.key.topicRootId || query.key.sublistPeer
		|| (query.offsetId && !query.allowNearest)) {
		return std::nullopt;
	}
	const auto latest = _lastMessages.find(query.key.peer);
	if (latest == end(_lastMessages) || !cacheable(latest->second)) {
		return std::nullopt;
	}
	const auto id = IdFromMessage(latest->second);
	if (!query.latest && query.offsetId && query.offsetId < id) {
		return std::nullopt;
	}
	const auto history = _session->data().historyLoaded(query.key.peer);
	const auto item = history ? history->lastServerMessage() : nullptr;
	const auto dialog = _dialogs.find(query.key.peer);
	const auto lastId = item ? item->id
		: dialog != end(_dialogs) ? MsgId(dialog->second.c_dialog().vtop_message().v)
		: MsgId();
	if (id != lastId) {
		return std::nullopt;
	}
	if (!entry.messages.contains(id)) {
		_previewMessages[query.key.peer] = id;
	}
	// 列表摘要只能证明最新一条，不能当作已缓存完整历史。
	return CachedHistorySlice{
		.messages = makeSlice({ latest->second }, {}, std::max(entry.count, 2)),
		.atTop = false,
		.atBottom = true,
	};
}

std::optional<CachedHistorySlice> ConversationCache::slice(
		const HistoryCacheQuery &query,
		HistoryEntry &entry) {
	for (auto i = begin(entry.messages); i != end(entry.messages);) {
		if (!cacheable(i->second)) {
			i = entry.messages.erase(i);
			entry.dirty = true;
			scheduleSave();
		} else {
			++i;
		}
	}
	if (!query.key.topicRootId && !query.key.sublistPeer
		&& !entry.ranges.empty() && !entry.messages.empty()
		&& entry.ranges.back().till == ServerMaxMsgId) {
		const auto latest = _lastMessages.find(query.key.peer);
		const auto cachedLast = entry.messages.rbegin()->first;
		const auto history = _session->data().historyLoaded(query.key.peer);
		const auto item = history ? history->lastServerMessage() : nullptr;
		const auto latestId = item ? item->id
			: latest != end(_lastMessages) ? IdFromMessage(latest->second) : MsgId();
		if (latestId > cachedLast) {
			entry.ranges.back().till = cachedLast;
			entry.bottomFresh = false;
			entry.dirty = true;
			scheduleSave();
		}
	}
	const auto anchor = query.latest || !query.offsetId
		? ServerMaxMsgId
		: query.addOffset == 0 ? query.offsetId - 1 : query.offsetId;
	auto range = ranges::find_if(entry.ranges, [&](Range range) {
		return range.from <= anchor && range.till >= anchor;
	});
	if (range == end(entry.ranges)) {
		if (const auto preview = latestPreview(query, entry)) {
			return preview;
		}
		if (query.allowNearest && !entry.ranges.empty()) {
			range = end(entry.ranges) - 1;
		} else {
			return std::nullopt;
		}
	}
	auto available = QVector<MTPMessage>();
	for (const auto &[id, message] : ranges::views::reverse(entry.messages)) {
		if (id >= range->from && id <= range->till) {
			available.push_back(message);
		}
	}
	auto offset = 0;
	if (query.offsetId && !query.latest) {
		while (offset < available.size()
			&& IdFromMessage(available[offset]) >= query.offsetId) {
			++offset;
		}
	}
	auto start = query.latest ? 0
		: std::clamp(offset + query.addOffset, 0, int(available.size()));
	auto count = std::min(query.limit, int(available.size()) - start);
	if (!query.latest && query.offsetId && query.addOffset <= -query.limit) {
		count = std::min(query.limit, offset);
		start = offset - count;
		if (!count && range->till != ServerMaxMsgId) {
			return std::nullopt;
		}
	} else if (!count && range->from != 0) {
		return latestPreview(query, entry);
	}
	const auto atTop = range->from == 0 && start + count == available.size();
	const auto atBottom = range->till == ServerMaxMsgId && !start;
	if (!count && (!query.offsetId || query.allowNearest) && !(atTop && atBottom)) {
		return latestPreview(query, entry);
	}
	return CachedHistorySlice{
		.messages = makeSlice(available.mid(start, count), {},
			atTop && atBottom ? count
				: std::max(entry.count, count + int(!atTop) + int(!atBottom))),
		.atTop = atTop,
		.atBottom = atBottom,
	};
}

void ConversationCache::putHistory(
		const HistoryCacheQuery &query,
		const MTPmessages_Messages &messages,
		bool refreshVisible) {
	if (!_enabled || _storageClearing
		|| messages.type() == mtpc_messages_messagesNotModified) {
		return;
	}
	loadHistory(query.key, [=](HistoryEntry &entry) {
#ifdef _DEBUG
		traceHistory("write-before", query, &messages);
#endif
		auto &owner = _session->data();
		const auto previousCount = entry.count;
		auto response = QVector<MTPMessage>();
		messages.match([](const MTPDmessages_messagesNotModified &) {
		}, [&](const auto &data) {
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
			owner.peer(query.key.peer)->processTopics(data.vtopics());
			response = data.vmessages().v;
			entry.topics = data.vtopics().v;
			if constexpr (std::is_same_v<std::decay_t<decltype(data)>, MTPDmessages_messages>) {
				entry.count = response.size();
			} else {
				entry.count = data.vcount().v;
			}
			for (const auto &user : data.vusers().v) {
				user.match([&](const auto &user) {
					entry.peers.emplace(peerFromUser(UserId(user.vid())));
				});
			}
			for (const auto &chat : data.vchats().v) {
				chat.match([&](const MTPDchat &chat) {
					entry.peers.emplace(peerFromChat(ChatId(chat.vid())));
				}, [&](const MTPDchatForbidden &chat) {
					entry.peers.emplace(peerFromChat(ChatId(chat.vid())));
				}, [&](const MTPDchatEmpty &chat) {
					entry.peers.emplace(peerFromChat(ChatId(chat.vid())));
				}, [&](const auto &channel) {
					entry.peers.emplace(peerFromChannel(ChannelId(channel.vid())));
				});
			}
		});
		entry.peers.emplace(query.key.peer);
		if (query.key.sublistPeer) {
			entry.peers.emplace(query.key.sublistPeer);
		}
		auto received = base::flat_set<MsgId>();
		auto range = Range{ ServerMaxMsgId, 0 };
		for (const auto &message : response) {
			const auto id = IdFromMessage(message);
			received.emplace(id);
			range.from = std::min(range.from, id);
			range.till = std::max(range.till, id);
		}
		const auto responseLast = range.till;
		if (!query.offsetId) {
			range.till = ServerMaxMsgId;
			if (response.isEmpty()) {
				range.from = 0;
			}
		} else if (!query.addOffset) {
			range.till = std::max(range.till, query.offsetId - 1);
			if (response.size() < query.limit) {
				range.from = 0;
			}
		} else if (query.addOffset <= -query.limit) {
			range.from = std::min(range.from, query.offsetId);
			if (response.size() < query.limit) {
				range.till = ServerMaxMsgId;
			}
		}
		if (response.size() == entry.count) {
			range = { 0, ServerMaxMsgId };
		}
		const auto deleteTill = std::min(range.till, query.deleteTill);
		const auto ordinary = !query.key.topicRootId && !query.key.sublistPeer;
		const auto history = owner.history(query.key.peer);
		const auto currentLast = history->lastServerMessage();
		if (ordinary && (query.latest || !query.offsetId) && currentLast
			&& currentLast->id > responseLast && currentLast->id > query.deleteTill) {
			range.till = responseLast;
			entry.count = std::max(entry.count, previousCount);
		}
		if (range.till == ServerMaxMsgId) {
			entry.bottomFresh = true;
		}
		const auto previousLast = entry.messages.empty() ? MsgId() : entry.messages.rbegin()->first;
		auto deleted = QVector<MTPint>();
		auto replacePreviewTop = false;
		for (auto i = begin(entry.messages); i != end(entry.messages);) {
			if (i->first >= range.from && i->first <= deleteTill
				&& !received.contains(i->first)) {
				deleted.push_back(MTP_int(i->first));
				i = entry.messages.erase(i);
			} else {
				++i;
			}
		}
		if (const auto preview = _previewMessages.find(query.key.peer);
			ordinary && preview != end(_previewMessages)) {
			if (received.contains(preview->second)) {
				_previewMessages.erase(preview);
			} else if (preview->second >= range.from && preview->second <= deleteTill) {
				history->messages().removeOne(preview->second);
				replacePreviewTop = currentLast && currentLast->id == preview->second;
				if (replacePreviewTop) {
					history->applyDialogTopMessage(MsgId());
				}
				if (const auto last = _lastMessages.find(query.key.peer);
					last != end(_lastMessages) && IdFromMessage(last->second) == preview->second) {
					_lastMessages.erase(last);
				}
				_previewMessages.erase(preview);
			}
		}
		for (const auto &message : response) {
			if (cacheable(message)) {
				entry.messages[IdFromMessage(message)] = message;
			}
		}
		if (range.from <= range.till) {
			mergeRange(entry.ranges, range);
		}
		entry.dirty = true;
		_historyKeys.emplace(query.key);
		if (!peerIsChannel(query.key.peer)) {
			for (const auto &[id, message] : entry.messages) {
				_nonChannelPeers[id] = query.key.peer;
			}
		}
		_indexDirty = true;
		trimHistory(query.key, entry);
#ifdef _DEBUG
		traceHistory("write-after", query, &messages, nullptr, deleted.size());
#endif
		scheduleSave();
		if (replacePreviewTop) {
			owner.processMessages(response, NewMessageType::Existing);
			history->applyDialogTopMessage(responseLast);
		}
		if (!refreshVisible) {
			return;
		}
		if (!deleted.empty()) {
			owner.processMessagesDeleted(query.key.peer, deleted);
		}
		const auto previousMax = history->maxMsgId();
		owner.processMessages(response, NewMessageType::Existing);
		for (const auto &message : response) {
			owner.updateEditedMessage(message);
			if (!ordinary && range.from <= previousLast && range.till >= previousLast) {
				if (const auto item = owner.message(query.key.peer, IdFromMessage(message))) {
					_session->changes().messageUpdated(item, Data::MessageUpdate::Flag::NewMaybeAdded);
				}
			}
		}
		if (ordinary && !history->isEmpty() && range.from <= previousMax
			&& range.till >= previousMax && history->loadedAtBottom()) {
			history->addNewerSlice(response);
			owner.sendHistoryChangeNotifications();
		} else if (ordinary && !history->isEmpty() && range.from > previousMax) {
			history->setNotLoadedAtBottom();
		}
	});
}

void ConversationCache::trimHistory(HistoryCacheKey key, HistoryEntry &entry) {
	constexpr auto kMaxMessageBytes = 8 * 1024 * 1024;
	auto bytes = int64();
	for (const auto &[id, message] : entry.messages) {
		bytes += tl::count_length(message);
	}
	if (entry.messages.size() <= kMaxHistoryMessages && bytes <= kMaxMessageBytes) {
		return;
	}
	const auto sharedIndex = ranges::any_of(_historyKeys, [&](HistoryCacheKey other) {
		return other.peer == key.peer && other != key;
	});
	while (entry.messages.size() > kMaxHistoryMessages || bytes > kMaxMessageBytes) {
		bytes -= tl::count_length(entry.messages.begin()->second);
		if (!peerIsChannel(key.peer) && !sharedIndex) {
			_nonChannelPeers.remove(entry.messages.begin()->first);
		}
		entry.messages.erase(entry.messages.begin());
	}
	if (entry.messages.empty()) {
		entry.ranges.clear();
		return;
	}
	const auto first = entry.messages.begin()->first;
	entry.ranges.erase(ranges::remove_if(entry.ranges, [&](Range &range) {
		range.from = std::max(range.from, first);
		return range.from > range.till;
	}), entry.ranges.end());
}

void ConversationCache::saveHistories() {
	for (auto &[key, entry] : _histories) {
		if (!entry.loaded || !entry.dirty) {
			continue;
		}
		entry.dirty = false;
		auto messages = QVector<MTPMessage>();
		for (const auto &[id, message] : entry.messages) {
			messages.push_back(message);
			collectMessagePeers(entry.peers, message);
		}
		auto bytes = QByteArray();
		QDataStream stream(&bytes, QIODevice::WriteOnly);
		writeHeader(stream, _session->userId());
		stream << serializePeers(entry.peers)
			<< encode(makeSlice(messages, entry.topics, entry.count))
			<< qint32(entry.ranges.size());
		for (const auto range : entry.ranges) {
			stream << qint64(range.from.bare) << qint64(range.till.bare);
		}
		_database->put(storageKey(key), std::move(bytes),
			onMain<Storage::Cache::Error>(_session, [=](Storage::Cache::Error error) {
				if (error.type != Storage::Cache::Error::Type::None) {
					LOG(("Conversation cache: could not save history."));
				}
			}));
	}
}

void ConversationCache::messageReceived(
		const MTPMessage &message,
		NewMessageType type) {
	if (!_enabled || _restoring || !cacheable(message)) {
		return;
	}
	const auto id = FullMsgId(PeerFromMessage(message), IdFromMessage(message));
	if (_pendingConfirmedMessages.remove(id)) {
		type = NewMessageType::Unread;
	}
	const auto peer = PeerFromMessage(message);
	if (type == NewMessageType::Last || type == NewMessageType::Unread) {
		const auto i = _lastMessages.find(peer);
		if (i == end(_lastMessages) || IdFromMessage(i->second) <= IdFromMessage(message)) {
			_lastMessages[peer] = message;
			if (!peerIsChannel(peer)) {
				withIndex([=] {
					_nonChannelPeers[IdFromMessage(message)] = peer;
					_indexDirty = true;
					scheduleSave();
				});
			}
			scheduleSave();
		}
	}
	if (type == NewMessageType::Unread) {
		if (message.type() == mtpc_messageService) {
			serviceMessageReceived(message.c_messageService());
		}
		applyMessage(message, true);
#ifdef _DEBUG
		traceHistory("new-message", { .key = { peer } });
#endif
	}
}

void ConversationCache::cacheConfirmedMessage(FullMsgId id) {
	if (!_enabled || _restoring || !_pendingConfirmedMessages.emplace(id).second) {
		return;
	}
	// 简短发送回执缺少完整消息资料，沿官方合并请求补取已确认消息。
	_session->api().requestMessageDataFromServer(_session->data().peer(id.peer), id.msg, [=] {
		_pendingConfirmedMessages.remove(id);
	});
}

void ConversationCache::messageEdited(const MTPMessage &message) {
	if (!_enabled || _restoring) {
		return;
	}
	const auto i = _lastMessages.find(PeerFromMessage(message));
	if (i != end(_lastMessages) && IdFromMessage(i->second) == IdFromMessage(message)) {
		i->second = message;
		scheduleSave();
	}
	applyMessage(message, false);
}

void ConversationCache::applyMessage(const MTPMessage &message, bool isNew) {
	withIndex([=] {
		const auto peer = PeerFromMessage(message);
		// 首条实时消息也要建立缓存，不能依赖先请求历史记录。
		if (isNew && _historyKeys.emplace(HistoryCacheKey{ peer }).second) {
			_indexDirty = true;
		}
		const auto keys = _historyKeys;
		for (const auto key : keys) {
			if (key.peer != peer) {
				continue;
			}
			loadHistory(key, [=](HistoryEntry &entry) {
				const auto id = IdFromMessage(message);
				if (isNew && !entry.bottomFresh && !entry.messages.empty()
					&& !entry.ranges.empty() && entry.ranges.back().till == ServerMaxMsgId
					&& id > entry.messages.rbegin()->first) {
					entry.ranges.back().till = entry.messages.rbegin()->first;
					entry.dirty = true;
					scheduleSave();
				}
				const auto ordinaryNew = isNew && !key.topicRootId && !key.sublistPeer;
				if (!entry.messages.contains(id) && !ordinaryNew) {
					return;
				}
				if (cacheable(message)) {
					if (ordinaryNew && ranges::none_of(entry.ranges, [=](Range range) {
						return range.from <= id && id <= range.till;
					})) {
						// 新消息单独保存，旧尾部与新消息之间的空档仍需服务器确认。
						const auto newest = entry.messages.empty() || id > entry.messages.rbegin()->first;
						mergeRange(entry.ranges, Range{ id, newest ? ServerMaxMsgId : id });
						if (newest) {
							entry.bottomFresh = true;
						}
					}
					entry.count += int(!entry.messages.contains(id));
					entry.messages[id] = message;
					if (!peerIsChannel(peer)) {
						_nonChannelPeers[id] = peer;
						_indexDirty = true;
					}
				} else {
					entry.messages.remove(id);
				}
				entry.dirty = true;
				trimHistory(key, entry);
				scheduleSave();
			});
		}
	});
}

void ConversationCache::messagesDeleted(PeerId peer, const QVector<MTPint> &ids) {
	if (!_enabled || _restoring) {
		return;
	}
	withIndex([=] {
		auto byPeer = base::flat_map<PeerId, QVector<MTPint>>();
		for (const auto id : ids) {
			const auto i = _nonChannelPeers.find(id.v);
			const auto target = peer ? peer : (i != end(_nonChannelPeers) ? i->second : PeerId());
			if (target) {
				byPeer[target].push_back(id);
			}
		}
		const auto keys = _historyKeys;
		for (const auto &[target, messages] : byPeer) {
			if (const auto preview = _previewMessages.find(target);
				preview != end(_previewMessages)
				&& ranges::contains(messages, preview->second.bare, &MTPint::v)) {
				_previewMessages.erase(preview);
			}
			const auto last = _lastMessages.find(target);
			if (last != end(_lastMessages) && ranges::contains(messages, IdFromMessage(last->second).bare, &MTPint::v)) {
				_lastMessages.erase(last);
			}
			for (const auto key : keys) {
				if (key.peer == target) {
					deleteMessages(key, messages);
				}
			}
		}
		scheduleSave();
	});
}

void ConversationCache::deleteMessages(HistoryCacheKey key, const QVector<MTPint> &ids) {
	loadHistory(key, [=](HistoryEntry &entry) {
		for (const auto id : ids) {
			if (entry.messages.remove(id.v)) {
				entry.count = std::max(0, entry.count - 1);
				entry.dirty = true;
			}
			if (!peerIsChannel(key.peer)) {
				_nonChannelPeers.remove(id.v);
			}
		}
		_indexDirty = true;
		scheduleSave();
	});
}

void ConversationCache::historyCleared(PeerId peer) {
	_previewMessages.remove(peer);
	if (!_enabled || _restoring) {
		return;
	}
	_lastMessages.remove(peer);
	withIndex([=] {
		const auto keys = _historyKeys;
		for (const auto key : keys) {
			if (key.peer != peer) {
				continue;
			}
			// 保留正在等待的读取回调，清空其数据后再恢复正常请求流程。
			loadHistory(key, [=](HistoryEntry &entry) {
				entry.messages.clear();
				entry.ranges.clear();
				entry.count = 0;
				entry.dirty = false;
				entry.bottomFresh = false;
				_historyKeys.remove(key);
				_database->remove(storageKey(key));
			});
		}
		for (auto i = begin(_nonChannelPeers); i != end(_nonChannelPeers);) {
			i = i->second == peer ? _nonChannelPeers.erase(i) : std::next(i);
		}
		_indexDirty = true;
		scheduleSave();
	});
}

} // namespace Extras
