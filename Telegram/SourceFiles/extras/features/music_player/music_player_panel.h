#pragma once

#include "data/data_audio_msg_id.h"
#include "ui/rp_widget.h"
#include "base/object_ptr.h"

namespace Ui {
class IconButton;
class LinkButton;
class MediaSlider;
} // namespace Ui

namespace Media::Player {
class OrderController;
class SpeedButton;
class SpeedController;
struct TrackState;
} // namespace Media::Player

namespace Media::View {
class PlaybackProgress;
} // namespace Media::View

namespace Window {
class SessionController;
} // namespace Window

namespace Extras::MusicPlayer {

class TrackCover;
class CompactPlayButton;

class CompactPanel final : public Ui::RpWidget {
public:
	CompactPanel(QWidget *parent,
		not_null<Ui::RpWidget*> menuParent,
		not_null<Window::SessionController*> controller,
		AudioMsgId::Type type);
	~CompactPanel();

	void dismiss();
	void stop();
	void setMenuBounds(QRect bounds);
	void setPlaybackType(AudioMsgId::Type type);
	void raiseMenus();
	[[nodiscard]] bool ownsMenu(QWidget *widget) const;
	[[nodiscard]] rpl::producer<> collapseRequests() const;
	[[nodiscard]] rpl::producer<> playlistRequests() const;

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	int resizeGetHeight(int width) override;

private:
	void refreshTrack();
	void refreshPlayback(const Media::Player::TrackState &state);
	void refreshRepeat();
	void refreshPlaylist();
	void seek(float64 progress, bool finished);
	void cancelSeek(bool resume);
	void showSource();
	void saveCover();
	void updateMenuGeometry();
	void setupDropdownSwitching();
	[[nodiscard]] AudioMsgId::Type playbackType() const;

	const not_null<Window::SessionController*> _controller;
	AudioMsgId::Type _type;
	TrackCover *_cover = nullptr;
	AudioMsgId _track;
	QRect _menuBounds;
	QString _title;
	QString _performer;
	QString _sourceName;
	QString _elapsed;
	QString _duration;
	crl::time _durationMs = 0;
	bool _playing = false;
	bool _seeking = false;
	bool _resumeAfterSeeking = false;

	object_ptr<Ui::IconButton> _save;
	object_ptr<Ui::IconButton> _collapse;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::IconButton> _previous;
	object_ptr<CompactPlayButton> _play;
	object_ptr<Ui::IconButton> _next;
	object_ptr<Ui::IconButton> _repeat;
	object_ptr<Ui::IconButton> _order;
	object_ptr<Ui::IconButton> _volume;
	object_ptr<Ui::IconButton> _playlist;
	object_ptr<Media::Player::SpeedButton> _speed;
	object_ptr<Ui::LinkButton> _source;
	object_ptr<Ui::MediaSlider> _progress;
	object_ptr<Ui::MediaSlider> _volumeSlider;
	std::unique_ptr<Media::View::PlaybackProgress> _playback;
	std::unique_ptr<Media::Player::OrderController> _orderController;
	std::unique_ptr<Media::Player::SpeedController> _speedController;
	rpl::event_stream<> _collapseRequests;
	rpl::event_stream<> _playlistRequests;
};

} // namespace Extras::MusicPlayer
