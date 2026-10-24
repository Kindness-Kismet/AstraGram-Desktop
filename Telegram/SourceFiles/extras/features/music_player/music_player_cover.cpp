#include "extras/features/music_player/music_player_cover.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_streaming.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "media/streaming/media_streaming_document.h"
#include "media/streaming/media_streaming_player.h"
#include "ui/image/image.h"

namespace Extras::MusicPlayer {

TrackCover::TrackCover(QObject *parent, Fn<void()> updated)
: QObject(parent)
, _updated(std::move(updated)) {
}

void TrackCover::setTrack(const AudioMsgId &track) {
	if (_track == track) {
		return;
	}
	_downloads.destroy();
	_streamed = nullptr;
	_track = track;
	_image = QImage();
	_embedded = false;
	const auto document = _track.audio();
	if (!document) {
		_media = nullptr;
		_updated();
		return;
	}
	_media = document->createMediaView();
	_media->thumbnailWanted(track.contextId());
	document->session().downloaderTaskFinished(
	) | rpl::on_next([=] { refresh(); }, _downloads);
	_streamed = document->owner().streaming().sharedDocument(document, track.contextId());
	if (_streamed) {
		_streamed->player().coverValue() | rpl::on_next([=](QImage cover) {
			if (!cover.isNull()) {
				_image = std::move(cover);
				_embedded = true;
				_updated();
			}
		}, _downloads);
	}
	refresh();
}

const QImage &TrackCover::image() const {
	return _image;
}

QImage TrackCover::originalImage() const {
	const auto document = _track.audio();
	const auto item = document ? document->owner().message(_track.contextId()) : nullptr;
	return (_embedded && document && !document->forbidsFileSave()
		&& (!_track.contextId() || (item && item->allowsMediaDownloadControls())))
		? _image : QImage();
}

void TrackCover::refresh() {
	if (!_embedded) {
		if (const auto thumbnail = _media->thumbnail()) {
			_image = thumbnail->original();
		}
	}
	_updated();
}

} // namespace Extras::MusicPlayer
