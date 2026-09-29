#include "extras/features/music_player/music_player_box.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "media/view/media_view_playback_progress.h"
#include "styles/style_extras_styles.h"
#include "styles/style_media_player.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/image/image.h"
#include "ui/layers/box_content.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "window/window_session_controller.h"

#include <QtGui/QPainterPath>

namespace Extras::MusicPlayer {
namespace {

using namespace Media::Player;
constexpr auto kSong = AudioMsgId::Type::Song;

class PlayerBox final : public Ui::BoxContent {
public:
	PlayerBox(QWidget*, not_null<Main::Session*> session);

protected:
	void prepare() override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

private:
	void refreshTrack();
	void refreshCover();
	void refreshPlayback(const TrackState &state);
	void refreshRepeat();
	void refreshPlaylist();
	void seek(float64 progress, bool finished);
	void saveCover();
	[[nodiscard]] int coverSize() const;
	[[nodiscard]] int detailsTop() const;

	const not_null<Main::Session*> _session;
	AudioMsgId _track;
	std::shared_ptr<Data::DocumentMedia> _media;
	QImage _cover;
	QString _title;
	QString _performer;
	QString _elapsed;
	QString _duration;
	crl::time _durationMs = 0;
	bool _extractingCover = false;
	bool _embeddedCover = false;
	bool _seeking = false;
	bool _resumeAfterSeeking = false;
	object_ptr<Ui::IconButton> _previous;
	object_ptr<Ui::IconButton> _play;
	object_ptr<Ui::IconButton> _next;
	object_ptr<Ui::IconButton> _repeat;
	object_ptr<Ui::IconButton> _volume;
	object_ptr<Ui::MediaSlider> _progress;
	object_ptr<Ui::MediaSlider> _volumeSlider;
	QPointer<Ui::RoundButton> _save;
	Media::View::PlaybackProgress _playback;
};

PlayerBox::PlayerBox(QWidget*, not_null<Main::Session*> session)
: _session(session)
, _previous(this, st::extrasMusicPrevious)
, _play(this, st::extrasMusicPlay)
, _next(this, st::extrasMusicNext)
, _repeat(this, st::extrasMusicRepeat)
, _volume(this, st::mediaPlayerVolumeToggle)
, _progress(this, st::mediaPlayerSpeedMenu.slider)
, _volumeSlider(this, st::mediaPlayerSpeedMenu.slider) {
	setObjectName(u"musicPlayer"_q);
	_previous->setObjectName(u"music.previous"_q);
	_play->setObjectName(u"music.play"_q);
	_next->setObjectName(u"music.next"_q);
	_repeat->setObjectName(u"music.repeat"_q);
	_volume->setObjectName(u"music.volume"_q);
	_progress->setObjectName(u"music.progress"_q);
	_volumeSlider->setObjectName(u"music.volumeSlider"_q);
	_previous->setAccessibleName(tr::extras_MusicPrevious(tr::now));
	_next->setAccessibleName(tr::extras_MusicNext(tr::now));
	_volume->setAccessibleName(tr::lng_ringtones_box_volume(tr::now));
	_progress->setAccessibleName(tr::extras_MusicProgress(tr::now));
	_volumeSlider->setAccessibleName(tr::lng_ringtones_box_volume(tr::now));
}

void PlayerBox::prepare() {
	setTitle(tr::extras_MusicPlayer());
	setDimensions(st::extrasMusicWidth, st::extrasMusicHeight);
	_save = addLeftButton(tr::extras_MusicSaveCover(), [=] { saveCover(); });
	_save->setObjectName(u"music.saveCover"_q);
	addButton(tr::extras_MusicCollapse(), [=] { closeBox(); });
	boxClosing() | rpl::on_next([=] {
		if (!_seeking) {
			return;
		}
		_seeking = false;
		instance()->cancelSeeking(kSong);
		if (_resumeAfterSeeking && IsPaused(instance()->getState(kSong).state)) {
			instance()->play(kSong);
		}
	}, lifetime());
	_previous->setClickedCallback([] { instance()->previous(kSong); });
	_next->setClickedCallback([] { instance()->next(kSong); });
	_play->setClickedCallback([] { instance()->playPause(kSong); });
	_repeat->setClickedCallback([] {
		auto &settings = Core::App().settings();
		const auto mode = settings.playerRepeatMode();
		settings.setPlayerRepeatMode(mode == Media::RepeatMode::None
			? Media::RepeatMode::One
			: mode == Media::RepeatMode::One
			? Media::RepeatMode::All
			: Media::RepeatMode::None);
		Core::App().saveSettingsDelayed();
	});
	_volume->setClickedCallback([] {
		auto &settings = Core::App().settings();
		const auto volume = settings.songVolume() > 0.
			? 0.
			: settings.rememberedSongVolume();
		settings.setSongVolume(volume);
		mixer()->setSongVolume(volume);
		Core::App().saveSettingsDelayed();
	});
	_volumeSlider->setChangeProgressCallback([](float64 volume) {
		Core::App().settings().setSongVolume(volume);
		mixer()->setSongVolume(volume);
	});
	_volumeSlider->setChangeFinishedCallback([](float64 volume) {
		if (volume > 0.) {
			Core::App().settings().setRememberedSongVolume(volume);
		}
		Core::App().saveSettingsDelayed();
	});
	_progress->setChangeProgressCallback([=](float64 value) {
		seek(value, false);
	});
	_progress->setChangeFinishedCallback([=](float64 value) {
		seek(value, true);
	});
	_playback.setValueChangedCallback([=](float64 value, float64 received) {
		if (!_seeking) {
			_progress->setValue(value, received);
		}
	});
	const auto refreshVolume = [=] {
		const auto volume = Core::App().settings().songVolume();
		if (!_volumeSlider->isChanging()) {
			_volumeSlider->setValue(volume);
		}
		_volume->setIconOverride(volume > 0. ? nullptr : &st::mediaPlayerVolumeIcon0);
	};
	Core::App().settings().songVolumeChanges(
	) | rpl::on_next(refreshVolume, lifetime());
	refreshVolume();
	Core::App().settings().playerRepeatModeValue(
	) | rpl::on_next([=] { refreshRepeat(); }, lifetime());
	instance()->playlistChanges(kSong
	) | rpl::on_next([=] { refreshPlaylist(); }, lifetime());
	instance()->trackChanged(
	) | rpl::filter([](AudioMsgId::Type type) {
		return type == kSong;
	}) | rpl::on_next([=] { refreshTrack(); }, lifetime());
	instance()->updatedNotifier(
	) | rpl::on_next([=](const TrackState &state) {
		refreshPlayback(state);
	}, lifetime());
	instance()->stops(kSong
	) | rpl::on_next([=] { closeBox(); }, lifetime());
	_session->downloaderTaskFinished(
	) | rpl::on_next([=] { refreshCover(); }, lifetime());
	refreshTrack();
}

void PlayerBox::refreshTrack() {
	const auto track = instance()->current(kSong);
	const auto document = track.audio();
	if (!document || &document->session() != _session.get()) {
		closeBox();
		return;
	}
	if (_track.audio() != document || _track.contextId() != track.contextId()) {
		_track = track;
		_seeking = false;
		_cover = QImage();
		_extractingCover = _embeddedCover = false;
		_media = document->createMediaView();
		_media->thumbnailWanted(track.contextId());
		const auto song = document->song();
		_title = (song && !song->title.isEmpty())
			? song->title
			: document->filename();
		_performer = song ? song->performer : QString();
		setAccessibleName(_title);
		refreshCover();
	}
	refreshPlaylist();
	auto state = instance()->getState(kSong);
	if (!state.id) {
		state.id = track;
		state.length = document->duration();
		state.frequency = 1000;
	}
	refreshPlayback(state);
	update();
}

void PlayerBox::refreshCover() {
	if (!_media) {
		return;
	}
	if (!_embeddedCover) {
		if (const auto thumbnail = _media->thumbnail()) {
			_cover = thumbnail->original();
		}
	}
	_save->setVisible(!_cover.isNull());
	update();
	if (_extractingCover) {
		return;
	}
	const auto bytes = _media->bytes();
	const auto path = _track.audio()->filepath(true);
	if (bytes.isEmpty() && path.isEmpty()) {
		return;
	}
	_extractingCover = true;
	const auto track = _track;
	const auto done = crl::guard(this, [=](QImage cover) {
		if (_track != track || cover.isNull()) {
			return;
		}
		_cover = std::move(cover);
		_embeddedCover = true;
		_save->show();
		update();
	});
	// 工作线程只处理文件与图片，回到主线程后再核对歌曲及控件生命周期。
	crl::async([=] {
		auto info = Media::Player::PrepareForSending(path, bytes);
		auto cover = v::get<Ui::PreparedFileInformation::Song>(info.media).cover;
		crl::on_main([=] { done(cover); });
	});
}

void PlayerBox::refreshPlayback(const TrackState &state) {
	if (!state.id || state.id.type() != kSong) {
		return;
	}
	const auto playing = ShowPauseIcon(state.state);
	_play->setIconOverride(playing ? &st::extrasMusicPauseIcon : nullptr);
	_play->setAccessibleName(playing
		? tr::extras_MusicPause(tr::now)
		: tr::extras_MusicPlay(tr::now));
	_durationMs = (state.frequency > 0 && state.length > 0)
		? state.length * 1000 / state.frequency
		: 0;
	_progress->setDisabled(_durationMs <= 0);
	_playback.updateState(state);
	_duration = Ui::FormatDurationText(_durationMs / 1000);
	if (!_seeking) {
		_elapsed = Ui::FormatDurationText(state.frequency > 0
			? std::max(int64(0), state.position / state.frequency)
			: 0);
	}
	update();
}

void PlayerBox::seek(float64 progress, bool finished) {
	if (_durationMs <= 0) {
		return;
	}
	if (!_seeking) {
		_resumeAfterSeeking = ShowPauseIcon(instance()->getState(kSong).state);
		_seeking = true;
		if (!instance()->getState(kSong).id) {
			instance()->play(kSong);
		}
		instance()->startSeeking(kSong);
	}
	_elapsed = Ui::FormatDurationText(int(progress * _durationMs / 1000));
	update();
	if (finished) {
		_seeking = false;
		instance()->finishSeeking(kSong, progress);
	}
}

void PlayerBox::refreshRepeat() {
	const auto mode = Core::App().settings().playerRepeatMode();
	_repeat->setIconOverride(mode == Media::RepeatMode::None
		? &st::mediaPlayerRepeatDisabledIcon
		: mode == Media::RepeatMode::One
		? &st::mediaPlayerRepeatOneIcon
		: nullptr);
	_repeat->setAccessibleName(mode == Media::RepeatMode::None
		? tr::extras_MusicRepeatOff(tr::now)
		: mode == Media::RepeatMode::One
		? tr::extras_MusicRepeatOne(tr::now)
		: tr::extras_MusicRepeatAll(tr::now));
}

void PlayerBox::refreshPlaylist() {
	const auto previous = instance()->previousAvailable(kSong);
	const auto next = instance()->nextAvailable(kSong);
	_previous->setDisabled(!previous);
	_next->setDisabled(!next);
	_previous->setIconOverride(previous ? nullptr : &st::mediaPlayerPreviousDisabledIcon);
	_next->setIconOverride(next ? nullptr : &st::mediaPlayerNextDisabledIcon);
}

int PlayerBox::coverSize() const {
	return std::max(0, std::min({
		st::extrasMusicCoverSize,
		width() - 2 * st::extrasMusicPadding,
		height() - st::extrasMusicDetailsHeight,
	}));
}

int PlayerBox::detailsTop() const {
	return st::extrasMusicPadding + coverSize();
}

void PlayerBox::resizeEvent(QResizeEvent *event) {
	BoxContent::resizeEvent(event);
	const auto padding = st::extrasMusicPadding;
	const auto top = detailsTop();
	_progress->setGeometry(padding, top + st::extrasMusicProgressTop,
		width() - 2 * padding, st::extrasMusicSliderHeight);
	const auto controlsTop = top + st::extrasMusicControlsTop;
	_play->move((width() - _play->width()) / 2, controlsTop);
	_previous->move(_play->x() - _previous->width() - padding, controlsTop);
	_next->move(_play->x() + _play->width() + padding, controlsTop);
	_repeat->move(width() - padding - _repeat->width(), controlsTop);
	_volume->move(padding, top + st::extrasMusicVolumeTop);
	_volumeSlider->setGeometry(padding + _volume->width(),
		top + st::extrasMusicVolumeTop + (_volume->height() - st::extrasMusicSliderHeight) / 2,
		width() - 2 * padding - _volume->width(), st::extrasMusicSliderHeight);
}

void PlayerBox::paintEvent(QPaintEvent *event) {
	auto p = Painter(this);
	p.fillRect(event->rect(), st::boxBg);
	p.setRenderHint(QPainter::Antialiasing);
	const auto padding = st::extrasMusicPadding;
	const auto size = coverSize();
	const auto cover = QRect((width() - size) / 2, padding, size, size);
	if (!cover.isEmpty()) {
		auto clip = QPainterPath();
		clip.addRoundedRect(cover, st::extrasMusicCoverRadius, st::extrasMusicCoverRadius);
		p.save();
		p.setClipPath(clip);
		p.fillRect(cover, st::windowBgOver);
		if (_cover.isNull()) {
			st::extrasMusicPlaceholder.paintInCenter(p, cover);
		} else {
			const auto fitted = _cover.size().scaled(cover.size(), Qt::KeepAspectRatio);
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			p.drawImage(QRect(cover.center() - QPoint(fitted.width() / 2,
				fitted.height() / 2), fitted), _cover);
		}
		p.restore();
	}
	const auto top = detailsTop();
	const auto textWidth = width() - 2 * padding;
	p.setPen(st::windowFg);
	p.setFont(st::extrasMusicTitleFont);
	p.drawText(QRect(padding, top + st::extrasMusicTitleTop, textWidth,
		st::extrasMusicTitleHeight), Qt::AlignCenter,
		p.fontMetrics().elidedText(_title, Qt::ElideRight, textWidth));
	p.setPen(st::windowSubTextFg);
	p.setFont(st::normalFont);
	p.drawText(QRect(padding, top + st::extrasMusicArtistTop, textWidth,
		st::extrasMusicTitleHeight), Qt::AlignCenter,
		p.fontMetrics().elidedText(_performer, Qt::ElideRight, textWidth));
	const auto times = QRect(padding, top + st::extrasMusicTimeTop,
		textWidth, st::extrasMusicTitleHeight);
	p.drawText(times, Qt::AlignLeft | Qt::AlignVCenter, _elapsed);
	p.drawText(times, Qt::AlignRight | Qt::AlignVCenter, _duration);
}

void PlayerBox::saveCover() {
	if (_cover.isNull()) {
		return;
	}
	const auto cover = _cover;
	// 保存对话框归属主窗口，收起播放器后仍可完成当前封面的保存。
	const auto parent = window();
	FileDialog::GetWritePath(parent, tr::extras_MusicSaveCover(tr::now),
		u"PNG (*.png);;"_q + FileDialog::AllFilesFilter(),
		filedialogDefaultName(u"cover"_q, u".png"_q),
		crl::guard(parent, [=](const QString &path) {
			if (path.isEmpty()) {
				return;
			}
			if (!cover.save(path, "PNG")) {
				Ui::Toast::Show(tr::extras_MusicSaveFailed(tr::now));
			}
		}));
}

} // namespace

void show(not_null<Window::SessionController*> controller) {
	const auto document = instance()->current(kSong).audio();
	if (document) {
		controller->show(Box<PlayerBox>(&document->session()));
	}
}

} // namespace Extras::MusicPlayer
