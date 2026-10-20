#pragma once

#include "storage/cache/storage_cache_types.h"
#include "storage/storage_encryption.h"

namespace Extras {

struct MediaCacheIndexEntry {
	Storage::Cache::Key key;
	uint8 tag = 0;
};

struct MediaCacheIndexResult {
	std::vector<MediaCacheIndexEntry> entries;
	Storage::Cache::Error error;
};

[[nodiscard]] MediaCacheIndexResult readMediaCacheIndex(
	const QString &path,
	const Storage::EncryptionKey &key,
	const Storage::Cache::details::Settings &settings);

} // namespace Extras
