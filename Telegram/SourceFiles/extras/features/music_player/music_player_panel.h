#pragma once

#include "data/data_audio_msg_id.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/object_ptr.h"

namespace Ui {
class LinkButton;
class MediaSlider;
class ScrollArea;
class BoxShadow;
class PopupMenu;
} // namespace Ui

namespace Media::Player { struct TrackState; }
namespace Media::View { class PlaybackProgress; }
namespace Window { class SessionController; }

namespace Extras::MusicPlayer {

class TrackCover;
class PlayerButton;
class PlayerQueue;
class PlayerProgress;
class Waveform;
class CoverButton;

class CompactPanel final : public Ui::RpWidget {
public:
	CompactPanel(not_null<Window::SessionController*> controller, AudioMsgId::Type type);
	~CompactPanel();
	void dismiss();
	void setPlaybackType(AudioMsgId::Type type);
	void showMini(QPoint anchor);
	[[nodiscard]] rpl::producer<> collapseRequests() const;

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void closeEvent(QCloseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;

private:
	void refreshTrack();
	void refreshPlayback(const Media::Player::TrackState &state);
	void refreshRepeat();
	void refreshOrder();
	void refreshSpeed();
	void refreshPlaylist();
	void refreshCover();
	void seek(float64 progress, bool finished);
	void cancelSeek(bool resume);
	void showSource();
	void saveCover();
	void setExpanded(bool expanded);
	void togglePlaylist();
	void updateSize();
	void constrainToScreen();
	[[nodiscard]] QRect cardRect() const;
	[[nodiscard]] QRect coverRect() const;
	[[nodiscard]] int fullHeight() const;
	[[nodiscard]] int playlistHeight() const;

	const not_null<Window::SessionController*> _controller;
	AudioMsgId::Type _type;
	TrackCover *_cover = nullptr;
	AudioMsgId _track;
	QString _title;
	QString _performer;
	QString _sourceName;
	QString _elapsed;
	QString _duration;
	crl::time _durationMs = 0;
	bool _playing = false;
	bool _seeking = false;
	bool _resumeAfterSeeking = false;
	bool _expanded = false;
	bool _queueOpen = true;
	bool _dragging = false;
	bool _positioned = false;
	QPoint _dragOffset;
	int _queueHeight = 0;
	int _coverSide = 0;
	Ui::Animations::Simple _queueAnimation;

	object_ptr<PlayerButton> _save;
	object_ptr<CoverButton> _coverButton;
	object_ptr<PlayerButton> _collapse;
	object_ptr<PlayerButton> _close;
	object_ptr<PlayerButton> _expand;
	object_ptr<PlayerButton> _previous;
	object_ptr<PlayerButton> _play;
	object_ptr<PlayerButton> _next;
	object_ptr<PlayerButton> _repeat;
	object_ptr<PlayerButton> _order;
	object_ptr<PlayerButton> _volume;
	object_ptr<PlayerButton> _playlist;
	object_ptr<PlayerButton> _speed;
	object_ptr<Ui::LinkButton> _source;
	object_ptr<PlayerProgress> _progress;
	object_ptr<Ui::MediaSlider> _volumeSlider;
	object_ptr<Ui::ScrollArea> _scroll;
	PlayerQueue *_queue = nullptr;
	Waveform *_wave = nullptr;
	std::unique_ptr<Media::View::PlaybackProgress> _playback;
	std::unique_ptr<Ui::BoxShadow> _shadow;
	std::unique_ptr<Ui::PopupMenu> _coverMenu;
	rpl::event_stream<> _collapseRequests;
};

} // namespace Extras::MusicPlayer
