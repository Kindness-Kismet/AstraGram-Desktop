#pragma once

#include "base/timer.h"
#include "data/data_file_origin.h"
#include "storage/cache/storage_cache_types.h"
#include <rpl/variable.h>
#include <array>
#include <map>

class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Storage::Cache {
class Database;
} // namespace Storage::Cache

namespace Extras {

enum class MediaCacheCategory {
	Images,
	Stickers,
	Documents,
	Avatars,
	Other,
	Messages,
	Count,
};

enum class MediaCacheChatType {
	Private,
	Groups,
	Channels,
	Stories,
	Count,
};

using MediaCacheMask = uint32;
inline constexpr auto kMediaCacheCategoryCount = int(MediaCacheCategory::Count);
inline constexpr auto kMediaCacheChatTypeCount = int(MediaCacheChatType::Count);

[[nodiscard]] constexpr MediaCacheMask mediaCacheBit(MediaCacheCategory category) {
	return MediaCacheMask(1) << int(category);
}

inline constexpr auto kAllMediaCacheMask
	= mediaCacheBit(MediaCacheCategory::Messages) - 1;

struct MediaCacheUsage {
	int64 bytes = 0;
	int count = 0;
};

using MediaCacheUsageList = std::array<MediaCacheUsage, kMediaCacheCategoryCount>;

struct MediaCacheChat {
	PeerId peer;
	MediaCacheUsageList categories;
	int64 bytes = 0;
};

struct MediaCacheSnapshot {
	MediaCacheUsageList categories;
	MediaCacheUsageList unassigned;
	std::vector<MediaCacheChat> chats;
	Storage::Cache::Error error;
};

struct MediaCacheClearResult {
	int64 freedBytes = 0;
	Storage::Cache::Error error;
};

class MediaCacheManager final {
public:
	explicit MediaCacheManager(not_null<Main::Session*> session);
	~MediaCacheManager();

	void track(
		Storage::Cache::Key key,
		bool bigFile,
		Data::FileOrigin origin,
		uint8 tag,
		std::optional<MediaCacheCategory> category = std::nullopt);
	void trackDocument(not_null<DocumentData*> document, Data::FileOrigin origin);
	void trackStreaming(
		Storage::Cache::Key baseKey,
		int64 size,
		Data::FileOrigin origin,
		MediaCacheCategory category);
	[[nodiscard]] Fn<void(Storage::Cache::Key)> streamingCallback(
		Data::FileOrigin origin,
		MediaCacheCategory category);

	void snapshot(Fn<void(MediaCacheSnapshot)> done);
	[[nodiscard]] bool clear(
		MediaCacheMask mask,
		PeerId peer,
		Fn<void(MediaCacheClearResult)> done);
	[[nodiscard]] bool clearing() const;
	void prune();
	void flush();
	void disable();

	[[nodiscard]] rpl::producer<int> retentionValue(MediaCacheChatType type) const;
	[[nodiscard]] int retentionDays(MediaCacheChatType type) const;
	void setRetentionDays(MediaCacheChatType type, int days);
	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] static MediaCacheCategory documentCategory(
		not_null<DocumentData*> document);

private:
	struct Key {
		bool bigFile = false;
		Storage::Cache::Key storage;
		friend inline auto operator<=>(const Key &a, const Key &b) {
			return std::tie(a.bigFile, a.storage.high, a.storage.low)
				<=> std::tie(b.bigFile, b.storage.high, b.storage.low);
		}
	};
	struct Entry {
		MediaCacheCategory category = MediaCacheCategory::Other;
		std::map<std::pair<PeerId, MediaCacheChatType>, int64> owners;
		int size = 0;
	};

	void restore();
	void enumerate(Fn<void(Storage::Cache::Error)> done);
	void requestSnapshot(Fn<void(MediaCacheSnapshot)> done);
	void finishSnapshot(MediaCacheSnapshot snapshot);
	void finishClearing();
	void refreshSizes(Fn<void(Storage::Cache::Error)> done);
	void refreshDatabaseSizes(
		bool bigFile,
		Fn<void(Storage::Cache::Error)> done);
	void clearKeys(std::vector<Key> keys, Fn<void(MediaCacheClearResult)> done);
	[[nodiscard]] bool selectForClearing(Entry &entry, PeerId peer);
	void scheduleSave();
	[[nodiscard]] MediaCacheSnapshot collectSnapshot() const;
	[[nodiscard]] Storage::Cache::Database &database(bool bigFile) const;
	[[nodiscard]] MediaCacheCategory classify(
		Storage::Cache::Key key,
		bool bigFile,
		uint8 tag) const;
	[[nodiscard]] std::optional<std::pair<PeerId, MediaCacheChatType>> owner(
		const Data::FileOrigin &origin) const;

	const not_null<Main::Session*> _session;
	std::map<Key, Entry> _entries;
	std::array<rpl::variable<int>, kMediaCacheChatTypeCount> _retention;
	base::Timer _saveTimer;
	base::Timer _pruneTimer;
	rpl::event_stream<> _changes;
	std::vector<Fn<void(MediaCacheSnapshot)>> _snapshotCallbacks;
	std::vector<Fn<void(MediaCacheSnapshot)>> _waitingSnapshots;
	bool _dirty = false;
	bool _pruning = false;
	bool _clearing = false;
	bool _refreshing = false;
	bool _disabled = false;
};

} // namespace Extras
