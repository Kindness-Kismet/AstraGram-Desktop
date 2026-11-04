#include "extras/data/download_file_index.h"

#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "logs.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>

namespace Extras {
namespace {

constexpr auto kFormat = quint32(1);

} // namespace

DownloadFileIndex::DownloadFileIndex(
	QString name,
	QString basePath,
	MTP::AuthKeyPtr key)
: _name(std::move(name))
, _basePath(std::move(basePath))
, _key(std::move(key))
, _saveTimer([=] { flush(); }) {
	Expects(_key != nullptr);

	restore();
}

DownloadFileIndex::~DownloadFileIndex() {
	flush();
}

bool DownloadFileIndex::accepts(
		MediaKey key,
		const Core::FileLocation &location) const {
	if (!key.second || !uint32(key.first)
		|| location.isEmpty() || location.inMediaCache()) {
		return false;
	}
	const auto relative = QDir(_basePath).relativeFilePath(
		QFileInfo(location.name()).absoluteFilePath());
	return !relative.startsWith(u"temp_"_q);
}

void DownloadFileIndex::remember(
		MediaKey key,
		const Core::FileLocation &location) {
	if (!accepts(key, location)) {
		return;
	}
	auto &files = _files[key];
	for (auto &file : files) {
		if (file.name() != location.name()) {
			continue;
		}
		if (file != location) {
			file = location;
			scheduleSave();
		}
		return;
	}
	files.push_back(location);
	scheduleSave();
}

Core::FileLocation DownloadFileIndex::lookup(MediaKey key) {
	const auto found = _files.find(key);
	if (found == _files.end()) {
		return {};
	}
	auto &files = found->second;
	for (auto i = files.begin(); i != files.end();) {
		if (i->check()) {
			return *i;
		}
		i = files.erase(i);
		scheduleSave();
	}
	_files.erase(found);
	return {};
}

void DownloadFileIndex::restore() {
	using namespace Storage::details;

	const auto base = _basePath + _name;
	if (!QFileInfo::exists(base + 's')
		&& !QFileInfo::exists(base + '0')
		&& !QFileInfo::exists(base + '1')) {
		return;
	}
	auto file = FileReadDescriptor();
	if (!ReadEncryptedFile(file, _name, _basePath, _key)) {
		LOG(("Download index: could not decrypt saved locations."));
		return;
	}
	auto format = quint32();
	file.stream >> format;
	if (format != kFormat) {
		LOG(("Download index: unsupported format."));
		return;
	}
	decltype(_files) files;
	while (!file.stream.atEnd()) {
		auto key = MediaKey();
		auto location = Core::FileLocation();
		auto bookmark = QByteArray();
		file.stream >> key.first >> key.second >> location.fname
			>> bookmark >> location.modified >> location.size;
		if (!CheckStreamStatus(file.stream)) {
			LOG(("Download index: corrupt location record."));
			return;
		}
		location.setBookmark(bookmark);
		if (accepts(key, location)) {
			files[key].push_back(std::move(location));
		}
	}
	_files = std::move(files);
}

void DownloadFileIndex::scheduleSave() {
	_dirty = true;
	if (!_saveTimer.isActive()) {
		_saveTimer.callOnce(1000);
	}
}

void DownloadFileIndex::flush() {
	_saveTimer.cancel();
	if (!_dirty) {
		return;
	}
	_dirty = false;
	auto data = Storage::details::EncryptedDescriptor(sizeof(quint32));
	data.stream << kFormat;
	for (const auto &[key, files] : _files) {
		for (const auto &file : files) {
			data.stream << key.first << key.second << file.fname
				<< file.bookmark() << file.modified << file.size;
		}
	}
	auto output = Storage::details::FileWriteDescriptor(
		_name, _basePath, false, [](bool success) {
			if (!success) {
				LOG(("Download index: could not save locations."));
			}
		});
	output.writeEncrypted(data, _key);
}

} // namespace Extras
