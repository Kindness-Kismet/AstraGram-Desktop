#include "data/data_document_file_watcher.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "logs.h"

#include <QtCore/QFileInfo>

namespace Data {

DocumentFileWatcher::DocumentFileWatcher(not_null<Session*> owner)
: _owner(owner) {
	QObject::connect(&_watcher, &QFileSystemWatcher::fileChanged,
		&_watcher, [=](const QString &path) { changed(path); });
}

void DocumentFileWatcher::watch(
		not_null<DocumentData*> document,
		const QString &path) {
	const auto absolute = path.isEmpty()
		? QString()
		: QFileInfo(path).absoluteFilePath();
	const auto i = _paths.find(document);
	if (i != _paths.end()) {
		if (i->second == absolute) {
			return;
		}
		const auto j = _documents.find(i->second);
		j->second.erase(document);
		if (j->second.empty()) {
			if (!_downloads.contains(j->first)) {
				_watcher.removePath(j->first);
			}
			_documents.erase(j);
		}
		_paths.erase(i);
	}
	if (absolute.isEmpty()) {
		return;
	}
	_paths.emplace(document, absolute);
	auto &documents = _documents[absolute];
	if (documents.empty() && !_downloads.contains(absolute)
		&& !_watcher.addPath(absolute)) {
		LOG(("Document file: could not watch %1.").arg(absolute));
	}
	documents.emplace(document);
}

void DocumentFileWatcher::watchDownload(const QString &path) {
	if (path.isEmpty()) {
		return;
	}
	const auto absolute = QFileInfo(path).absoluteFilePath();
	const auto location = Core::FileLocation(absolute);
	if (location.isEmpty()) {
		return;
	}
	if (!_documents.contains(absolute) && !_downloads.contains(absolute)
		&& !_watcher.addPath(absolute)) {
		LOG(("Document file: could not watch %1.").arg(absolute));
	}
	_downloads.insert_or_assign(absolute, location);
}

void DocumentFileWatcher::checkPath(const QString &path) {
	changed(QFileInfo(path).absoluteFilePath());
}

void DocumentFileWatcher::clear() {
	const auto paths = _watcher.files();
	if (!paths.empty()) {
		_watcher.removePaths(paths);
	}
	_paths.clear();
	_documents.clear();
	_downloads.clear();
}

void DocumentFileWatcher::changed(const QString &path) {
	const auto i = _documents.find(path);
	// 校验文件位置会移除监听，先保存受影响的文档。
	const auto documents = (i == _documents.end())
		? std::set<not_null<DocumentData*>>()
		: i->second;
	auto invalidated = false;
	const auto download = _downloads.find(path);
	if (download != _downloads.end() && !download->second.check()) {
		_downloads.erase(download);
		invalidated = true;
	}
	for (const auto document : documents) {
		const auto &location = document->location(true);
		if (location.isEmpty() || QFileInfo(location.name()).absoluteFilePath() != path) {
			invalidated = true;
			_owner->notifyDocumentLayoutChanged(document);
		}
	}
	if (invalidated) {
		_invalidatedPaths.fire_copy(path);
	}
	if (!_documents.contains(path) && !_downloads.contains(path)) {
		_watcher.removePath(path);
		return;
	}
	// 原子替换可能保留有效文件，但系统会撤销原文件的监听。
	if (!_watcher.files().contains(path)) {
		if (!_watcher.addPath(path)) {
			LOG(("Document file: could not watch %1.").arg(path));
		}
	}
}

rpl::producer<QString> DocumentFileWatcher::invalidatedPaths() const {
	return _invalidatedPaths.events();
}

} // namespace Data
