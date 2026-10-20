#include "extras/data/media_cache_index.h"

#include "storage/cache/storage_cache_binlog_reader.h"
#include "storage/storage_encrypted_file.h"

namespace Extras {

MediaCacheIndexResult readMediaCacheIndex(
		const QString &path,
		const Storage::EncryptionKey &key,
		const Storage::Cache::details::Settings &settings) {
	using namespace Storage::Cache::details;
	const auto base = ComputeBasePath(path);
	const auto version = ReadVersionValue(base);
	if (!version) {
		return {};
	}
	const auto binlogPath = base + QString::number(*version) + u"/binlog"_q;
	auto file = Storage::File();
	if (file.open(binlogPath, Storage::File::Mode::Read, key)
			!= Storage::File::Result::Success
		|| !BinlogWrapper::ReadHeader(file, settings)) {
		return { {}, { Storage::Cache::Error::Type::IO, binlogPath } };
	}
	auto entries = base::flat_map<Storage::Cache::Key, uint8>();
	auto wrapper = BinlogWrapper(file, settings);
	auto reader = BinlogReader<
		StoreWithTime,
		MultiStoreWithTime,
		MultiRemove,
		MultiAccess>(wrapper);
	const auto store = [&](const StoreWithTime &record) {
		entries[record.key] = record.tag;
		return true;
	};
	while (!reader.readTillEnd(store,
			[&](const MultiStoreWithTime &, const auto &next) {
				while (const auto record = next()) {
					store(*record);
				}
				return true;
			}, [&](const MultiRemove &, const auto &next) {
				while (const auto record = next()) {
					entries.remove(*record);
				}
				return true;
			}, [](const MultiAccess &, const auto &) { return true; })) {
	}
	if (wrapper.failed()) {
		return { {}, { Storage::Cache::Error::Type::IO, binlogPath } };
	}
	auto result = MediaCacheIndexResult();
	result.entries.reserve(entries.size());
	for (const auto &[entryKey, tag] : entries) {
		result.entries.push_back({ entryKey, tag });
	}
	return result;
}

} // namespace Extras
