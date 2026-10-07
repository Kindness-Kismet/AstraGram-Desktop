#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"

#include "base/timer.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_download_manager.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_context_menu.h"
#include "info/downloads/info_downloads_provider.h"
#include "main/main_session.h"
#include "main/main_account.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;
using TypeFilter = Info::Downloads::TypeFilter;

std::optional<TypeFilter> parseType(const QString &text) {
	if (text == u"all"_q) return TypeFilter::All;
	if (text == u"archives"_q) return TypeFilter::Archive;
	if (text == u"music"_q) return TypeFilter::Music;
	if (text == u"videos"_q) return TypeFilter::Video;
	if (text == u"other"_q) return TypeFilter::Other;
	return std::nullopt;
}

const char *typeName(TypeFilter type) {
	switch (type) {
	case TypeFilter::All: return "all";
	case TypeFilter::Archive: return "archives";
	case TypeFilter::Music: return "music";
	case TypeFilter::Video: return "videos";
	case TypeFilter::Other: return "other";
	}
	Unexpected("Invalid download type.");
}

Json downloadInfo(not_null<HistoryItem*> item, const QString &path) {
	const auto document = item->media() ? item->media()->document() : nullptr;
	return Json{
		{ "peerId", item->history()->peer->id.value },
		{ "messageId", item->id.bare },
		{ "type", typeName(Info::Downloads::ClassifyFile(item, path)) },
		{ "filename", document ? document->filename().toStdString() : "" },
		{ "path", path.toStdString() },
		{ "size", document ? document->size : 0 },
	};
}

Result listDownloads(const QStringList &args) {
	if (args.size() > 2) {
		return Result::Err(u"usage: downloads.list [all|archives|music|videos|other] [query]"_q);
	}
	const auto type = parseType(args.isEmpty() ? u"all"_q : args[0]);
	if (!type) return Result::Err(u"unknown download type"_q);
	const auto session = ActiveSession();
	if (!session) return Result::Err(u"no active session"_q);
	auto &manager = Core::App().downloadManager();
	// 使用下载页原有数据提供器，查询不会切换页面或修改页面筛选。
	auto provider = Info::Downloads::Provider(session);
	provider.setTypeFilter(*type);
	provider.setSearchQuery(args.value(1));
	provider.refreshViewer();
	auto resolved = false;
	auto resolutionLifetime = rpl::lifetime();
	manager.loadedResolveDone() | rpl::on_next([&] {
		resolved = true;
	}, resolutionLifetime);
	auto items = Json::array();
	for (const auto &entry : provider.entries()) {
		if (&entry.item->history()->session() != session) continue;
		auto info = downloadInfo(entry.item, entry.path);
		info["started"] = entry.started;
		info["state"] = "completed";
		info["ready"] = info["size"];
		for (const auto loading : manager.loadingList()) {
			if (loading->object.item != entry.item || loading->done) continue;
			info["state"] = "downloading";
			info["ready"] = loading->ready;
			info["size"] = loading->total;
			break;
		}
		items.push_back(std::move(info));
	}
	const auto count = items.size();
	return Result::Ok(Compact(Json{
		{ "count", count }, { "items", std::move(items) },
		{ "userId", session->userId().bare },
		{ "type", typeName(*type) }, { "query", args.value(1).toStdString() },
		{ "resolved", resolved },
	}));
}

class DownloadJob final : public QObject {
public:
	DownloadJob(not_null<HistoryItem*> item, QString path)
	: QObject(qApp)
	, _session(&item->history()->session())
	, _itemId(item->fullId())
	, _document(item->media()->document())
	, _path(std::move(path))
	, _id(beginJob("download")) {
		QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
			delete this;
		});
		_session->account().sessionChanges() | rpl::on_next([this] {
			finish(false, "session closed");
		}, _lifetime);
	}

	~DownloadJob() override {
		finishJob(_id, false, "download task ended before completion");
	}

	uint64 start() {
		const auto media = _document->createMediaView();
		if (media->loaded(true)) {
			const auto bytes = media->bytes();
			const auto source = _document->filepath(true);
			const auto path = _path;
			const auto guard = QPointer<DownloadJob>(this);
			crl::async([=] {
				auto saved = false;
				if (bytes.isEmpty()) {
					saved = QFile::copy(source, path);
				} else {
					auto file = QFile(path);
					saved = file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
						&& file.write(bytes) == bytes.size() && file.flush();
				}
				crl::on_main([=] {
					if (!guard || guard->_finished) return;
					if (!saved) {
						guard->finish(false, "could not save file");
						return;
					}
					const auto item = findSessionMessage(guard->_session, guard->_itemId);
					if (!item) {
						guard->finish(false, "message no longer exists");
						return;
					}
					auto &manager = Core::App().downloadManager();
					manager.addLoaded({ item, guard->_document }, path,
						manager.computeNextStartDate());
					guard->finish(true, downloadInfo(item, path));
				});
			});
			return _id;
		}
		_document->save(_itemId, _path);
		const auto item = findSessionMessage(_session, _itemId);
		Core::App().downloadManager().addLoading({ item, _document });
		_timer.setCallback([this] { check(); });
		_timer.callEach(100);
		return _id;
	}

private:
	void check() {
		const auto item = findSessionMessage(_session, _itemId);
		if (!item || _document->status == FileDownloadFailed || _document->cancelled()) {
			finish(false, "download failed or cancelled");
			return;
		}
		if (_document->loading()) return;
		if (_document->filepath(true) != _path || QFileInfo(_path).size() != _document->size) {
			finish(false, "download did not produce the requested file");
			return;
		}
		finish(true, downloadInfo(item, _path));
	}

	void finish(bool ok, Json result) {
		if (_finished) return;
		_finished = true;
		_timer.cancel();
		finishJob(_id, ok, std::move(result));
		deleteLater();
	}

	const not_null<Main::Session*> _session;
	const FullMsgId _itemId;
	const not_null<DocumentData*> _document;
	const QString _path;
	const uint64 _id;
	bool _finished = false;
	base::Timer _timer;
	rpl::lifetime _lifetime;
};

Result startDownload(const QStringList &args) {
	if (args.size() != 3) return Result::Err(u"usage: downloads.start <peerId> <messageId> <destination>"_q);
	const auto item = findMessage(args[0], args[1]);
	const auto document = item && item->media() ? item->media()->document() : nullptr;
	if (!document) return Result::Err(u"message has no document"_q);
	if (document->forbidsFileSave() || !item->history()->peer->allowsForwarding() || HistoryView::ItemHasTtl(item)) {
		return Result::Err(u"saving this document is restricted"_q);
	}
	if (document->loading()) return Result::Err(u"document is already downloading"_q);
	const auto path = QFileInfo(args[2]);
	if (!path.isAbsolute() || path.exists() || !path.dir().exists()) {
		return Result::Err(u"destination must be a new absolute path in an existing directory"_q);
	}
	return jobStarted((new DownloadJob(item, path.absoluteFilePath()))->start());
}

Result cancelDownload(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: downloads.cancel <peerId> <messageId>"_q);
	const auto item = findMessage(args[0], args[1]);
	if (!item) return Result::Err(u"message not found"_q);
	auto &manager = Core::App().downloadManager();
	if (manager.loadingExternalState(item)) {
		manager.cancelLoadingExternal(item);
		return Result::Ok();
	}
	const auto document = item->media() ? item->media()->document() : nullptr;
	if (!document || !document->loading()) return Result::Err(u"document is not downloading"_q);
	document->cancel();
	return Result::Ok();
}

Result fakeDownload(const QStringList &args) {
	if (args.size() != 3) return Result::Err(u"usage: downloads.fake <path> <file|song|video|voice> <loading|done>"_q);
	const auto session = ActiveSession();
	if (!session || !isFakeSession(session)) return Result::Err(u"a fake session is required"_q);
	const auto path = QFileInfo(args[0]);
	if (!path.isFile() || path.size() <= 0) return Result::Err(u"expected a nonempty fixture file"_q);
	const auto kind = args[1];
	if (kind != u"file"_q && kind != u"song"_q && kind != u"video"_q && kind != u"voice"_q) return Result::Err(u"invalid media kind"_q);
	if (args[2] != u"loading"_q && args[2] != u"done"_q) return Result::Err(u"invalid download state"_q);
	auto attributes = QVector<MTPDocumentAttribute>{ MTP_documentAttributeFilename(MTP_string(path.fileName())) };
	if (kind == u"song"_q || kind == u"voice"_q) {
		attributes.push_back(MTP_documentAttributeAudio(
			MTP_flags(kind == u"voice"_q ? MTPDdocumentAttributeAudio::Flag::f_voice : MTPDdocumentAttributeAudio::Flags()),
			MTP_int(1), MTPstring(), MTPstring(), MTPbytes()));
	} else if (kind == u"video"_q) {
		attributes.push_back(MTP_documentAttributeVideo(MTP_flags(0),
			MTP_double(1), MTP_int(320), MTP_int(320), MTPint(), MTPdouble(), MTPstring()));
	}
	const auto document = session->data().document(base::RandomValue<DocumentId>(),
		0, QByteArray(), base::unixtime::now(), attributes,
		Core::MimeTypeForName(kind == u"voice"_q ? u"audio/ogg"_q : Core::MimeTypeForFile(path).name()).name(),
		InlineImageLocation(), ImageWithLocation(), ImageWithLocation(), false, 0, path.size());
	auto &manager = Core::App().downloadManager();
	const auto item = manager.generateExternalItem(document);
	if (args[2] == u"done"_q) {
		document->setLocation(Core::FileLocation(path.absoluteFilePath()));
		manager.addLoaded({ item, document }, path.absoluteFilePath(), manager.computeNextStartDate());
	} else {
		manager.addLoadingExternal({ item, document }, path.absoluteFilePath(), path.size(), [] {});
	}
	return Result::Ok(Compact(downloadInfo(item, path.absoluteFilePath())));
}

Result progressDownload(const QStringList &args) {
	if (args.size() != 3) return Result::Err(u"usage: downloads.progress <peerId> <messageId> <bytes|done>"_q);
	const auto session = ActiveSession();
	if (!session || !isFakeSession(session)) return Result::Err(u"a fake session is required"_q);
	const auto item = findMessage(args[0], args[1]);
	if (!item) return Result::Err(u"message not found"_q);
	auto &manager = Core::App().downloadManager();
	const auto state = manager.loadingExternalState(item);
	if (!state || state->done) return Result::Err(u"no active external download"_q);
	if (args[2] == u"done"_q) {
		auto path = QString();
		for (const auto entry : manager.loadingList()) {
			if (entry->object.item == item) path = entry->path;
		}
		item->media()->document()->setLocation(Core::FileLocation(path));
		manager.finishLoadingExternal(item, path);
		return Result::Ok();
	}
	auto ok = false;
	const auto bytes = args[2].toLongLong(&ok);
	if (!ok || bytes < state->ready || bytes > state->total) return Result::Err(u"progress must be monotonic and within file size"_q);
	manager.updateLoadingExternal(item, bytes, state->total);
	return Result::Ok();
}

} // namespace

const HandlerMap &downloadHandlers() {
	static const auto handlers = HandlerMap{
		{ u"downloads.list"_q, &listDownloads },
		{ u"downloads.start"_q, &startDownload },
		{ u"downloads.cancel"_q, &cancelDownload },
		{ u"downloads.fake"_q, &fakeDownload },
		{ u"downloads.progress"_q, &progressDownload },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
