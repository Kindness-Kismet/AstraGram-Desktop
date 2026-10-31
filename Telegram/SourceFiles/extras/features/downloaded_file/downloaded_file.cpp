#include "extras/features/downloaded_file/downloaded_file.h"

#include "core/application.h"
#include "data/data_document.h"
#include "data/data_document_file_watcher.h"
#include "data/data_download_manager.h"
#include "data/data_session.h"
#include "main/main_session.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace ExtrasDownloadedFile {
namespace {

[[nodiscard]] bool transferring(not_null<DocumentData*> document) {
	if (document->loading() || document->uploading()) {
		return true;
	}
	for (const auto entry : Core::App().downloadManager().loadingList()) {
		if (entry->object.document == document && !entry->done) {
			return true;
		}
	}
	return false;
}

} // namespace

Core::FileLocation location(
		not_null<DocumentData*> document,
		const QString &path) {
	if (transferring(document)) {
		return {};
	}
	const auto file = path.isEmpty()
		? document->location(true)
		: Core::FileLocation(path);
	return (!file.isEmpty() && QFileInfo(file.name()).isFile())
		? file
		: Core::FileLocation();
}

void remove(
		not_null<DocumentData*> document,
		Core::FileLocation file,
		Fn<void(bool)> done) {
	if (file.isEmpty() || transferring(document)) {
		done(false);
		return;
	}
	const auto weak = base::make_weak(&document->session());
	crl::async([weak, file = std::move(file), done = std::move(done)] {
		const auto access = file.accessEnable();
		// 菜单打开后文件可能被替换，删除前仍须匹配原文件。
		const auto success = access
			&& file.check()
			&& QFileInfo(file.name()).isFile()
			&& QFile(file.name()).moveToTrash();
		if (access) {
			file.accessDisable();
		}
		crl::on_main([weak, path = file.name(), success, done] {
			if (success && weak) {
				weak->data().documentFileWatcher().checkPath(path);
			}
			done(success);
		});
	});
}

} // namespace ExtrasDownloadedFile
