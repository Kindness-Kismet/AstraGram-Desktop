#include "extras/features/music_player/music_player_panel.h"

#include "extras/features/music_player/music_player_cover.h"
#include "extras/features/music_player/music_player_queue.h"
#include "extras/features/music_player/music_player_widgets.h"
#include "extras/ui/utils/extras_tooltip.h"
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
#include "mainwindow.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_dropdown.h"
#include "media/player/media_player_instance.h"
#include "media/view/media_view_playback_progress.h"
#include "styles/style_extras_styles.h"
#include "styles/style_media_player.h"
#include "styles/style_chat.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtGui/QCloseEvent>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QScreen>
#include <QtCore/QFileInfo>
#include <array>
#include <cmath>

namespace Extras::MusicPlayer {
namespace {

using namespace Media::Player;
constexpr auto kSong = AudioMsgId::Type::Song;
constexpr auto kHeaderHeight = 48;
constexpr auto kControlsHeight = 240;

int scaled(int value) { return style::ConvertScale(value); }

} // namespace

CompactPanel::CompactPanel(
	not_null<Window::SessionController*> controller,
	AudioMsgId::Type type)
: RpWidget(nullptr)
, _controller(controller)
, _type(type)
, _cover(new TrackCover(this, [=] { refreshCover(); }))
, _save(this, PlayerIcon::Download, u"music.download"_q, tr::extras_MusicSaveCover(tr::now))
, _coverButton(this)
, _collapse(this, PlayerIcon::Collapse, u"music.collapse"_q, tr::extras_MusicCollapse(tr::now))
, _minimize(this, PlayerIcon::Minimize, u"music.minimize"_q, tr::extras_MusicMinimize(tr::now))
, _close(this, PlayerIcon::Close, u"music.close"_q, tr::extras_MusicClose(tr::now))
, _expand(this, PlayerIcon::Expand, u"music.expand"_q, tr::extras_MusicExpand(tr::now))
, _previous(this, PlayerIcon::Previous, u"music.previous"_q, tr::extras_MusicPrevious(tr::now))
, _play(this, PlayerIcon::Play, u"music.play"_q, tr::extras_MusicPlay(tr::now))
, _next(this, PlayerIcon::Next, u"music.next"_q, tr::extras_MusicNext(tr::now))
, _repeat(this, PlayerIcon::Repeat, u"music.repeat"_q, tr::extras_MusicRepeatOff(tr::now))
, _order(this, PlayerIcon::Order, u"music.order"_q, tr::lng_sr_playback_order(tr::now))
, _volume(this, PlayerIcon::Volume, u"music.volume"_q, tr::lng_ringtones_box_volume(tr::now))
, _playlist(this, PlayerIcon::Queue, u"music.playlist"_q, tr::extras_MusicPlaylist(tr::now))
, _speed(this, PlayerIcon::None, u"music.speed"_q, tr::extras_MediaSpeed(tr::now))
, _source(this, QString(), st::extrasMusicCompactSource)
, _progress(this)
, _volumeSlider(this, st::extrasMusicProgress)
, _scroll(this, st::extrasMusicQueueScroll)
, _wave(new Waveform(this))
, _playback(std::make_unique<Media::View::PlaybackProgress>())
, _shadow(std::make_unique<Ui::BoxShadow>(st::defaultPopupMenu.shadow)) {
	// 无宿主的工具窗口可以离开主窗口边界，生命周期由 FloatingPlayer 管理。
	setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	setObjectName(u"music.window"_q);
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	_source->setObjectName(u"music.source"_q);
	ExtrasUi::installTooltip(_source.data(), [] { return tr::extras_MusicSource(tr::now); }, st::topBarTooltip);
	_progress->setObjectName(u"music.progress"_q);
	_volumeSlider->setObjectName(u"music.volumeSlider"_q);
	_scroll->setObjectName(u"music.queueScroll"_q);
	_progress->setAccessibleName(tr::extras_MusicProgress(tr::now));
	_volumeSlider->setAccessibleName(tr::lng_ringtones_box_volume(tr::now));
	const auto refreshColors = [=] {
		const auto colors = Ui::MediaSlider::ColorOverrides{
			.activeFg = accentColor(),
			.seekFg = accentColor(),
			.inactiveFg = anim::color(st::windowBg->c, st::windowBgOver->c, 0.6),
		};
		_progress->setColorOverrides(colors);
		_volumeSlider->setColorOverrides(colors);
		update();
	};
	style::PaletteChanged() | rpl::on_next(refreshColors, lifetime());
	refreshColors();
	_queue = _scroll->setOwnedWidget(object_ptr<PlayerQueue>(_scroll));
	_play->setPrimary(true);
	_save->setClickedCallback([=] { saveCover(); });
	_coverButton->setClickedCallback([=] {
		if (_expanded) { instance()->playPauseCancelClicked(_type); }
		else { setExpanded(true); }
	});
	_collapse->setClickedCallback([=] { setExpanded(false); });
	_minimize->setClickedCallback([=] { _collapseRequests.fire({}); });
	_close->setClickedCallback([=] {
		instance()->pause(_type);
		_closeRequests.fire_copy(_track);
	});
	_expand->setClickedCallback([=] { setExpanded(true); });
	_previous->setClickedCallback([=] { instance()->previous(_type); });
	_next->setClickedCallback([=] { instance()->next(_type); });
	_play->setClickedCallback([=] { instance()->playPauseCancelClicked(_type); });
	_playlist->setClickedCallback([=] { togglePlaylist(); });
	_source->setClickedCallback([=] { showSource(); });
	_repeat->setClickedCallback([] {
		auto &settings = Core::App().settings();
		const auto mode = settings.playerRepeatMode();
		settings.setPlayerRepeatMode(mode == Media::RepeatMode::None
			? Media::RepeatMode::All : mode == Media::RepeatMode::All
			? Media::RepeatMode::One : Media::RepeatMode::None);
		Core::App().saveSettingsDelayed();
	});
	_order->setClickedCallback([] {
		auto &settings = Core::App().settings();
		const auto mode = settings.playerOrderMode();
		settings.setPlayerOrderMode(mode == Media::OrderMode::Default
			? Media::OrderMode::Reverse : mode == Media::OrderMode::Reverse
			? Media::OrderMode::Shuffle : Media::OrderMode::Default);
		Core::App().saveSettingsDelayed();
	});
	const auto lookupSpeed = [=](bool) {
		const auto &settings = Core::App().settings();
		return (_type == kSong)
			? settings.audioPlaybackSpeed()
			: settings.voicePlaybackSpeed();
	};
	const auto changeSpeed = [=](float64 speed) {
		auto &settings = Core::App().settings();
		if (_type == kSong) {
			settings.setAudioPlaybackSpeed(speed);
		} else {
			settings.setVoicePlaybackSpeed(speed);
		}
		instance()->updatePlaybackSpeed();
		Core::App().saveSettingsDelayed();
	};
	_speedController = std::make_unique<SpeedController>(
		_speed.data(),
		st::mediaPlayerSpeedButton,
		this,
		[](bool) {},
		lookupSpeed,
		changeSpeed);
	_speedController->setShowOnHover(false);
	_speedController->setClickCallback([=] {
		const auto current = lookupSpeed(false);
		constexpr auto speeds = std::array{ 0.75, 1., 1.25, 1.5, 2. };
		const auto i = ranges::find_if(speeds, [&](double speed) { return speed > current + 0.01; });
		const auto next = i == end(speeds) ? speeds.front() : *i;
		changeSpeed(next);
	});
	_speedController->setApplySpeed([=](float64 speed) {
		if (instance()->getActiveType() == _type) {
			instance()->setPlaybackSpeed(speed);
		}
	});
	_speedController->realtimeValue(
	) | rpl::on_next([=](float64 speed) {
		const auto text = QString::number(std::round(speed * 100.) / 100.)
			+ u"×"_q;
		_speed->setText(text);
		_speed->setActive(speed != 1.);
		_speed->setLabel(tr::lng_mediaview_playback_speed(
			tr::now, lt_speed, text));
	}, lifetime());
	events() | rpl::on_next([=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Hide
			|| event->type() == QEvent::WindowDeactivate) {
			_speedController->reloadFromLookup();
		}
	}, lifetime());
	_volume->setClickedCallback([] {
		auto &settings = Core::App().settings();
		const auto volume = settings.songVolume() > 0. ? 0. : settings.rememberedSongVolume();
		settings.setSongVolume(volume);
		mixer()->setSongVolume(volume);
		Core::App().saveSettingsDelayed();
	});
	_volumeSlider->setChangeProgressCallback([](float64 volume) {
		Core::App().settings().setSongVolume(volume);
		mixer()->setSongVolume(volume);
	});
	_volumeSlider->setChangeFinishedCallback([](float64 volume) {
		if (volume > 0.) { Core::App().settings().setRememberedSongVolume(volume); }
		Core::App().saveSettingsDelayed();
	});
	const auto refreshVolume = [=] {
		const auto volume = Core::App().settings().songVolume();
		if (!_volumeSlider->isChanging()) { _volumeSlider->setValue(volume); }
		_volume->setIcon(volume > 0. ? PlayerIcon::Volume : PlayerIcon::Muted);
	};
	Core::App().settings().songVolumeChanges(
	) | rpl::on_next(refreshVolume, lifetime());
	refreshVolume();
	_progress->setChangeProgressCallback([=](float64 value) { seek(value, false); });
	_progress->setChangeFinishedCallback([=](float64 value) { seek(value, true); });
	_playback->setValueChangedCallback([=](float64 value, float64 received) {
		if (!_seeking) { _progress->setValue(value, received); }
	});
	rpl::merge(Core::App().settings().audioPlaybackSpeedChanges(),
		Core::App().settings().voicePlaybackSpeedChanges()
	) | rpl::on_next([=] { refreshSpeed(); }, lifetime());
	Core::App().settings().playerRepeatModeValue(
	) | rpl::on_next([=] { refreshRepeat(); }, lifetime());
	Core::App().settings().playerOrderModeValue(
	) | rpl::on_next([=] { refreshOrder(); refreshPlaylist(); }, lifetime());
	rpl::merge(instance()->playlistChanges(kSong),
		instance()->playlistChanges(AudioMsgId::Type::Voice)
	) | rpl::on_next([=] { refreshPlaylist(); }, lifetime());
	rpl::merge(instance()->trackChanged() | rpl::to_empty,
		instance()->updatedNotifier() | rpl::to_empty,
		instance()->playbackAllowedValue() | rpl::to_empty
	) | rpl::on_next([=] { refreshTrack(); }, lifetime());
	rpl::merge(instance()->stops(kSong), instance()->stops(AudioMsgId::Type::Voice)
	) | rpl::on_next([=] { cancelSeek(false); refreshTrack(); }, lifetime());
	refreshTrack();
	updateSize();
}

CompactPanel::~CompactPanel() { lifetime().destroy(); }

void CompactPanel::setPlaybackType(AudioMsgId::Type type) {
	if (_type == type) { return; }
	cancelSeek(false);
	_speedController->reloadFromLookup();
	_type = type;
	refreshTrack();
	updateSize();
}

void CompactPanel::refreshTrack() {
	const auto track = instance()->current(_type);
	const auto document = track.audio();
	if (!document) { return; }
	if (_track != track) {
		_seeking = false;
		_track = track;
		_cover->setTrack(track);
		const auto song = document->song();
		_title = song && !song->title.isEmpty() ? song->title : document->filename();
		_performer = song ? song->performer : QString();
		const auto item = document->owner().message(track.contextId());
		if (_type == AudioMsgId::Type::Voice) {
			_title = document->isVideoMessage() ? tr::extras_MusicVideo(tr::now) : tr::extras_MusicVoice(tr::now);
			_performer = item ? item->author()->shortName() : QString();
		}
		_sourceName = item ? item->history()->peer->shortName() : QString();
		setWindowTitle(_title);
		setAccessibleName(_title);
		refreshSpeed();
	}
	refreshPlayback(instance()->getState(_type));
	refreshPlaylist();
	update();
}

void CompactPanel::refreshPlayback(const TrackState &state) {
	if (!state.id || state.id != _track) { return; }
	_playing = ShowPauseIcon(state.state);
	_play->setIcon(state.id.audio()->loading() ? PlayerIcon::Cancel
		: _playing ? PlayerIcon::Pause : PlayerIcon::Play);
	_play->setLabel(_playing ? tr::extras_MusicPause(tr::now) : tr::extras_MusicPlay(tr::now));
	_wave->setPlaying(_playing);
	refreshCover();
	_durationMs = state.frequency > 0 && state.length > 0 ? state.length * 1000 / state.frequency : 0;
	_progress->setDisabled(_durationMs <= 0 || !instance()->playbackAllowed());
	_playback->updateState(state);
	_duration = Ui::FormatDurationText(_durationMs / 1000);
	if (!_seeking) {
		_elapsed = Ui::FormatDurationText(state.frequency > 0
			? std::max(int64(0), state.position / state.frequency) : 0);
	}
}

void CompactPanel::refreshCover() {
	const auto document = _track.audio();
	_coverButton->setImage(_cover->image(), document && document->isVideoMessage(), _type != kSong);
	_coverButton->setPlayback(_expanded, _playing);
	const auto label = _expanded
		? (_playing ? tr::extras_MusicPause(tr::now) : tr::extras_MusicPlay(tr::now))
		: tr::extras_MusicExpand(tr::now);
	_coverButton->setAccessibleName(label);
	_save->setDisabled(_cover->originalImage().isNull());
	// 格式来自真实文件，不把普通音频标成高音质或无损。
	const auto format = document ? QFileInfo(document->filename()).suffix().toUpper() : QString();
	_coverButton->setLabels(_type == kSong ? _repeat->accessibleName() : QString(),
		format.size() <= 5 ? format : QString());
}

void CompactPanel::refreshRepeat() {
	const auto mode = Core::App().settings().playerRepeatMode();
	_repeat->setIcon(mode == Media::RepeatMode::One ? PlayerIcon::RepeatOne : PlayerIcon::Repeat);
	_repeat->setActive(mode != Media::RepeatMode::None);
	_repeat->setLabel(mode == Media::RepeatMode::None ? tr::extras_MusicRepeatOff(tr::now)
		: mode == Media::RepeatMode::One ? tr::extras_MusicRepeatOne(tr::now) : tr::extras_MusicRepeatAll(tr::now));
	refreshCover();
	update();
}

void CompactPanel::refreshOrder() {
	const auto mode = Core::App().settings().playerOrderMode();
	_order->setIcon(mode == Media::OrderMode::Default ? PlayerIcon::Order
		: mode == Media::OrderMode::Reverse ? PlayerIcon::Reverse : PlayerIcon::Shuffle);
	_order->setActive(mode != Media::OrderMode::Default);
	_order->setLabel(mode == Media::OrderMode::Default ? tr::extras_MusicOrderDefault(tr::now)
		: mode == Media::OrderMode::Reverse ? tr::lng_audio_player_reverse(tr::now) : tr::lng_audio_player_shuffle(tr::now));
}

void CompactPanel::refreshSpeed() {
	_speedController->reloadFromLookup();
}

void CompactPanel::refreshPlaylist() {
	const auto music = _type == kSong;
	const auto allowed = instance()->playbackAllowed();
	_play->setDisabled(!allowed);
	_previous->setDisabled(!allowed || !instance()->previousAvailable(_type));
	_next->setDisabled(!allowed || !instance()->nextAvailable(_type));
	_previous->setLabel(music ? tr::extras_MusicPrevious(tr::now) : tr::extras_MediaPrevious(tr::now));
	_next->setLabel(music ? tr::extras_MusicNext(tr::now) : tr::extras_MediaNext(tr::now));
	_queue->refresh();
	_playlist->setLabel(tr::extras_MusicPlaylist(tr::now) + u" · "_q + QString::number(_queue->count()));
	_playlist->setText(QString::number(_queue->count()));
	_playlist->setActive(_queueOpen);
	layoutContents();
}

void CompactPanel::seek(float64 progress, bool finished) {
	if (_durationMs <= 0) { return; }
	if (!_seeking) {
		_resumeAfterSeeking = ShowPauseIcon(instance()->getState(_type).state);
		_seeking = true;
		instance()->startSeeking(_type);
	}
	_elapsed = Ui::FormatDurationText(int(progress * _durationMs / 1000));
	update();
	if (finished) {
		_seeking = false;
		instance()->finishSeeking(_type, progress);
		if (!_resumeAfterSeeking) { instance()->pause(_type); }
	}
}

void CompactPanel::cancelSeek(bool resume) {
	if (!_seeking) { return; }
	_seeking = false;
	instance()->cancelSeeking(_type);
	if (resume && _resumeAfterSeeking && IsPaused(instance()->getState(_type).state)) {
		instance()->play(_type);
	}
}

void CompactPanel::dismiss() {
	cancelSeek(true);
	_dragging = false;
	_resizeEdges = {};
	unsetCursor();
	hide();
}

void CompactPanel::showMini(QPoint anchor) {
	setExpanded(false);
	if (!_positioned) {
		move(anchor - QPoint(width(), height()));
		_positioned = true;
	}
	constrainToScreen();
	show();
	raise();
}

void CompactPanel::setExpanded(bool expanded) {
	cancelSeek(true);
	const auto bottomRight = geometry().bottomRight();
	_expanded = expanded;
	refreshCover();
	updateSize();
	if (_positioned) {
		move(bottomRight - QPoint(width() - 1, height() - 1));
		constrainToScreen();
	}
	if (_expanded) { _scroll->scrollToY(_queue->currentTop()); }
	setFocus(Qt::OtherFocusReason);
	update();
}

void CompactPanel::togglePlaylist() {
	_queueOpen = !_queueOpen;
	_playlist->setActive(_queueOpen);
	layoutContents();
	update();
	if (_queueOpen) {
		_scroll->scrollToY(_queue->currentTop());
	}
}

void CompactPanel::updateSize() {
	const auto margins = _shadow->extend();
	const auto shadow = QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
	if (!_expanded) {
		setFixedSize(QSize(scaled(460), scaled(66)) + shadow);
		layoutContents();
		update();
		return;
	}
	const auto bounds = screenBounds();
	const auto minimum = (QSize(scaled(340), scaled(480)) + shadow).boundedTo(bounds.size());
	const auto requested = _expandedSize.isEmpty()
		? QSize(scaled(376) + shadow.width(), std::min(scaled(560) + shadow.height(),
			_controller->window().widget()->height() - scaled(24)))
		: _expandedSize;
	setMinimumSize(0, 0);
	setMaximumSize(bounds.size());
	setMinimumSize(minimum);
	resize(requested.expandedTo(minimum).boundedTo(bounds.size()));
	layoutContents();
	update();
}

QRect CompactPanel::screenBounds() const {
	const auto screen = QGuiApplication::screenAt(geometry().center());
	return (screen ? screen : QGuiApplication::primaryScreen())->availableGeometry();
}

void CompactPanel::constrainToScreen() {
	const auto bounds = screenBounds();
	move(std::clamp(x(), bounds.left(), std::max(bounds.left(), bounds.right() + 1 - width())),
		std::clamp(y(), bounds.top(), std::max(bounds.top(), bounds.bottom() + 1 - height())));
}

void CompactPanel::showSource() {
	const auto document = _track.audio();
	const auto item = document ? document->owner().message(_track.contextId()) : nullptr;
	if (!item) { return; }
	_controller->showMessage(item);
}

rpl::producer<> CompactPanel::collapseRequests() const { return _collapseRequests.events(); }
rpl::producer<AudioMsgId> CompactPanel::closeRequests() const { return _closeRequests.events(); }

void CompactPanel::saveCover() {
	const auto cover = _cover->originalImage();
	if (cover.isNull()) { return; }
	FileDialog::GetWritePath(this, tr::extras_MusicSaveCover(tr::now),
		u"PNG (*.png);;"_q + FileDialog::AllFilesFilter(),
		filedialogDefaultName(u"cover"_q, u".png"_q),
		crl::guard(this, [=](const QString &path) {
			if (!path.isEmpty() && !cover.save(path, "PNG")) { Ui::Toast::Show(tr::extras_MusicSaveFailed(tr::now)); }
		}));
}

QRect CompactPanel::cardRect() const { return rect().marginsRemoved(_shadow->extend()); }

QRect CompactPanel::coverRect() const {
	return _coverRect.translated(cardRect().topLeft());
}

void CompactPanel::resizeEvent(QResizeEvent *event) {
	RpWidget::resizeEvent(event);
	if (_expanded) {
		_expandedSize = size();
	}
	layoutContents();
}

void CompactPanel::layoutContents() {
	const auto music = _type == kSong;
	const auto card = cardRect();
	const auto top = card.topLeft();
	_playerRect = QRect(QPoint(), card.size());
	_queueRect = {};
	if (_expanded && music && _queueOpen) {
		// 宽窗口把列表放右侧；窄窗口在固定外框内分配封面与列表空间。
		if (card.width() >= scaled(680)) {
			const auto playerWidth = std::min(scaled(500), card.width() * 56 / 100);
			_playerRect.setWidth(playerWidth);
			_queueRect = QRect(playerWidth, 0, card.width() - playerWidth, card.height());
		} else {
			const auto remaining = card.height() - scaled(kHeaderHeight + kControlsHeight);
			const auto queueHeight = std::clamp(remaining / 2, scaled(100), scaled(260));
			_playerRect.setHeight(card.height() - queueHeight);
			_queueRect = QRect(0, _playerRect.height(), card.width(), queueHeight);
		}
	}
	_controlsTop = _playerRect.height() - scaled(kControlsHeight);
	const auto coverHeight = std::max(0, _controlsTop - scaled(kHeaderHeight));
	const auto side = std::max(0, std::min(_playerRect.width() - scaled(40), coverHeight));
	_coverRect = _expanded
		? QRect((_playerRect.width() - side) / 2, scaled(kHeaderHeight) + (coverHeight - side) / 2, side, side)
		: QRect(scaled(14), scaled(8), scaled(44), scaled(44));
	_save->setVisible(_expanded && music);
	_coverButton->setGeometry(coverRect());
	_collapse->setVisible(_expanded);
	_expand->setVisible(!_expanded);
	_wave->setVisible(_expanded);
	_order->setVisible(_expanded && music);
	_repeat->setVisible(music);
	_volume->setVisible(_expanded && music);
	_volumeSlider->setVisible(_expanded && music);
	_playlist->setVisible(_expanded && music);
	_source->setVisible(_expanded && !_sourceName.isEmpty());
	_speed->setVisible(_track.changeablePlaybackSpeed() && Media::Audio::SupportsSpeedControl());
	_scroll->setVisible(!_queueRect.isEmpty());
	_progress->setMini(!_expanded);
	const auto place = [&](QWidget *widget, int x, int y, int w, int h) {
		widget->setGeometry(QRect(top + QPoint(x, y), QSize(scaled(w), scaled(h))));
	};
	if (!_expanded) {
		place(_previous, scaled(170), scaled(13), 30, 34); place(_play, scaled(202), scaled(12), 36, 36);
		place(_next, scaled(242), scaled(13), 30, 34); place(_speed, scaled(278), scaled(16), 40, 28);
		place(_repeat, scaled(320), scaled(16), 28, 28); place(_expand, scaled(354), scaled(16), 28, 28);
		place(_minimize, scaled(386), scaled(16), 28, 28);
		place(_close, scaled(418), scaled(16), 28, 28); place(_progress, 0, scaled(56), 460, 10);
		return;
	}
	const auto width = _playerRect.width();
	place(_wave, scaled(20), scaled(16), 16, 16);
	place(_save, width - scaled(144), scaled(10), 28, 28);
	place(_collapse, width - scaled(112), scaled(10), 28, 28);
	place(_minimize, width - scaled(80), scaled(10), 28, 28);
	place(_close, width - scaled(48), scaled(10), 28, 28);
	const auto controls = [&](QWidget *widget, int x, int y, int w, int h) {
		place(widget, x, _controlsTop + scaled(y), w, h);
	};
	_source->setText(st::extrasMusicCompactSource.font->elided(_sourceName, width - scaled(40)));
	_source->move(top + QPoint(scaled(20), _controlsTop + scaled(58)));
	_progress->setGeometry(top.x() + scaled(20), top.y() + _controlsTop + scaled(82), width - scaled(40), scaled(18));
	controls(_play, (width - scaled(48)) / 2, 134, 48, 48);
	controls(_previous, width / 2 - scaled(80), 140, 36, 36);
	controls(_next, width / 2 + scaled(44), 140, 36, 36);
	controls(_order, scaled(20), 140, 36, 36);
	controls(_repeat, width - scaled(56), 140, 36, 36);
	controls(_volume, scaled(16), 201, 28, 28);
	_volumeSlider->setGeometry(top.x() + scaled(52), top.y() + _controlsTop + scaled(206), width - scaled(196), scaled(18));
	controls(_speed, width - scaled(122), 199, 42, 32);
	controls(_playlist, width - scaled(72), 199, 56, 32);
	if (!_queueRect.isEmpty()) {
		_scroll->setGeometry(_queueRect.adjusted(scaled(8), scaled(38), -scaled(8), -scaled(6)).translated(top));
		_queue->resize(_scroll->width(), _queue->height());
	}
}

void CompactPanel::paintEvent(QPaintEvent *event) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto card = cardRect();
	p.setPen(Qt::NoPen);
	_shadow->paint(p, card, scaled(10));
	p.setBrush(st::windowBg);
	p.setPen(QPen(st::menuSeparatorFg, style::ConvertScale(1)));
	p.drawRoundedRect(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), scaled(10), scaled(10));
	p.translate(card.topLeft());
	if (!_expanded) {
		p.setPen(st::windowFg); p.setFont(st::extrasMusicMiniTitle);
		p.drawTextLeft(scaled(70), scaled(10), card.width(), st::extrasMusicMiniTitle->elided(_title, scaled(96)));
		p.setPen(st::windowSubTextFg); p.setFont(st::extrasMusicCompactSmall);
		const auto time = _elapsed + u" / "_q + _duration;
		const auto remaining = scaled(96) - st::extrasMusicCompactSmall->width(time + u" · "_q);
		const auto artist = st::extrasMusicCompactSmall->elided(_performer, std::max(0, remaining));
		p.drawTextLeft(scaled(70), scaled(34), card.width(), artist.isEmpty() ? time : artist + u" · "_q + time);
		p.fillRect(scaled(274), scaled(21), scaled(1), scaled(20), st::menuSeparatorFg);
		return;
	}
	p.setPen(st::windowSubTextFg); p.setFont(st::extrasMusicCompactSmall);
	const auto width = _playerRect.width();
	const auto textWidth = width - scaled(40);
	p.drawTextLeft(scaled(44), scaled(17), width, st::extrasMusicCompactSmall->elided(
		_playing ? tr::extras_MusicPlaying(tr::now) : tr::extras_MusicPaused(tr::now), width - scaled(196)));
	p.setPen(st::windowFg); p.setFont(st::extrasMusicCompactTitle);
	p.drawTextLeft(scaled(20), _controlsTop + scaled(8), width,
		st::extrasMusicCompactTitle->elided(_title, textWidth));
	p.setPen(accentColor()); p.setFont(st::extrasMusicArtist);
	p.drawTextLeft(scaled(20), _controlsTop + scaled(36), width, st::extrasMusicArtist->elided(_performer, textWidth));
	p.setFont(st::extrasMusicCompactSmall);
	p.setPen(st::windowSubTextFg);
	p.drawTextLeft(scaled(20), _controlsTop + scaled(104), width, _elapsed);
	p.drawTextRight(scaled(20), _controlsTop + scaled(104), width, _duration);
	p.save();
	p.setClipRect(0, _controlsTop + scaled(194), width, scaled(46));
	p.setPen(Qt::NoPen);
	p.setBrush(anim::color(st::windowBg->c, st::windowBgOver->c, 0.3));
	p.drawRoundedRect(QRect(QPoint(), card.size()), scaled(10), scaled(10));
	p.restore();
	p.fillRect(0, _controlsTop + scaled(194), width, scaled(1), st::menuSeparatorFg);
	if (_speed->isVisible()) {
		p.setPen(QPen(st::menuSeparatorFg, scaled(1))); p.setBrush(st::windowBg);
		p.drawRoundedRect(QRectF(_speed->geometry().translated(-card.topLeft())).adjusted(0.5, 0.5, -0.5, -0.5), scaled(10), scaled(10));
	}
	if (_type != kSong) {
		p.setPen(st::windowSubTextFg);
		p.drawTextLeft(scaled(20), _controlsTop + scaled(210), width, tr::extras_MediaSpeed(tr::now));
	}
	if (_queueRect.isEmpty()) { return; }
	p.fillRect(_queueRect.x(), _queueRect.y(),
		_queueRect.x() ? scaled(1) : _queueRect.width(),
		_queueRect.x() ? _queueRect.height() : scaled(1), st::menuSeparatorFg);
	p.translate(_queueRect.topLeft());
	p.setFont(st::extrasMusicQueueTitle); p.setPen(st::windowFg);
	p.drawTextLeft(scaled(20), scaled(12), _queueRect.width(), tr::extras_MusicPlaylist(tr::now));
	p.setFont(st::extrasMusicCompactSmall); p.setPen(st::windowSubTextFg);
	p.drawTextRight(scaled(20), scaled(12), _queueRect.width(),
		st::extrasMusicCompactSmall->elided(_repeat->accessibleName(), _queueRect.width() / 2 - scaled(20)));
}

void CompactPanel::closeEvent(QCloseEvent *event) {
	event->ignore();
	instance()->pause(_type);
	_closeRequests.fire_copy(_track);
}

void CompactPanel::mousePressEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) { return; }
	_resizeEdges = resizeEdges(event->pos());
	if (_resizeEdges) {
		_resizeOrigin = event->globalPosition().toPoint();
		_resizeGeometry = geometry();
		return;
	}
	if (cardRect().contains(event->pos())) {
		_dragging = true;
		_dragOffset = event->globalPosition().toPoint() - pos();
		setFocus(Qt::MouseFocusReason);
	}
}

void CompactPanel::mouseMoveEvent(QMouseEvent *event) {
	if (_resizeEdges) {
		const auto delta = event->globalPosition().toPoint() - _resizeOrigin;
		const auto bounds = screenBounds();
		auto rect = _resizeGeometry;
		if (_resizeEdges & Qt::LeftEdge) {
			rect.setLeft(std::clamp(rect.left() + delta.x(), bounds.left(), rect.right() + 1 - minimumWidth()));
		}
		if (_resizeEdges & Qt::RightEdge) {
			rect.setRight(std::clamp(rect.right() + delta.x(), rect.left() + minimumWidth() - 1, bounds.right()));
		}
		if (_resizeEdges & Qt::TopEdge) {
			rect.setTop(std::clamp(rect.top() + delta.y(), bounds.top(), rect.bottom() + 1 - minimumHeight()));
		}
		if (_resizeEdges & Qt::BottomEdge) {
			rect.setBottom(std::clamp(rect.bottom() + delta.y(), rect.top() + minimumHeight() - 1, bounds.bottom()));
		}
		setGeometry(rect);
		return;
	}
	if (_dragging) {
		move(event->globalPosition().toPoint() - _dragOffset);
		return;
	}
	const auto edges = resizeEdges(event->pos());
	const auto horizontal = edges & (Qt::LeftEdge | Qt::RightEdge);
	const auto vertical = edges & (Qt::TopEdge | Qt::BottomEdge);
	if (horizontal && vertical) {
		setCursor((edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge))
			? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
	} else if (horizontal || vertical) {
		setCursor(horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor);
	} else {
		unsetCursor();
	}
}

Qt::Edges CompactPanel::resizeEdges(QPoint point) const {
	if (!_expanded) { return {}; }
	const auto card = cardRect();
	const auto corner = scaled(12);
	const auto nearX = point.x() < card.left() + corner || point.x() > card.right() - corner;
	const auto nearY = point.y() < card.top() + corner || point.y() > card.bottom() - corner;
	const auto grip = (nearX && nearY) ? corner : scaled(5);
	auto result = Qt::Edges();
	if (point.x() < card.left() + grip) { result |= Qt::LeftEdge; }
	if (point.x() > card.right() - grip) { result |= Qt::RightEdge; }
	if (point.y() < card.top() + grip) { result |= Qt::TopEdge; }
	if (point.y() > card.bottom() - grip) { result |= Qt::BottomEdge; }
	return result;
}

void CompactPanel::leaveEventHook(QEvent *event) {
	RpWidget::leaveEventHook(event);
	if (!_resizeEdges && !_dragging) { unsetCursor(); }
}

void CompactPanel::contextMenuEvent(QContextMenuEvent *event) {
	if (!coverRect().contains(event->pos()) || _cover->originalImage().isNull()) { return; }
	_coverMenu = std::make_unique<Ui::PopupMenu>(this, st::defaultPopupMenu);
	_coverMenu->addAction(tr::extras_MusicSaveCover(tr::now), [=] { saveCover(); });
	_coverMenu->popup(event->globalPos());
}

void CompactPanel::mouseReleaseEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) { return; }
	_resizeEdges = {};
	unsetCursor();
	if (_dragging) {
		_dragging = false;
		updateSize();
		constrainToScreen();
	}
}

void CompactPanel::keyPressEvent(QKeyEvent *event) {
	if (event->key() == Qt::Key_Escape) {
		if (_expanded) { setExpanded(false); } else { _collapseRequests.fire({}); }
	} else if (event->key() == Qt::Key_Space) {
		instance()->playPause(_type);
	} else {
		RpWidget::keyPressEvent(event);
	}
}

} // namespace Extras::MusicPlayer
