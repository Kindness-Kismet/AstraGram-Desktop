#include "extras/data/conversation_cache.h"

#include "extras/data/conversation_cache_serialization.h"
#include "core/version.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "history/history_unread_things.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "storage/storage_encryption.h"
#include "storage/serialize_peer.h"
#include "storage/cache/storage_cache_database.h"

namespace Extras {
namespace {

using namespace CacheSerialization;

constexpr auto kListMetadataPref = "conversation-list-metadata";

[[nodiscard]] Storage::Cache::Database::Settings cacheSettings() {
	auto result = Storage::Cache::Database::Settings();
	result.totalSizeLimit = kMaxCacheBytes;
	result.totalTimeLimit = 0;
	return result;
}

[[nodiscard]] MTPDialog makeDialog(
		not_null<History*> history,
		const MTPDdialog &data) {
	using Flag = MTPDdialog::Flag;
	auto flags = data.vflags().v
		& ~(Flag::f_pts | Flag::f_draft | Flag::f_pinned | Flag::f_unread_mark);
	flags |= Flag::f_folder_id;
	if (history->isPinnedDialog(0)) {
		flags |= Flag::f_pinned;
	}
	if (history->unreadMark()) {
		flags |= Flag::f_unread_mark;
	}
	const auto last = history->lastServerMessage();
	return MTP_dialog(
		MTP_flags(flags),
		data.vpeer(),
		MTP_int(last ? last->id : 0),
		MTP_int(history->inboxReadTillId()),
		MTP_int(history->outboxReadTillId()),
		MTP_int(history->unreadCount()),
		MTP_int(history->unreadMentions().count(0)),
		MTP_int(history->unreadReactions().count(0)),
		MTP_int(history->unreadPollVotesCount()),
		data.vnotify_settings(),
		MTPint(),
		MTPDraftMessage(),
		MTP_int(history->folder() ? history->folder()->id() : 0),
		MTP_int(data.vttl_period().value_or(0)));
}

} // namespace

ConversationCache::ConversationCache(not_null<Main::Session*> session)
: _session(session)
, _database(std::make_unique<Storage::Cache::Database>(
	// 媒体缓存会清理自身目录，历史缓存必须放在同级目录。
	session->local().cachePath() + u"-conversations"_q,
	cacheSettings()))
, _saveTimer([=] { flush(); }) {
	_database->open(session->local().cacheKey(), onMain<Storage::Cache::Error>(session, [=](
			Storage::Cache::Error error) {
		if (error.type != Storage::Cache::Error::Type::None) {
			LOG(("Conversation cache: could not open history storage."));
			_enabled = false;
		}
	}));
}

ConversationCache::~ConversationCache() {
	_database->close();
	_database->sync();
}

void ConversationCache::restore() {
	_restoring = true;
	const auto bytes = _session->local().readPref<QByteArray>(kSnapshotPref);
	if (!bytes.isEmpty()) {
		QDataStream stream(bytes);
		auto peers = QByteArray();
		auto dialogsBytes = QByteArray();
		auto filtersBytes = QByteArray();
		if (readHeader(stream, _session->userId())) {
			stream >> peers >> dialogsBytes >> filtersBytes;
			const auto dialogs = decode<MTPmessages_Dialogs>(dialogsBytes);
			const auto filters = decode<MTPmessages_DialogFilters>(filtersBytes);
			if (stream.status() == QDataStream::Ok && stream.atEnd()
				&& dialogs && filters
				&& dialogs->type() == mtpc_messages_dialogs) {
				restorePeers(peers);
				const auto &data = dialogs->c_messages_dialogs();
				for (const auto &message : data.vmessages().v) {
					if (cacheable(message)) {
						_lastMessages.emplace(PeerFromMessage(message), message);
					}
				}
				for (const auto &dialog : data.vdialogs().v) {
					const auto &fields = dialog.c_dialog();
					const auto peer = peerFromMTP(fields.vpeer());
					_dialogs.emplace(peer, dialog);
					_restoredDialogs.emplace(peer, fields.vfolder_id()->v);
				}
				_session->data().chatsFilters().restoreCached(
					filters->data().vfilters().v,
					filters->data().is_tags_enabled());
				auto messages = QVector<MTPMessage>();
				for (const auto &[peer, message] : _lastMessages) {
					messages.push_back(message);
				}
				_session->data().applyDialogs(nullptr, messages, data.vdialogs().v);
				restoreListMetadata();
				_restoredCount = _restoredDialogs.size();
				_hasDialogsSnapshot = true;
			}
		}
	}
	_restoring = false;
	_ready = true;
	_session->data().chatListEntryRefreshes(
	) | rpl::on_next([=] { scheduleSave(); }, _lifetime);
	_session->data().chatsFilters().changed(
	) | rpl::on_next([=] { scheduleSave(); }, _lifetime);

	_database->get({ 1, 0 }, onMain<QByteArray>(_session, [=](QByteArray bytes) {
		QDataStream stream(bytes);
		if (!bytes.isEmpty() && readHeader(stream, _session->userId())) {
			auto count = qint32();
			stream >> count;
			for (auto i = 0; i < count && !stream.atEnd(); ++i) {
				auto peer = quint64();
				auto topic = qint64();
				auto sublist = quint64();
				stream >> peer >> topic >> sublist;
				_historyKeys.emplace(HistoryCacheKey{
					DeserializePeerId(peer), MsgId(topic), DeserializePeerId(sublist) });
			}
			stream >> count;
			for (auto i = 0; i < count && !stream.atEnd(); ++i) {
				auto id = qint64();
				auto peer = quint64();
				stream >> id >> peer;
				_nonChannelPeers.emplace(MsgId(id), DeserializePeerId(peer));
			}
			if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
				_historyKeys.clear();
				_nonChannelPeers.clear();
			}
		}
		_indexReady = true;
		for (const auto key : _historyKeys) {
			if (key.topicRootId || key.sublistPeer) {
				loadHistory(key, [](HistoryEntry &) {});
			}
		}
		for (auto &done : base::take(_afterIndex)) {
			done();
		}
	}));
}

void ConversationCache::restoreListMetadata() {
	auto stream = QDataStream(_session->local().readPref<QByteArray>(kListMetadataPref));
	if (!readHeader(stream, _session->userId())) {
		return;
	}
	while (!stream.atEnd()) {
		auto peer = quint64();
		auto date = qint32();
		stream >> peer >> date;
		if (stream.status() != QDataStream::Ok) {
			return;
		}
		const auto id = DeserializePeerId(peer);
		if (!_dialogs.contains(id)) {
			continue;
		}
		const auto history = _session->data().history(id);
		if (!history->lastServerMessage()) {
			history->restoreCachedChatListMetadata(date);
		}
	}
}

void ConversationCache::scheduleSave() {
	if (_enabled && _ready && !_restoring && !_saveTimer.isActive()) {
		_saveTimer.callOnce(1000);
	}
}

void ConversationCache::flush() {
	_saveTimer.cancel();
	if (!_enabled || !_ready || _restoring || _storageClearing) {
		return;
	}
	saveDialogs();
	saveHistories();
	saveIndex();
}

void ConversationCache::saveDialogs() {
	const auto &filters = _session->data().chatsFilters();
	if (!filters.loaded()) {
		return;
	}
	auto peers = base::flat_set<PeerId>();
	auto dialogs = QVector<MTPDialog>();
	auto messages = QVector<MTPMessage>();
	auto metadata = QByteArray();
	auto metadataStream = QDataStream(&metadata, QIODevice::WriteOnly);
	writeHeader(metadataStream, _session->userId());
	const auto collect = [&](not_null<Dialogs::MainList*> list) {
		for (const auto &row : list->indexed()->all()) {
			const auto history = row->history();
			if (!history || dialogs.size() >= kMaxDialogs) {
				continue;
			}
			const auto i = _dialogs.find(history->peer->id);
			if (i == end(_dialogs)) {
				continue;
			}
			dialogs.push_back(makeDialog(history, i->second.c_dialog()));
			metadataStream << SerializePeerId(history->peer->id)
				<< qint32(history->chatListTimeId());
			peers.emplace(history->peer->id);
			const auto item = history->lastServerMessage();
			if (!item) {
				continue;
			}
			const auto last = _lastMessages.find(history->peer->id);
			const auto entry = _histories.find({ history->peer->id });
			auto message = (last != end(_lastMessages) && IdFromMessage(last->second) == item->id)
				? &last->second : nullptr;
			if (!message && entry != end(_histories)) {
				const auto cached = entry->second.messages.find(item->id);
				if (cached != end(entry->second.messages)) {
					message = &cached->second;
				}
			}
			if (message && cacheable(*message)) {
				messages.push_back(*message);
			}
			peers.emplace(item->from()->id);
			if (const auto sender = item->originalSender()) {
				peers.emplace(sender->id);
			}
		}
	};
	collect(_session->data().chatsList());
	if (const auto folder = _session->data().folderLoaded(Data::Folder::kId)) {
		collect(folder->chatsList());
	}
	auto filterList = QVector<MTPDialogFilter>();
	if (!ranges::contains(filters.list(), FilterId(0), &Data::ChatFilter::id)) {
		filterList.push_back(MTP_dialogFilterDefault());
	}
	for (const auto &filter : filters.list()) {
		filterList.push_back(filter.id() ? filter.tl() : MTP_dialogFilterDefault());
		for (const auto history : filter.always()) {
			peers.emplace(history->peer->id);
		}
		for (const auto history : filter.never()) {
			peers.emplace(history->peer->id);
		}
	}
	auto bytes = QByteArray();
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	writeHeader(stream, _session->userId());
	using Flag = MTPDmessages_dialogFilters::Flag;
	stream << serializePeers(peers)
		<< encode(MTPmessages_Dialogs(MTP_messages_dialogs(
			MTP_vector<MTPDialog>(dialogs),
			MTP_vector<MTPMessage>(messages),
			MTP_vector<MTPChat>(), MTP_vector<MTPUser>())))
		<< encode(MTPmessages_DialogFilters(MTP_messages_dialogFilters(
			MTP_flags(filters.tagsEnabled() ? Flag::f_tags_enabled : Flag()),
			MTP_vector<MTPDialogFilter>(filterList))));
	_session->local().writePref<QByteArray>(kSnapshotPref, std::move(bytes));
	_session->local().writePref<QByteArray>(kListMetadataPref, std::move(metadata));
}

void ConversationCache::dialogReceived(const MTPDdialog &dialog) {
	if (!_enabled || _restoring) {
		return;
	}
	const auto peer = peerFromMTP(dialog.vpeer());
	const auto history = _session->data().history(peer);
	_dialogs[peer] = makeDialog(history, dialog);
	_restoredDialogs.remove(peer);
	scheduleSave();
}

void ConversationCache::dialogsDone(Data::Folder *folder) {
	const auto folderId = folder ? folder->id() : 0;
	auto stale = std::vector<PeerId>();
	for (const auto &[peer, cachedFolder] : _restoredDialogs) {
		if (cachedFolder == folderId) {
			stale.push_back(peer);
		}
	}
	for (const auto peer : stale) {
		_session->data().removeChatListEntry(_session->data().history(peer));
		_restoredDialogs.remove(peer);
		_dialogs.remove(peer);
		historyCleared(peer);
	}
	scheduleSave();
}

QByteArray ConversationCache::serializePeers(
		const base::flat_set<PeerId> &peers) const {
	auto bytes = QByteArray();
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	for (const auto peerId : peers) {
		if (const auto peer = _session->data().peerLoaded(peerId)) {
			Serialize::writePeer(stream, peer);
		}
	}
	return bytes;
}

base::flat_set<PeerId> ConversationCache::restorePeers(const QByteArray &bytes) {
	auto result = base::flat_set<PeerId>();
	QDataStream stream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	while (!stream.atEnd() && stream.status() == QDataStream::Ok) {
		const auto peer = Serialize::readPeer(_session, AppVersion, stream);
		if (!peer) {
			break;
		}
		result.emplace(peer->id);
	}
	return result;
}

void ConversationCache::withIndex(Fn<void()> done) {
	if (_indexReady) {
		done();
	} else {
		_afterIndex.push_back(std::move(done));
	}
}

void ConversationCache::saveIndex() {
	if (!_indexDirty || !_indexReady) {
		return;
	}
	_indexDirty = false;
	auto bytes = QByteArray();
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	writeHeader(stream, _session->userId());
	stream << qint32(_historyKeys.size());
	for (const auto &key : _historyKeys) {
		stream << SerializePeerId(key.peer) << qint64(key.topicRootId.bare)
			<< SerializePeerId(key.sublistPeer);
	}
	stream << qint32(_nonChannelPeers.size());
	for (const auto &[id, peer] : _nonChannelPeers) {
		stream << qint64(id.bare) << SerializePeerId(peer);
	}
	_database->put({ 1, 0 }, std::move(bytes));
}

void ConversationCache::clear() {
	disable();
	_session->local().clearPref(kSnapshotPref);
	_session->local().clearPref(kListMetadataPref);
	_database->clear();
}

void ConversationCache::storageStats(Fn<void(StorageStats)> done) {
	withIndex([=] {
		flush();
		const auto keys = std::vector<HistoryCacheKey>(
			_historyKeys.begin(), _historyKeys.end());
		auto storageKeys = std::vector<Storage::Cache::Key>();
		storageKeys.push_back({ 1, 0 });
		for (const auto key : keys) {
			storageKeys.push_back(storageKey(key));
		}
		_database->putIfEmpty({ 0, 1 }, QByteArray(1, '\0'));
		const auto ready = crl::guard(_session, [=](std::vector<int> sizes) {
			if (sizes.size() != keys.size() + 1) {
				done({ .error = { Storage::Cache::Error::Type::IO, {} } });
				return;
			}
			auto result = StorageStats();
			result.bytes = _session->local().readPref<QByteArray>(kSnapshotPref).size();
			result.bytes += _session->local().readPref<QByteArray>(kListMetadataPref).size();
			for (const auto size : sizes) {
				result.bytes += size;
			}
			for (auto i = 0; i != int(keys.size()); ++i) {
				if (!sizes[i + 1]) {
					continue;
				}
				auto &usage = result.chats[keys[i].peer];
				usage.first += sizes[i + 1];
				++usage.second;
				++result.count;
			}
			done(std::move(result));
		});
		_database->getWithSizes({ 0, 1 }, std::move(storageKeys), [=](
				QByteArray &&, std::vector<int> &&sizes) {
			crl::on_main([ready, sizes = std::move(sizes)]() mutable {
				ready(std::move(sizes));
			});
		});
	});
}

void ConversationCache::clearStorage(
		PeerId peer,
		Fn<void(Storage::Cache::Error)> done) {
	withIndex([=] {
		_storageClearing = true;
		_saveTimer.cancel();
		auto keys = std::vector<HistoryCacheKey>();
		auto cancelled = std::vector<HistoryEntry::Pending>();
		for (auto i = _histories.begin(); i != _histories.end();) {
			if (peer && i->first.peer != peer) {
				++i;
				continue;
			}
			for (auto &pending : i->second.pending) {
				cancelled.push_back(std::move(pending));
			}
			i = _histories.erase(i);
		}
		for (auto i = _historyKeys.begin(); i != _historyKeys.end();) {
			if (peer && i->peer != peer) {
				++i;
				continue;
			}
			keys.push_back(*i);
			i = _historyKeys.erase(i);
		}
		for (auto i = _nonChannelPeers.begin(); i != _nonChannelPeers.end();) {
			i = !peer || i->second == peer ? _nonChannelPeers.erase(i) : std::next(i);
		}
		if (peer) {
			_lastMessages.remove(peer);
		} else {
			_lastMessages.clear();
		}
		// 旧读取先失效，再完成请求，避免已清理正文写回新缓存。
		for (auto &pending : cancelled) {
			if (pending.cancelled) {
				pending.cancelled();
			}
		}
		if (_enabled) {
			_session->local().clearPref(kSnapshotPref);
			saveDialogs();
		}
		const auto finish = [=](Storage::Cache::Error error) {
			_storageClearing = false;
			_indexDirty = true;
			if (_enabled) {
				saveIndex();
			}
			done(error);
		};
		if (!_enabled) {
			_database->clear(onMain<Storage::Cache::Error>(_session, finish));
			return;
		}
		if (keys.empty()) {
			finish({});
			return;
		}
		const auto remaining = std::make_shared<int>(keys.size());
		const auto result = std::make_shared<Storage::Cache::Error>();
		for (const auto key : keys) {
			_database->remove(storageKey(key), onMain<Storage::Cache::Error>(
				_session, [=](Storage::Cache::Error error) {
				if (error.type != Storage::Cache::Error::Type::None) {
					*result = error;
					_historyKeys.emplace(key);
				}
				if (--*remaining == 0) {
					finish(*result);
				}
			}));
		}
	});
}

void ConversationCache::disable() {
	_enabled = false;
	_saveTimer.cancel();
	_lifetime.destroy();
}

bool ConversationCache::hasDialogsSnapshot() const {
	return _hasDialogsSnapshot;
}

ConversationCache::Stats ConversationCache::stats() const {
	auto messages = 0;
	for (const auto &[key, entry] : _histories) {
		messages += entry.messages.size();
	}
	return {
		.restoredDialogs = _restoredCount,
		.histories = int(_historyKeys.size()),
		.loadedMessages = messages,
		.hits = _hits,
		.misses = _misses,
		.messageDataHits = _messageDataHits,
		.available = _enabled && _ready && _indexReady,
	};
}

} // namespace Extras
