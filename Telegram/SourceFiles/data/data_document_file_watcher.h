#pragma once

#include "core/file_location.h"
#include <QtCore/QFileSystemWatcher>
#include <rpl/event_stream.h>
#include <map>
#include <set>

class DocumentData;

namespace Data {

class Session;

class DocumentFileWatcher final {
public:
	explicit DocumentFileWatcher(not_null<Session*> owner);
	void watch(not_null<DocumentData*> document, const QString &path);
	void watchDownload(const QString &path);
	void clear();
	[[nodiscard]] rpl::producer<QString> invalidatedPaths() const;

private:
	void changed(const QString &path);

	const not_null<Session*> _owner;
	QFileSystemWatcher _watcher;
	std::map<not_null<DocumentData*>, QString> _paths;
	std::map<QString, std::set<not_null<DocumentData*>>> _documents;
	std::map<QString, Core::FileLocation> _downloads;
	rpl::event_stream<QString> _invalidatedPaths;
};

} // namespace Data
