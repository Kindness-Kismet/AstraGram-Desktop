#include "extras/data/media_cache_manager.h"

#include "extras/data/conversation_cache.h"
#include "extras/data/conversation_cache_serialization.h"
#include "extras/data/media_cache_index.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "storage/cache/storage_cache_database.h"
#include "base/unixtime.h"
#include <QtCore/QDataStream>

namespace Extras {
namespace {

constexpr auto kIndexPref = "media-cache-index";
constexpr auto kRetentionPref = "media-cache-retention";
constexpr auto kIndexFormat = quint32(1);
constexpr auto kQueryKey = Storage::Cache::Key{ 0, 1 };
constexpr auto kSweepInterval = crl::time(60 * 60 * 1000);
constexpr auto kClearBatchSize = 32;
// 每个账号只保留最近使用的清晰原图，缩放缓存和播放资源仍随视图释放。
constexpr auto kDecodedPhotoLimit = 32 * 1024 * 1024;

template <typename Callback>
[[nodiscard]] auto onMain(not_null<Main::Session*> session, Callback callback) {
	const auto guarded = crl::guard(session, std::move(callback));
	return [guarded](auto value) {
		crl::on_main([guarded, value = std::move(value)]() mutable {
			guarded(std::move(value));
		});
	};
}

void addUsage(MediaCacheUsage &usage, int64 size) {
	usage.bytes += size;
	++usage.count;
}

} // namespace

MediaCacheManager::MediaCacheManager(not_null<Main::Session*> session)
: _session(session)
, _decodedPhotos(kDecodedPhotoLimit)
, _saveTimer([=] { flush(); })
, _pruneTimer([=] { prune(); }) {
	restore();
	_pruneTimer.callOnce(kSweepInterval);
}

MediaCacheManager::~MediaCacheManager() {
	flush();
}

Storage::Cache::Database &MediaCacheManager::database(bool bigFile) const {
	return bigFile ? _session->data().cacheBigFile() : _session->data().cache();
}

void MediaCacheManager::restore() {
	auto bytes = _session->local().readPref<QByteArray>(kRetentionPref);
	auto retentionStream = QDataStream(bytes);
	for (auto &value : _retention) {
		auto days = qint32();
		if (!retentionStream.atEnd()) {
			retentionStream >> days;
		}
		value = days;
	}
	bytes = _session->local().readPref<QByteArray>(kIndexPref);
	if (bytes.isEmpty()) {
		return;
	}
	auto stream = QDataStream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto format = quint32();
	auto count = qint32();
	stream >> format >> count;
	if (format != kIndexFormat) {
		return;
	}
	for (auto i = 0; i < count && !stream.atEnd(); ++i) {
		auto key = Key();
		auto category = qint32();
		auto owners = qint32();
		stream >> key.bigFile >> key.storage.high >> key.storage.low
			>> category >> owners;
		auto entry = Entry();
		entry.category = MediaCacheCategory(category);
		for (auto j = 0; j < owners && !stream.atEnd(); ++j) {
			auto peer = quint64();
			auto type = qint32();
			auto used = qint64();
			stream >> peer >> type >> used;
			if (type < 0 || type >= kMediaCacheChatTypeCount) {
				stream.setStatus(QDataStream::ReadCorruptData);
				break;
			}
			entry.owners.emplace(std::make_pair(
				DeserializePeerId(peer), MediaCacheChatType(type)), used);
		}
		if (category < 0 || category >= int(MediaCacheCategory::Messages)) {
			stream.setStatus(QDataStream::ReadCorruptData);
			break;
		}
		_entries.emplace(key, std::move(entry));
	}
	if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
		_entries.clear();
	}
}

void MediaCacheManager::scheduleSave() {
	if (_disabled) {
		return;
	}
	_dirty = true;
	if (!_saveTimer.isActive()) {
		_saveTimer.callOnce(1000);
	}
}

void MediaCacheManager::flush() {
	_saveTimer.cancel();
	if (!_dirty || _disabled) {
		return;
	}
	_dirty = false;
	auto bytes = QByteArray();
	auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kIndexFormat << qint32(_entries.size());
	for (const auto &[key, entry] : _entries) {
		stream << key.bigFile << key.storage.high << key.storage.low
			<< qint32(entry.category) << qint32(entry.owners.size());
		for (const auto &[owner, used] : entry.owners) {
			stream << SerializePeerId(owner.first) << qint32(owner.second)
				<< qint64(used);
		}
	}
	_session->local().writePref<QByteArray>(kIndexPref, std::move(bytes));
}

void MediaCacheManager::disable() {
	_disabled = true;
	_decodedPhotos.clear();
	_dirty = false;
	_saveTimer.cancel();
	_pruneTimer.cancel();
	_session->local().clearPref(kIndexPref);
	_session->local().clearPref(kRetentionPref);
}

DecodedPhoto MediaCacheManager::decodedPhoto(Storage::Cache::Key key) const {
	const auto photo = _decodedPhotos.object({ key.high, key.low });
	return photo ? *photo : DecodedPhoto();
}

void MediaCacheManager::rememberDecodedPhoto(
		Storage::Cache::Key key,
		DecodedPhoto photo) {
	if (_disabled || !key || photo.image.isNull()) {
		return;
	}
	const auto cost = photo.image.sizeInBytes() + photo.bytes.size();
	_decodedPhotos.insert(
		{ key.high, key.low },
		new DecodedPhoto(std::move(photo)),
		cost);
}

void MediaCacheManager::forgetDecodedPhoto(Storage::Cache::Key key) {
	_decodedPhotos.remove({ key.high, key.low });
}

#ifdef _DEBUG
MediaCacheUsage MediaCacheManager::decodedPhotoUsage() const {
	return { _decodedPhotos.totalCost(), int(_decodedPhotos.size()) };
}
#endif

std::optional<std::pair<PeerId, MediaCacheChatType>> MediaCacheManager::owner(
		const Data::FileOrigin &origin) const {
	return v::match(origin.data, [&](const Data::FileOriginStory &value)
			-> std::optional<std::pair<PeerId, MediaCacheChatType>> {
		return std::make_pair(value.peer, MediaCacheChatType::Stories);
	}, [&](const auto &value)
			-> std::optional<std::pair<PeerId, MediaCacheChatType>> {
		auto id = PeerId();
		using Type = std::decay_t<decltype(value)>;
		if constexpr (std::is_same_v<Type, Data::FileOriginMessage>) {
			id = value.peer;
		} else if constexpr (std::is_same_v<Type, Data::FileOriginPeerPhoto>) {
			id = value.peerId;
		} else if constexpr (std::is_same_v<Type, Data::FileOriginCloudDraft>) {
			id = value.peerId;
		} else if constexpr (std::is_same_v<Type, Data::FileOriginUserPhoto>
			|| std::is_same_v<Type, Data::FileOriginFullUser>) {
			id = peerFromUser(value.userId);
		}
		if (!id) {
			return std::nullopt;
		}
		const auto peer = _session->data().peer(id);
		const auto channel = peer->asChannel();
		const auto type = peer->isUser() ? MediaCacheChatType::Private
			: channel && channel->isBroadcast() ? MediaCacheChatType::Channels
			: MediaCacheChatType::Groups;
		return std::make_pair(id, type);
	});
}

MediaCacheCategory MediaCacheManager::classify(
		Storage::Cache::Key key,
		bool bigFile,
		uint8 tag) const {
	if (tag == Data::kStickerCacheTag) {
		return MediaCacheCategory::Stickers;
	}
	if (bigFile) {
		return ((key.high >> 16) & 0xFF) == 8
			? MediaCacheCategory::Stickers : MediaCacheCategory::Other;
	}
	const auto locationType = (key.high >> 8) & 0xFF;
	if (tag == Data::kImageCacheTag && locationType == 9) {
		return MediaCacheCategory::Avatars;
	}
	if (tag == Data::kImageCacheTag && locationType == 10) {
		return MediaCacheCategory::Stickers;
	}
	if (tag == Data::kImageCacheTag) {
		return MediaCacheCategory::Images;
	}
	if (tag == Data::kVoiceMessageCacheTag
		|| tag == Data::kVideoMessageCacheTag
		|| tag == Data::kAnimationCacheTag) {
		return MediaCacheCategory::Other;
	}
	return locationType == 1 ? MediaCacheCategory::Documents
		: MediaCacheCategory::Other;
}

MediaCacheCategory MediaCacheManager::documentCategory(
		not_null<DocumentData*> document) {
	return document->sticker() ? MediaCacheCategory::Stickers
		: (document->isVideoFile() || document->isAudioFile()
			|| document->isVoiceMessage() || document->isVideoMessage()
			|| document->isAnimation()) ? MediaCacheCategory::Other
		: document->isWallPaper() ? MediaCacheCategory::Images
		: MediaCacheCategory::Documents;
}

void MediaCacheManager::track(
		Storage::Cache::Key key,
		bool bigFile,
		Data::FileOrigin origin,
		uint8 tag,
		std::optional<MediaCacheCategory> category) {
	if (_disabled || !key || key == kQueryKey) {
		return;
	}
	if (!category && (v::is<Data::FileOriginPeerPhoto>(origin.data)
		|| v::is<Data::FileOriginUserPhoto>(origin.data)
		|| v::is<Data::FileOriginFullUser>(origin.data))) {
		category = MediaCacheCategory::Avatars;
	}
	auto [i, added] = _entries.try_emplace(Key{ bigFile, key });
	auto &entry = i->second;
	if (added || category) {
		entry.category = category.value_or(classify(key, bigFile, tag));
	}
	if (const auto source = owner(origin)) {
		entry.owners[*source] = base::unixtime::now();
	}
	scheduleSave();
}

void MediaCacheManager::trackDocument(
		not_null<DocumentData*> document,
	Data::FileOrigin origin) {
	const auto category = documentCategory(document);
	track(document->cacheKey(), false, origin, document->cacheTag(), category);
	track(document->thumbnailLocation().file().cacheKey(), false, origin,
		Data::kImageCacheTag, category);
	track(document->videoThumbnailLocation().file().cacheKey(), false, origin,
		Data::kAnimationCacheTag, category);
	track(document->goodThumbnailCacheKey(), false, origin,
		Data::kImageCacheTag, category);
}

void MediaCacheManager::trackStreaming(
		Storage::Cache::Key baseKey,
		int64 size,
		Data::FileOrigin origin,
		MediaCacheCategory category) {
	if (!baseKey) {
		return;
	}
	constexpr auto kSliceSize = int64(8 * 1024 * 1024);
	for (auto i = 0; i <= (size + kSliceSize - 1) / kSliceSize; ++i) {
		track({ baseKey.high, baseKey.low + i }, true, origin, 0, category);
	}
}

Fn<void(Storage::Cache::Key)> MediaCacheManager::streamingCallback(
		Data::FileOrigin origin,
		MediaCacheCategory category) {
	return onMain(_session, [=](Storage::Cache::Key key) {
		track(key, true, origin, 0, category);
	});
}

void MediaCacheManager::enumerate(Fn<void(Storage::Cache::Error)> done) {
	const auto paths = std::array{
		_session->local().cachePath(), _session->local().cacheBigFilePath() };
	const auto settings = std::array{
		_session->local().cacheSettings(), _session->local().cacheBigFileSettings() };
	const auto encryption = _session->local().cacheKey();
	const auto ready = onMain(_session, [=](std::array<MediaCacheIndexResult, 2> results) {
		if (_disabled) {
			done({ Storage::Cache::Error::Type::IO, {} });
			return;
		}
		auto error = Storage::Cache::Error();
		for (auto index = 0; index != 2; ++index) {
			if (results[index].error.type != Storage::Cache::Error::Type::None) {
				error = results[index].error;
			}
			for (const auto &record : results[index].entries) {
				if (record.key == kQueryKey) {
					continue;
				}
				const auto key = Key{ index != 0, record.key };
				if (!_entries.contains(key)) {
					_entries.emplace(key, Entry{
						.category = classify(record.key, index != 0, record.tag) });
				}
			}
		}
		done(error);
	});
	crl::async([=] {
		auto results = std::array<MediaCacheIndexResult, 2>();
		for (auto i = 0; i != 2; ++i) {
			results[i] = readMediaCacheIndex(paths[i], encryption, settings[i]);
		}
		ready(std::move(results));
	});
}

void MediaCacheManager::refreshDatabaseSizes(
		bool bigFile,
		Fn<void(Storage::Cache::Error)> done) {
	auto keys = std::vector<Storage::Cache::Key>();
	for (const auto &[key, entry] : _entries) {
		if (key.bigFile == bigFile) {
			keys.push_back(key.storage);
		}
	}
	const auto ready = onMain(_session, [=](std::vector<int> sizes) {
		if (_disabled) {
			done({ Storage::Cache::Error::Type::IO, {} });
			return;
		}
		if (sizes.size() != keys.size()) {
			done({ Storage::Cache::Error::Type::IO, {} });
			return;
		}
		for (auto i = 0; i != int(keys.size()); ++i) {
			const auto key = Key{ bigFile, keys[i] };
			auto &entry = _entries.at(key);
			if (sizes[i]) {
				entry.size = sizes[i];
			} else if (entry.size) {
				_entries.erase(key);
			}
		}
		scheduleSave();
		done({});
	});
	// 只读取查询键的内容，媒体键只查大小，保留其真实使用时间。
	auto &cache = database(bigFile);
	cache.putIfEmpty(kQueryKey, QByteArray(1, '\0'));
	cache.getWithSizes(kQueryKey, std::move(keys), [ready](
			QByteArray &&, std::vector<int> &&sizes) {
		ready(std::move(sizes));
	});
}

void MediaCacheManager::refreshSizes(Fn<void(Storage::Cache::Error)> done) {
	refreshDatabaseSizes(false, [=](Storage::Cache::Error first) {
		if (first.type != Storage::Cache::Error::Type::None) {
			done(first);
			return;
		}
		refreshDatabaseSizes(true, [=](Storage::Cache::Error second) {
			done(second);
		});
	});
}

MediaCacheSnapshot MediaCacheManager::collectSnapshot() const {
	auto result = MediaCacheSnapshot();
	auto chats = std::map<PeerId, MediaCacheChat>();
	for (const auto &[key, entry] : _entries) {
		if (!entry.size) {
			continue;
		}
		const auto category = int(entry.category);
		addUsage(result.categories[category], entry.size);
		if (entry.owners.empty()) {
			addUsage(result.unassigned[category], entry.size);
			continue;
		}
		// 一个实体文件只计入一个聊天，共享文件保留全部归属用于清理判断。
		const auto peer = entry.owners.begin()->first.first;
		auto &chat = chats[peer];
		chat.peer = peer;
		chat.bytes += entry.size;
		addUsage(chat.categories[category], entry.size);
	}
	for (auto &[peer, chat] : chats) {
		result.chats.push_back(std::move(chat));
	}
	return result;
}

void MediaCacheManager::snapshot(Fn<void(MediaCacheSnapshot)> done) {
	if (_disabled) {
		done({ .error = { Storage::Cache::Error::Type::IO, {} } });
		return;
	}
	if (_clearing || _pruning) {
		_waitingSnapshots.push_back(std::move(done));
		return;
	}
	requestSnapshot(std::move(done));
}

void MediaCacheManager::requestSnapshot(Fn<void(MediaCacheSnapshot)> done) {
	_snapshotCallbacks.push_back(std::move(done));
	if (_refreshing) {
		return;
	}
	_refreshing = true;
	enumerate([=](Storage::Cache::Error indexError) {
		if (indexError.type != Storage::Cache::Error::Type::None) {
			finishSnapshot({ .error = indexError });
			return;
		}
		refreshSizes([=](Storage::Cache::Error sizesError) {
			if (sizesError.type != Storage::Cache::Error::Type::None) {
				finishSnapshot({ .error = sizesError });
				return;
			}
			auto result = collectSnapshot();
			_session->conversationCache().storageStats([=](
					ConversationCache::StorageStats messages) mutable {
				if (messages.error.type != Storage::Cache::Error::Type::None) {
					finishSnapshot({ .error = messages.error });
					return;
				}
				result.categories[int(MediaCacheCategory::Messages)] = {
					messages.bytes, messages.count };
				for (const auto &[peer, usage] : messages.chats) {
					auto i = ranges::find(result.chats, peer, &MediaCacheChat::peer);
					if (i == result.chats.end()) {
						result.chats.push_back({ .peer = peer });
						i = std::prev(result.chats.end());
					}
					i->categories[int(MediaCacheCategory::Messages)] = {
						usage.first, usage.second };
					i->bytes += usage.first;
				}
				ranges::sort(result.chats, ranges::greater(), &MediaCacheChat::bytes);
				finishSnapshot(std::move(result));
			});
		});
	});
}

void MediaCacheManager::finishSnapshot(MediaCacheSnapshot snapshot) {
	_refreshing = false;
	for (auto &callback : base::take(_snapshotCallbacks)) {
		callback(snapshot);
	}
}

struct MediaCacheManager::ClearOperation {
	std::vector<Key> keys;
	size_t next = 0;
	int pending = 0;
	MediaCacheClearResult result;
	Fn<void(MediaCacheClearResult)> done;
};

void MediaCacheManager::clearKeys(
		std::vector<Key> keys,
		Fn<void(MediaCacheClearResult)> done) {
	if (keys.empty()) {
		done({});
		return;
	}
	const auto operation = std::make_shared<ClearOperation>();
	operation->keys = std::move(keys);
	operation->done = std::move(done);
	clearNextBatch(operation);
}

void MediaCacheManager::clearNextBatch(
		const std::shared_ptr<ClearOperation> &operation) {
	if (_disabled) {
		operation->result.error = { Storage::Cache::Error::Type::IO, {} };
		operation->done(operation->result);
		return;
	}
	if (operation->next == operation->keys.size()) {
		scheduleSave();
		_changes.fire({});
		operation->done(operation->result);
		return;
	}
	operation->pending = std::min(
		size_t(kClearBatchSize),
		operation->keys.size() - operation->next);
	const auto till = operation->next + operation->pending;
	while (operation->next != till) {
		const auto key = operation->keys[operation->next++];
		const auto size = _entries.at(key).size;
		database(key.bigFile).remove(key.storage, onMain(_session, [=](
				Storage::Cache::Error error) {
			if (error.type == Storage::Cache::Error::Type::None) {
				_entries.erase(key);
				if (!key.bigFile) {
					forgetDecodedPhoto(key.storage);
				}
				operation->result.freedBytes += size;
			} else {
				operation->result.error = error;
			}
			if (--operation->pending == 0) {
				clearNextBatch(operation);
			}
		}));
	}
}

bool MediaCacheManager::selectForClearing(Entry &entry, PeerId peer) {
	if (!peer) {
		return true;
	}
	if (!ranges::any_of(entry.owners, [&](const auto &owner) {
		return owner.first.first == peer;
	})) {
		return false;
	}
	if (!ranges::any_of(entry.owners, [&](const auto &owner) {
		return owner.first.first != peer;
	})) {
		return true;
	}
	for (auto i = entry.owners.begin(); i != entry.owners.end();) {
		i = i->first.first == peer ? entry.owners.erase(i) : std::next(i);
	}
	return false;
}

bool MediaCacheManager::clear(
		MediaCacheMask mask,
		PeerId peer,
		Fn<void(MediaCacheClearResult)> done) {
	if (_disabled || _clearing || _pruning) {
		return false;
	}
	_clearing = true;
	_changes.fire({});
	requestSnapshot([=](MediaCacheSnapshot snapshot) {
		if (snapshot.error.type != Storage::Cache::Error::Type::None) {
			finishClearing();
			done({ 0, snapshot.error });
			return;
		}
		auto keys = std::vector<Key>();
		for (auto &[key, entry] : _entries) {
			if (!entry.size || !(mask & mediaCacheBit(entry.category))) {
				continue;
			}
			if (!selectForClearing(entry, peer)) {
				continue;
			}
			keys.push_back(key);
		}
		const auto messageBytes = snapshot.categories[int(MediaCacheCategory::Messages)].bytes;
		clearKeys(std::move(keys), [=](MediaCacheClearResult result) {
			if (_disabled || !(mask & mediaCacheBit(MediaCacheCategory::Messages))) {
				finishClearing();
				done(result);
				return;
			}
			_session->conversationCache().clearStorage(peer, [=](
					Storage::Cache::Error error) mutable {
				if (error.type != Storage::Cache::Error::Type::None) {
					result.error = error;
				}
				_session->conversationCache().storageStats([=](
						ConversationCache::StorageStats remaining) mutable {
					if (remaining.error.type != Storage::Cache::Error::Type::None) {
						result.error = remaining.error;
						finishClearing();
						done(result);
						return;
					}
					result.freedBytes += std::max(int64(0), messageBytes - remaining.bytes);
					finishClearing();
					done(result);
				});
			});
		});
	});
	return true;
}

bool MediaCacheManager::clearing() const {
	return _clearing || _pruning;
}

void MediaCacheManager::finishClearing() {
	_clearing = false;
	scheduleSave();
	for (auto &callback : base::take(_waitingSnapshots)) {
		snapshot(std::move(callback));
	}
	_changes.fire({});
}

rpl::producer<int> MediaCacheManager::retentionValue(MediaCacheChatType type) const {
	return _retention[int(type)].value();
}

int MediaCacheManager::retentionDays(MediaCacheChatType type) const {
	return _retention[int(type)].current();
}

void MediaCacheManager::setRetentionDays(MediaCacheChatType type, int days) {
	Expects(days >= 0);
	_retention[int(type)] = days;
	auto bytes = QByteArray();
	auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
	for (const auto &value : _retention) {
		stream << qint32(value.current());
	}
	_session->local().writePref<QByteArray>(kRetentionPref, std::move(bytes));
	_changes.fire({});
	prune();
}

rpl::producer<> MediaCacheManager::changes() const {
	return _changes.events();
}

void MediaCacheManager::prune() {
	if (_disabled) {
		return;
	}
	_pruneTimer.callOnce(kSweepInterval);
	if (_pruning || _clearing || _refreshing || ranges::all_of(_retention, [](const auto &value) {
		return value.current() == 0;
	})) {
		return;
	}
	_pruning = true;
	_changes.fire({});
	refreshSizes([=](Storage::Cache::Error error) {
		if (error.type != Storage::Cache::Error::Type::None) {
			LOG(("Media cache: automatic size query failed (%1).").arg(int(error.type)));
			_pruning = false;
			finishClearing();
			return;
		}
		const auto now = base::unixtime::now();
		auto keys = std::vector<Key>();
		for (const auto &[key, entry] : _entries) {
			if (!entry.size || entry.owners.empty()) {
				continue;
			}
			const auto expired = ranges::all_of(entry.owners, [&](const auto &owner) {
				const auto days = retentionDays(owner.first.second);
				return days && owner.second <= now - int64(days) * 24 * 60 * 60;
			});
			if (expired) {
				keys.push_back(key);
			}
		}
		clearKeys(std::move(keys), [=](MediaCacheClearResult result) {
			if (result.error.type != Storage::Cache::Error::Type::None) {
				LOG(("Media cache: automatic clearing failed (%1).").arg(int(result.error.type)));
			}
			_pruning = false;
			finishClearing();
		});
	});
}

} // namespace Extras
