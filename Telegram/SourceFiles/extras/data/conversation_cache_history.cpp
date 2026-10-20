#include "extras/data/conversation_cache.h"

#include "extras/data/conversation_cache_serialization.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_forum.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/cache/storage_cache_database.h"

namespace Extras {
namespace {

using namespace CacheSerialization;

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
				auto result = slice(query, entry);
				result ? ++_hits : ++_misses;
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
	const auto anchor = !query.offsetId
		? ServerMaxMsgId
		: query.addOffset == 0 ? query.offsetId - 1 : query.offsetId;
	auto range = ranges::find_if(entry.ranges, [&](Range range) {
		return range.from <= anchor && range.till >= anchor;
	});
	if (range == end(entry.ranges)) {
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
	if (query.offsetId) {
		while (offset < available.size()
			&& IdFromMessage(available[offset]) >= query.offsetId) {
			++offset;
		}
	}
	auto start = std::clamp(offset + query.addOffset, 0, int(available.size()));
	auto count = std::min(query.limit, int(available.size()) - start);
	if (query.offsetId && query.addOffset <= -query.limit) {
		count = std::min(query.limit, offset);
		start = offset - count;
		if (!count && range->till != ServerMaxMsgId) {
			return std::nullopt;
		}
	} else if (!count && range->from != 0) {
		return std::nullopt;
	}
	const auto atTop = range->from == 0 && start + count == available.size();
	const auto atBottom = range->till == ServerMaxMsgId && !start;
	if (!count && (!query.offsetId || query.allowNearest) && !(atTop && atBottom)) {
		return std::nullopt;
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
		auto &owner = _session->data();
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
		if (range.till == ServerMaxMsgId) {
			entry.bottomFresh = true;
		}
		const auto ordinary = !query.key.topicRootId && !query.key.sublistPeer;
		const auto previousLast = entry.messages.empty() ? MsgId() : entry.messages.rbegin()->first;
		auto deleted = QVector<MTPint>();
		for (auto i = begin(entry.messages); i != end(entry.messages);) {
			if (i->first >= range.from && i->first <= range.till
				&& !received.contains(i->first)) {
				deleted.push_back(MTP_int(i->first));
				i = entry.messages.erase(i);
			} else {
				++i;
			}
		}
		for (const auto &message : response) {
			if (cacheable(message)) {
				entry.messages[IdFromMessage(message)] = message;
			}
		}
		if (range.from <= range.till) {
			entry.ranges.push_back(range);
			ranges::sort(entry.ranges, {}, &Range::from);
			auto merged = std::vector<Range>();
			for (const auto part : entry.ranges) {
				if (!merged.empty() && part.from <= merged.back().till + 1) {
					merged.back().till = std::max(merged.back().till, part.till);
				} else {
					merged.push_back(part);
				}
			}
			entry.ranges = std::move(merged);
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
		scheduleSave();
		if (!refreshVisible) {
			return;
		}
		if (!deleted.empty()) {
			owner.processMessagesDeleted(query.key.peer, deleted);
		}
		const auto history = owner.history(query.key.peer);
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
	const auto peer = PeerFromMessage(message);
	if (type == NewMessageType::Last || type == NewMessageType::Unread) {
		const auto i = _lastMessages.find(peer);
		if (i == end(_lastMessages) || IdFromMessage(i->second) <= IdFromMessage(message)) {
			_lastMessages[peer] = message;
			scheduleSave();
		}
	}
	if (type == NewMessageType::Unread) {
		applyMessage(message, true);
	}
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
				const auto append = isNew && !key.topicRootId && !key.sublistPeer
					&& !entry.ranges.empty() && entry.ranges.back().till == ServerMaxMsgId;
				if (!entry.messages.contains(id) && !append) {
					return;
				}
				if (cacheable(message)) {
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
