#include "extras/features/music_player/music_player_cover.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "ui/chat/attach/attach_prepare.h"
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
	_track = track;
	_image = QImage();
	_extracting = _embedded = false;
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
	refresh();
}

const QImage &TrackCover::image() const {
	return _image;
}

void TrackCover::refresh() {
	if (!_embedded) {
		if (const auto thumbnail = _media->thumbnail()) {
			_image = thumbnail->original();
		}
	}
	_updated();
	if (_extracting || !_track.audio()->song()) {
		return;
	}
	const auto bytes = _media->bytes();
	const auto path = _track.audio()->filepath(true);
	if (bytes.isEmpty() && path.isEmpty()) {
		return;
	}
	_extracting = true;
	const auto track = _track;
	const auto done = crl::guard(this, [=](QImage image) {
		if (_track != track || image.isNull()) {
			return;
		}
		_image = std::move(image);
		_embedded = true;
		_updated();
	});
	// 工作线程只处理文件与图片，界面销毁或切歌后丢弃旧结果。
	crl::async([=] {
		auto info = Media::Player::PrepareForSending(path, bytes);
		auto image = v::get<Ui::PreparedFileInformation::Song>(info.media).cover;
		crl::on_main([=] { done(image); });
	});
}

} // namespace Extras::MusicPlayer
