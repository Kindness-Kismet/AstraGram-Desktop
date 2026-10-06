#include "extras/features/music_player/music_player_panel.h"

#include "extras/features/music_player/music_player_cover.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_button.h"
#include "media/player/media_player_dropdown.h"
#include "media/player/media_player_instance.h"
#include "media/view/media_view_playback_progress.h"
#include "styles/style_extras_styles.h"
#include "styles/style_media_player.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/dropdown_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtGui/QPainterPath>
#include <array>

namespace Extras::MusicPlayer {
namespace {

using namespace Media::Player;
constexpr auto kSong = AudioMsgId::Type::Song;

} // namespace

class CompactPlayButton final : public Ui::IconButton {
public:
	explicit CompactPlayButton(QWidget *parent)
	: IconButton(parent, st::extrasMusicCompactPlay)
	, _layout(st::mediaPlayerPlayIcon, [=] { update(); }) {
	}

	void setState(PlayButtonLayout::State state) {
		_layout.setState(state);
	}
	void finishTransform() {
		_layout.finishTransform();
	}

protected:
	void paintEvent(QPaintEvent *event) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		p.drawEllipse(rect());
		paintRipple(p, st::extrasMusicCompactPlay.rippleAreaPosition);
		p.translate(
			(width() - st::mediaPlayerPlayIcon.size.width()) / 2,
			(height() - st::mediaPlayerPlayIcon.size.height()) / 2);
		_layout.paint(p, st::windowFgActive);
	}

private:
	PlayButtonLayout _layout;
};

CompactPanel::CompactPanel(
	QWidget *parent,
	not_null<Ui::RpWidget*> menuParent,
	not_null<Window::SessionController*> controller,
	AudioMsgId::Type type)
: RpWidget(parent)
, _controller(controller)
, _type(type)
, _cover(new TrackCover(this, [=] {
	_save->setDisabled(_cover->image().isNull());
	update();
}))
, _save(this, st::extrasMusicCompactSave)
, _collapse(this, st::extrasMusicCompactCollapse)
, _close(this, st::extrasMusicCompactClose)
, _previous(this, st::extrasMusicPrevious)
, _play(this)
, _next(this, st::extrasMusicNext)
, _repeat(this, st::extrasMusicRepeat)
, _order(this, st::mediaPlayerOrderButton)
, _volume(this, st::mediaPlayerVolumeToggle)
, _playlist(this, st::extrasMusicCompactQueue)
, _speed(this, st::mediaPlayerSpeedButton)
, _source(this, QString(), st::extrasMusicCompactSource)
, _progress(this, st::mediaPlayerSpeedMenu.slider)
, _volumeSlider(this, st::mediaPlayerSpeedMenu.slider)
, _playback(std::make_unique<Media::View::PlaybackProgress>())
, _orderController(std::make_unique<OrderController>(
	_order.data(), menuParent, [](bool) {},
	Core::App().settings().playerOrderModeValue(),
	[](Media::OrderMode mode) {
		Core::App().settings().setPlayerOrderMode(mode);
		Core::App().saveSettingsDelayed();
	}))
, _speedController(std::make_unique<SpeedController>(
	_speed.data(), _speed->st(), menuParent, [](bool) {},
	[=](bool lastNonDefault) {
		return playbackType() == kSong
			? Core::App().settings().audioPlaybackSpeed(lastNonDefault)
			: Core::App().settings().voicePlaybackSpeed(lastNonDefault);
	},
	[=](float64 speed) {
		if (playbackType() == kSong) {
			Core::App().settings().setAudioPlaybackSpeed(speed);
		} else {
			Core::App().settings().setVoicePlaybackSpeed(speed);
		}
		Core::App().saveSettingsDelayed();
	})) {
	setObjectName(u"music.compact"_q);
	_save->setObjectName(u"music.saveCover"_q);
	_collapse->setObjectName(u"music.collapse"_q);
	_close->setObjectName(u"music.close"_q);
	_previous->setObjectName(u"music.previous"_q);
	_play->setObjectName(u"music.play"_q);
	_next->setObjectName(u"music.next"_q);
	_repeat->setObjectName(u"music.repeat"_q);
	_order->setObjectName(u"music.order"_q);
	_volume->setObjectName(u"music.volume"_q);
	_playlist->setObjectName(u"music.playlist"_q);
	_speed->setObjectName(u"music.speed"_q);
	_source->setObjectName(u"music.source"_q);
	_progress->setObjectName(u"music.progress"_q);
	_volumeSlider->setObjectName(u"music.volumeSlider"_q);
	_save->setAccessibleName(tr::extras_MusicSaveCover(tr::now));
	_save->setToolTip(tr::extras_MusicSaveCover(tr::now));
	_collapse->setAccessibleName(tr::extras_MusicCollapse(tr::now));
	_close->setAccessibleName(tr::extras_MusicClose(tr::now));
	_close->setToolTip(tr::extras_MusicClose(tr::now));
	_previous->setAccessibleName(tr::extras_MusicPrevious(tr::now));
	_next->setAccessibleName(tr::extras_MusicNext(tr::now));
	_order->setAccessibleName(tr::lng_sr_playback_order(tr::now));
	_volume->setAccessibleName(tr::lng_ringtones_box_volume(tr::now));
	_playlist->setAccessibleName(tr::extras_MusicPlaylist(tr::now));
	_playlist->setToolTip(tr::extras_MusicPlaylist(tr::now));
	_progress->setAccessibleName(tr::extras_MusicProgress(tr::now));
	_volumeSlider->setAccessibleName(tr::lng_ringtones_box_volume(tr::now));

	_save->setClickedCallback([=] { saveCover(); });
	_collapse->setClickedCallback([=] { _collapseRequests.fire({}); });
	_close->setClickedCallback([=] { stop(); });
	_previous->setClickedCallback([=] { instance()->previous(playbackType()); });
	_next->setClickedCallback([=] { instance()->next(playbackType()); });
	_play->setClickedCallback([=] { instance()->playPauseCancelClicked(playbackType()); });
	_playlist->setClickedCallback([=] { _playlistRequests.fire({}); });
	_source->setClickedCallback([=] { showSource(); });
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
			? 0. : settings.rememberedSongVolume();
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
	setupDropdownSwitching();

	_progress->setChangeProgressCallback([=](float64 value) { seek(value, false); });
	_progress->setChangeFinishedCallback([=](float64 value) { seek(value, true); });
	_playback->setValueChangedCallback([=](float64 value, float64 received) {
		if (!_seeking) {
			_progress->setValue(value, received);
		}
	});
	_speedController->saved(
	) | rpl::on_next([] { instance()->updatePlaybackSpeed(); }, lifetime());
	rpl::merge(_orderController->menuToggledValue(), _speedController->menuToggledValue()
	) | rpl::filter([](bool shown) { return shown; }
	) | rpl::on_next([=] { updateMenuGeometry(); }, lifetime());
	_speedController->realtimeValue(
	) | rpl::on_next([=](float64 value) {
		_speed->setSpeed(value);
		_speed->setAccessibleName(tr::lng_mediaview_playback_speed(
			tr::now, lt_speed, QString::number(value) + u"x"_q));
	}, lifetime());
	rpl::merge(
		Core::App().settings().audioPlaybackSpeedChanges(),
		Core::App().settings().voicePlaybackSpeedChanges()
	) | rpl::on_next([=] { _speedController->reloadFromLookup(); }, lifetime());
	Core::App().settings().playerRepeatModeValue(
	) | rpl::on_next([=] { refreshRepeat(); }, lifetime());
	rpl::merge(instance()->playlistChanges(kSong),
		instance()->playlistChanges(AudioMsgId::Type::Voice)
	) | rpl::on_next([=] { refreshPlaylist(); }, lifetime());
	instance()->trackChanged(
	) | rpl::on_next([=] { refreshTrack(); }, lifetime());
	instance()->updatedNotifier(
	) | rpl::on_next([=] { refreshTrack(); }, lifetime());
	instance()->playbackAllowedValue(
	) | rpl::on_next([=] { refreshTrack(); }, lifetime());
	rpl::merge(instance()->stops(kSong), instance()->stops(AudioMsgId::Type::Voice)
	) | rpl::on_next([=] {
		cancelSeek(false);
		refreshTrack();
	}, lifetime());
	resize(st::extrasMusicCompactWidth, st::extrasMusicCompactHeight);
	refreshTrack();
	_play->finishTransform();
}

CompactPanel::~CompactPanel() {
	lifetime().destroy();
}

AudioMsgId::Type CompactPanel::playbackType() const {
	return _type;
}

void CompactPanel::setPlaybackType(AudioMsgId::Type type) {
	if (_type == type) {
		return;
	}
	cancelSeek(false);
	_type = type;
	refreshTrack();
}

void CompactPanel::refreshTrack() {
	const auto track = instance()->current(playbackType());
	const auto document = track.audio();
	if (!document) {
		return;
	}
	if (_track != track) {
		_seeking = false;
		_track = track;
		_cover->setTrack(track);
		const auto song = document->song();
		_title = (song && !song->title.isEmpty()) ? song->title : document->filename();
		_performer = song ? song->performer : QString();
		const auto item = document->owner().message(track.contextId());
		if (track.type() == AudioMsgId::Type::Voice) {
			_title = document->isVideoMessage()
				? tr::extras_MusicVideo(tr::now) : tr::extras_MusicVoice(tr::now);
			_performer = item ? item->author()->shortName() : QString();
		}
		_sourceName = item ? item->history()->peer->shortName() : QString();
		_source->setVisible(item != nullptr);
		_source->setToolTip(tr::extras_MusicSource(tr::now));
		_speedController->reloadFromLookup();
		setAccessibleName(_title);
		QResizeEvent resized(size(), size());
		resizeEvent(&resized);
	}
	refreshPlaylist();
	refreshPlayback(instance()->getState(playbackType()));
	update();
}

void CompactPanel::refreshPlayback(const TrackState &state) {
	if (!state.id || state.id != _track) {
		return;
	}
	_playing = ShowPauseIcon(state.state);
	_play->setState(state.id.audio()->loading()
		? PlayButtonLayout::State::Cancel
		: _playing ? PlayButtonLayout::State::Pause : PlayButtonLayout::State::Play);
	_play->setAccessibleName(_playing
		? tr::extras_MusicPause(tr::now) : tr::extras_MusicPlay(tr::now));
	_durationMs = (state.frequency > 0 && state.length > 0)
		? state.length * 1000 / state.frequency : 0;
	_progress->setDisabled(_durationMs <= 0 || !instance()->playbackAllowed());
	_playback->updateState(state);
	_duration = Ui::FormatDurationText(_durationMs / 1000);
	if (!_seeking) {
		_elapsed = Ui::FormatDurationText(state.frequency > 0
			? std::max(int64(0), state.position / state.frequency) : 0);
	}
	update();
}

void CompactPanel::refreshRepeat() {
	const auto mode = Core::App().settings().playerRepeatMode();
	_repeat->setIconOverride(mode == Media::RepeatMode::None
		? &st::mediaPlayerRepeatDisabledIcon
		: mode == Media::RepeatMode::One ? &st::mediaPlayerRepeatOneIcon : nullptr);
	_repeat->setAccessibleName(mode == Media::RepeatMode::None
		? tr::extras_MusicRepeatOff(tr::now)
		: mode == Media::RepeatMode::One
		? tr::extras_MusicRepeatOne(tr::now) : tr::extras_MusicRepeatAll(tr::now));
}

void CompactPanel::refreshPlaylist() {
	const auto type = playbackType();
	const auto music = type == kSong;
	_order->setVisible(music);
	_repeat->setVisible(music);
	_volume->setVisible(music);
	_volumeSlider->setVisible(music);
	_playlist->setVisible(music);
	_save->setVisible(music);
	_previous->setAccessibleName(music
		? tr::extras_MusicPrevious(tr::now) : tr::extras_MediaPrevious(tr::now));
	_next->setAccessibleName(music
		? tr::extras_MusicNext(tr::now) : tr::extras_MediaNext(tr::now));
	const auto allowed = instance()->playbackAllowed();
	_play->setDisabled(!allowed);
	const auto previous = allowed && instance()->previousAvailable(type);
	const auto next = allowed && instance()->nextAvailable(type);
	_previous->setDisabled(!previous);
	_next->setDisabled(!next);
	_previous->setIconOverride(previous ? nullptr : &st::mediaPlayerPreviousDisabledIcon);
	_next->setIconOverride(next ? nullptr : &st::mediaPlayerNextDisabledIcon);
	const auto supported = _track.changeablePlaybackSpeed() && Media::Audio::SupportsSpeedControl();
	_speed->setVisible(supported);
}

void CompactPanel::seek(float64 progress, bool finished) {
	if (_durationMs <= 0) {
		return;
	}
	if (!_seeking) {
		_resumeAfterSeeking = ShowPauseIcon(instance()->getState(playbackType()).state);
		_seeking = true;
		instance()->startSeeking(playbackType());
	}
	_elapsed = Ui::FormatDurationText(int(progress * _durationMs / 1000));
	update();
	if (finished) {
		_seeking = false;
		instance()->finishSeeking(playbackType(), progress);
		if (!_resumeAfterSeeking) {
			instance()->pause(playbackType());
		}
	}
}

void CompactPanel::cancelSeek(bool resume) {
	if (!_seeking) {
		return;
	}
	_seeking = false;
	instance()->cancelSeeking(playbackType());
	if (resume && _resumeAfterSeeking && IsPaused(instance()->getState(playbackType()).state)) {
		instance()->play(playbackType());
	}
}

void CompactPanel::dismiss() {
	cancelSeek(true);
	if (const auto menu = _orderController->menu()) {
		menu->hideFast();
	}
	if (const auto menu = _speedController->menu()) {
		menu->hideFast();
	}
}

void CompactPanel::setupDropdownSwitching() {
	const auto controls = std::array<not_null<Ui::RpWidget*>, 3>{
		_order.data(), _speed.data(), _volume.data(),
	};
	for (const auto control : controls) {
		control->events(
		) | rpl::filter([](not_null<QEvent*> event) {
			return event->type() == QEvent::Enter;
		}) | rpl::on_next([=] {
			if (control.get() != _order.data()) {
				if (const auto menu = _orderController->menu()) {
					menu->hideFast();
				}
			}
			if (control.get() != _speed.data()) {
				if (const auto menu = _speedController->menu()) {
					menu->hideFast();
				}
			}
		}, lifetime());
	}
	const auto otherDropdownCheck = [=](QPoint position) {
		return ranges::any_of(controls, [&](not_null<Ui::RpWidget*> control) {
			return !control->isHidden()
				&& control->rect().contains(control->mapFromGlobal(position));
		});
	};
	_orderController->setOtherDropdownCheck(otherDropdownCheck);
	_speedController->setOtherDropdownCheck(otherDropdownCheck);
}

void CompactPanel::setMenuBounds(QRect bounds) {
	_menuBounds = bounds;
	updateMenuGeometry();
}

void CompactPanel::updateMenuGeometry() {
	_orderController->updateDropdownGeometry();
	_speedController->updateDropdownGeometry();
	if (_menuBounds.isEmpty()) {
		return;
	}
	for (const auto menu : { _orderController->menu(), _speedController->menu() }) {
		if (!menu) {
			continue;
		}
		menu->setMaxHeight(_menuBounds.height());
		menu->move(
			std::clamp(menu->x(), _menuBounds.left(),
				std::max(_menuBounds.left(), _menuBounds.right() + 1 - menu->width())),
			std::clamp(menu->y(), _menuBounds.top(),
				std::max(_menuBounds.top(), _menuBounds.bottom() + 1 - menu->height())));
	}
}

void CompactPanel::stop() {
	cancelSeek(false);
	instance()->stopAndClose();
	_collapseRequests.fire({});
}

bool CompactPanel::ownsMenu(QWidget *widget) const {
	const auto contains = [&](QWidget *menu) {
		return menu && (menu == widget || menu->isAncestorOf(widget));
	};
	return contains(_orderController->menu()) || contains(_speedController->menu());
}

void CompactPanel::raiseMenus() {
	for (const auto menu : { _orderController->menu(), _speedController->menu() }) {
		if (menu && !menu->isHidden()) {
			menu->raise();
		}
	}
}

void CompactPanel::showSource() {
	const auto document = _track.audio();
	const auto item = document ? document->owner().message(_track.contextId()) : nullptr;
	if (!item) {
		return;
	}
	_collapseRequests.fire({});
	if (const auto window = Core::App().windowFor(item->history()->peer)) {
		if (const auto controller = window->sessionController()) {
			controller->showMessage(item);
			return;
		}
	}
	_controller->showMessage(item);
}

rpl::producer<> CompactPanel::collapseRequests() const {
	return _collapseRequests.events();
}

void CompactPanel::saveCover() {
	const auto cover = _cover->image();
	if (cover.isNull()) {
		return;
	}
	// 保存对话框归属主窗口，收起播放器后仍可保存当前封面。
	const auto parent = window();
	FileDialog::GetWritePath(parent, tr::extras_MusicSaveCover(tr::now),
		u"PNG (*.png);;"_q + FileDialog::AllFilesFilter(),
		filedialogDefaultName(u"cover"_q, u".png"_q),
		crl::guard(parent, [=](const QString &path) {
			if (!path.isEmpty() && !cover.save(path, "PNG")) {
				Ui::Toast::Show(tr::extras_MusicSaveFailed(tr::now));
			}
		}));
}

rpl::producer<> CompactPanel::playlistRequests() const {
	return _playlistRequests.events();
}

int CompactPanel::resizeGetHeight(int width) {
	return st::extrasMusicCompactHeight;
}

void CompactPanel::resizeEvent(QResizeEvent *event) {
	RpWidget::resizeEvent(event);
	const auto padding = st::extrasMusicCompactPadding;
	_close->moveToRight(style::ConvertScale(7), style::ConvertScale(5));
	_collapse->moveToRight(style::ConvertScale(37), style::ConvertScale(5));
	_save->moveToRight(style::ConvertScale(67), style::ConvertScale(5));
	const auto textLeft = padding + st::extrasMusicCompactCover + style::ConvertScale(13);
	const auto textWidth = std::max(0, width() - textLeft - padding);
	_source->setText(st::extrasMusicCompactSource.font->elided(_sourceName, textWidth));
	_source->moveToLeft(textLeft, style::ConvertScale(91));
	_progress->setGeometry(padding, style::ConvertScale(120), width() - 2 * padding, style::ConvertScale(16));
	_play->move((width() - _play->width()) / 2, style::ConvertScale(167));
	_previous->moveToLeft(_play->x() - _previous->width() - style::ConvertScale(8), style::ConvertScale(167));
	_next->moveToLeft(_play->x() + _play->width() + style::ConvertScale(8), style::ConvertScale(167));
	_order->moveToLeft(style::ConvertScale(10), style::ConvertScale(174));
	_repeat->moveToRight(style::ConvertScale(10), style::ConvertScale(167));
	const auto footerTop = height() - style::ConvertScale(42);
	_volume->moveToLeft(style::ConvertScale(8), footerTop);
	_volumeSlider->setGeometry(
		style::ConvertScale(45),
		footerTop + style::ConvertScale(7),
		std::max(style::ConvertScale(32), width() - style::ConvertScale(175)),
		style::ConvertScale(16));
	_playlist->moveToRight(style::ConvertScale(47), footerTop);
	_speed->moveToRight(style::ConvertScale(10), footerTop);
	updateMenuGeometry();
}

void CompactPanel::paintEvent(QPaintEvent *event) {
	auto p = Painter(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(st::menuBg);
	p.drawRoundedRect(rect(), style::ConvertScale(8), style::ConvertScale(8));
	p.setPen(st::windowSubTextFg);
	p.setFont(st::extrasMusicCompactSmall);
	p.drawTextLeft(st::extrasMusicCompactPadding, style::ConvertScale(11), width(), _playing
		? tr::extras_MusicPlaying(tr::now) : tr::extras_MusicPaused(tr::now));
	const auto padding = st::extrasMusicCompactPadding;
	const auto cover = QRect(padding, style::ConvertScale(44),
		st::extrasMusicCompactCover, st::extrasMusicCompactCover);
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	auto clip = QPainterPath();
	clip.addRoundedRect(cover, style::ConvertScale(8), style::ConvertScale(8));
	p.setClipPath(clip);
	p.fillRect(cover, st::windowBgOver);
	const auto &image = _cover->image();
	if (image.isNull()) {
		(playbackType() == kSong ? st::extrasMusicPlaceholder
			: _track.audio() && _track.audio()->isVideoMessage()
			? st::extrasMediaVideo : st::extrasMediaVoice).paintInCenter(p, cover);
	} else {
		const auto fitted = image.size().scaled(cover.size(), Qt::KeepAspectRatio);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(QRect(cover.center() - QPoint(fitted.width() / 2,
			fitted.height() / 2), fitted), image);
	}
	p.restore();
	const auto textLeft = cover.right() + style::ConvertScale(14);
	const auto textWidth = std::max(0, width() - textLeft - padding);
	p.setPen(st::windowFg);
	p.setFont(st::extrasMusicCompactTitle);
	p.drawTextLeft(textLeft, style::ConvertScale(47), width(), st::extrasMusicCompactTitle->elided(_title, textWidth));
	p.setPen(st::windowSubTextFg);
	p.setFont(st::extrasMusicCompactSmall);
	p.drawTextLeft(textLeft, style::ConvertScale(73), width(), st::extrasMusicCompactSmall->elided(_performer, textWidth));
	p.drawTextLeft(padding, style::ConvertScale(140), width(), _elapsed);
	p.drawTextRight(padding, style::ConvertScale(140), width(), _duration);
	p.fillRect(0, height() - style::ConvertScale(49), width(),
		st::lineWidth, st::menuSeparatorFg);
	if (playbackType() != kSong) {
		p.drawTextLeft(padding, height() - style::ConvertScale(33), width(),
			tr::extras_MediaSpeed(tr::now));
	}
}

} // namespace Extras::MusicPlayer
