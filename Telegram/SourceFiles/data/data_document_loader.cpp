#include "data/data_document_loader.h"

#include "data/data_document_media.h"
#include "data/data_reply_preview.h"
#include "data/data_session.h"
#include "data/data_streaming.h"
#include "extras/data/media_cache_manager.h"
#include "main/main_session.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "storage/storage_account.h"
#include "storage/streamed_file_downloader.h"
#include "storage/file_download_mtproto.h"
#include "storage/file_download_web.h"
#include "test/test_transfer.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/abstract_box.h"

#include <QtCore/QFile>

void DocumentData::setLocalSource(LocalSource source) {
	Expects(source.download != nullptr && source.valid != nullptr);
	Expects(!hasRemoteLocation() && !hasWebLocation() && _url.isEmpty());
	Expects(!uploading() && !_loader);

	clearLocalSource();
	_localSource = std::make_shared<LocalSourceState>();
	_localSource->source = std::move(source);
	_localSource->media = createMediaView();
	const auto weak = std::weak_ptr<LocalSourceState>(_localSource);
	_localSource->media->setLocalSourceValid([=] {
		const auto local = weak.lock();
		return local && local->active && local->source.valid();
	});
	_location = Core::FileLocation();
	resetCancelled();
}

bool DocumentData::hasLocalSource() const {
	return _localSource != nullptr;
}

bool DocumentData::localSourceValid() const {
	return _localSource
		&& _localSource->active
		&& _localSource->source.valid();
}

void DocumentData::clearLocalSource() {
	if (!_localSource) {
		return;
	}
	_localSource->active = false;
	cancelLocalSource();
	_localSource->media->clearLocalData();
	_thumbnail.clear();
	_videoThumbnail.clear();
	_inlineThumbnailBytes.clear();
	_replyPreview = nullptr;
	_goodThumbnailState = GoodThumbnailState();
	_goodThumbnailPhoto = nullptr;
	_location = Core::FileLocation();
	_owner->notifyDocumentLayoutChanged(this);
}

void DocumentData::cancelLocalSource() {
	if (!_localSource) {
		return;
	}
	++_localSourceGeneration;
	const auto wasDownloading = std::exchange(_localSource->downloading, false);
	_localSource->saveTo.clear();
	_localSource->offset = 0;
	if (wasDownloading && _localSource->source.cancel) {
		_localSource->source.cancel();
	}
}

void DocumentData::saveLocalSource(const QString &toFile, bool autoLoading) {
	if (!localSourceValid() || (!toFile.isEmpty() && forbidsFileSave())) {
		return;
	}
	const auto local = _localSource;
	if (local->media->loaded()) {
		if (toFile.isEmpty()) {
			return;
		}
		auto file = QFile(toFile);
		const auto bytes = local->media->bytes();
		if (!file.open(QIODevice::WriteOnly)
			|| file.write(bytes) != bytes.size()) {
			status = FileDownloadFailed;
			_owner->documentLoadFail(this, true);
			return;
		}
		file.close();
		_location = Core::FileLocation(toFile);
		return;
	}
	if (!toFile.isEmpty()) {
		local->saveTo = toFile;
	}
	if (local->downloading) {
		return;
	}
	status = FileReady;
	resetCancelled();
	local->downloading = true;
	local->offset = 0;
	const auto generation = ++_localSourceGeneration;
	const auto weak = std::weak_ptr<LocalSourceState>(local);
	_owner->documentLoadProgress(this);
	local->source.download([=](QByteArray bytes, QString error) {
		const auto local = weak.lock();
		if (!local || generation != _localSourceGeneration) {
			return;
		}
		local->downloading = false;
		if (!localSourceValid()) {
			clearLocalSource();
			return;
		}
		if (!error.isEmpty() || bytes.size() != size) {
			local->saveTo.clear();
			status = FileDownloadFailed;
			LOG(("Local document download failed: %1").arg(error));
			_owner->documentLoadFail(this, !autoLoading);
			return;
		}
		local->media->setBytes(bytes);
		setGoodThumbnailDataReady();
		const auto saveTo = base::take(local->saveTo);
		if (!saveTo.isEmpty()) {
			saveLocalSource(saveTo, false);
		}
		_owner->documentLoadDone(this);
		session().notifyDownloaderTaskFinished();
	}, [=](qint64 offset) {
		const auto local = weak.lock();
		if (!local || generation != _localSourceGeneration || !localSourceValid()) {
			return;
		}
		local->offset = std::clamp(offset, qint64(0), qint64(size));
		_owner->documentLoadProgress(this);
	});
}

void DocumentData::finishLoad() {
	// 下载器可能已经进入析构阶段。
	const auto guard = gsl::finally([&] {
		destroyLoader();
	});
	if (!_loader || _loader->cancelled()) {
		_flags |= Flag::DownloadCancelled;
		return;
	}
	setLocation(Core::FileLocation(_loader->fileName()));
	setGoodThumbnailDataReady();
	if (const auto media = activeMediaView()) {
		media->setBytes(_loader->bytes());
		media->checkStickerLarge(_loader.get());
	}
}

void DocumentData::destroyLoader() {
	if (!_loader) {
		return;
	}
	const auto loader = base::take(_loader);
	if (cancelled()) {
		loader->cancel();
	}
}

bool DocumentData::loading() const {
	return _localSource ? _localSource->downloading : (_loader != nullptr);
}

void DocumentData::permitLoadFromCloud() {
	if (_loader) {
		_loader->permitLoadFromCloud();
	}
}

void DocumentData::setForbidsFileSave() {
	_flags |= Flag::FileSaveForbidden;
}

bool DocumentData::forbidsFileSave() const {
	return (_flags & Flag::FileSaveForbidden);
}

QString DocumentData::loadingFilePath() const {
	if (_localSource) {
		return _localSource->downloading ? _localSource->saveTo : QString();
	}
	return loading() ? _loader->fileName() : QString();
}

bool DocumentData::displayLoading() const {
	if (_localSource) {
		return _localSource->downloading || (uploading() && !waitingForAlbum());
	}
	return loading()
		? !_loader->loadingLocal()
		: (uploading() && !waitingForAlbum());
}

float64 DocumentData::progress() const {
	if (uploading()) {
		if (uploadingData->preparing) {
			return 0.;
		}
		if (uploadingData->size > 0) {
			const auto result = float64(uploadingData->offset)
				/ float64(uploadingData->size);
			return std::clamp(result, 0., 1.);
		}
		return 0.;
	}
	if (_localSource) {
		return (size > 0) ? float64(_localSource->offset) / float64(size) : 0.;
	}
	return loading() ? _loader->currentProgress() : 0.;
}

int64 DocumentData::loadOffset() const {
	if (_localSource) {
		return _localSource->offset;
	}
	return loading() ? _loader->currentOffset() : 0;
}

bool DocumentData::uploading() const {
	return (uploadingData != nullptr);
}

bool DocumentData::loadedInMediaCache() const {
	return (_flags & Flag::LoadedInMediaCache);
}

void DocumentData::setLoadedInMediaCache(bool loaded) {
	if (hasLocalSource()) {
		return;
	}
	const auto flags = loaded
		? (_flags | Flag::LoadedInMediaCache)
		: (_flags & ~Flag::LoadedInMediaCache);
	if (_flags == flags) {
		return;
	}
	_flags = flags;
	if (filepath().isEmpty()) {
		if (loadedInMediaCache()) {
			session().local().writeFileLocation(
				mediaKey(),
				Core::FileLocation::InMediaCacheLocation());
		} else {
			session().local().removeFileLocation(mediaKey());
		}
		owner().requestDocumentViewRepaint(this);
	}
}

void DocumentData::save(
		Data::FileOrigin origin,
		const QString &toFile,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	if (hasLocalSource()) {
		if (fromCloud != LoadFromLocalOnly || _localSource->media->loaded()) {
			saveLocalSource(toFile, autoLoading);
		}
		return;
	}
	session().mediaCache().trackDocument(this, origin);
	Test::NotifyDocumentSave(this, toFile, autoLoading);
	if (const auto media = activeMediaView(); media && media->loaded(true)) {
		const auto &l = location(true);
		if (!toFile.isEmpty()) {
			if (!media->bytes().isEmpty()) {
				QFile f(toFile);
				if (f.open(QIODevice::WriteOnly)) {
					f.write(media->bytes());
					f.close();
				}

				setLocation(Core::FileLocation(toFile));
				session().local().writeFileLocation(
					mediaKey(),
					Core::FileLocation(toFile));
			} else if (l.accessEnable()) {
				const auto &alreadyName = l.name();
				if (alreadyName != toFile) {
					QFile(toFile).remove();
					QFile(alreadyName).copy(toFile);
				}
				l.accessDisable();
			}
		}
		return;
	}

	if (_loader) {
		if (!_loader->setFileName(toFile)) {
			cancel();
		}
	}
	resetCancelled();

	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) {
			_loader->permitLoadFromCloud();
		}
	} else {
		status = FileReady;
		auto reader = owner().streaming().sharedReader(this, origin, true);
		if (reader) {
			_loader = createStreamedDownloader(
				std::move(reader),
				origin,
				mediaKey(),
				toFile,
				(saveToCache() ? LoadToCacheAsWell : LoadToFileOnly),
				fromCloud,
				autoLoading);
		} else if (hasWebLocation()) {
			_loader = std::make_unique<mtpFileLoader>(
				&session(),
				_urlLocation,
				size,
				size,
				fromCloud,
				autoLoading,
				cacheTag());
		} else if (!_access && !_url.isEmpty()) {
			_loader = std::make_unique<webFileLoader>(
				&session(),
				_url,
				toFile,
				fromCloud,
				autoLoading,
				cacheTag());
		} else {
			_loader = std::make_unique<mtpFileLoader>(
				&session(),
				StorageFileLocation(
					_dc,
					session().userId(),
					MTP_inputDocumentFileLocation(
						MTP_long(id),
						MTP_long(_access),
						MTP_bytes(_fileReference),
						MTP_string())),
				origin,
				locationType(),
				toFile,
				size,
				size,
				(saveToCache() ? LoadToCacheAsWell : LoadToFileOnly),
				fromCloud,
				autoLoading,
				cacheTag());
		}
		handleLoaderUpdates();
	}
	if (loading()) {
		_loader->start();
	}
}

void DocumentData::handleLoaderUpdates() {
	_loader->updates(
	) | rpl::on_next_error_done([=] {
		_owner->documentLoadProgress(this);
	}, [=](FileLoader::Error error) {
		using FailureReason = FileLoader::FailureReason;
		if (error.started && _loader) {
			const auto origin = _loader->fileOrigin();
			const auto failedFileName = _loader->fileName();
			const auto retry = [=] {
				Ui::hideLayer();
				save(origin, failedFileName);
			};
			Ui::show(Ui::MakeConfirmBox({
				tr::lng_download_finish_failed(),
				crl::guard(&session(), retry)
			}));
		} else if (error.failureReason == FailureReason::FileWriteFailure) {
			if (!Core::App().settings().downloadPath().isEmpty()) {
				Core::App().settings().setDownloadPathBookmark(QByteArray());
				Core::App().settings().setDownloadPath(QString());
				Core::App().saveSettingsDelayed();
				InvokeQueued(qApp, [] {
					Ui::show(
						Ui::MakeInformBox(
							tr::lng_download_path_failed(tr::now)));
				});
			}
		}
		finishLoad();
		status = FileDownloadFailed;
		Test::NotifyDocumentLoadFailed(this, error.started);
		_owner->documentLoadFail(this, error.started);
	}, [=] {
		finishLoad();
		_owner->documentLoadDone(this);
	}, _loader->lifetime());

}

void DocumentData::cancel() {
	if (!loading()) {
		return;
	}

	_flags |= Flag::DownloadCancelled;
	if (hasLocalSource()) {
		cancelLocalSource();
	} else {
		destroyLoader();
	}
	_owner->documentLoadDone(this);
}

bool DocumentData::cancelled() const {
	return (_flags & Flag::DownloadCancelled);
}

void DocumentData::resetCancelled() {
	_flags &= ~Flag::DownloadCancelled;
}

auto DocumentData::createStreamedDownloader(
	std::shared_ptr<Media::Streaming::Reader> reader,
	Data::FileOrigin origin,
	std::optional<MediaKey> fileLocationKey,
	const QString &toFile,
	LoadToCacheSetting toCache,
	LoadFromCloudSetting fromCloud,
	bool autoLoading) const
-> std::unique_ptr<Storage::StreamedFileDownloader> {
	return std::make_unique<Storage::StreamedFileDownloader>(
		&session(),
		id,
		_dc,
		origin,
		Data::DocumentCacheKey(_dc, id),
		fileLocationKey,
		std::move(reader),
		toFile,
		size,
		locationType(),
		toCache,
		fromCloud,
		autoLoading,
		cacheTag());
}

bool DocumentData::useStreamingLoader() const {
	if (size <= 0) {
		return false;
	} else if (const auto info = sticker()) {
		return info->isWebm();
	}
	return isAnimation()
		|| isVideoFile()
		|| isAudioFile()
		|| isVoiceMessage();
}
