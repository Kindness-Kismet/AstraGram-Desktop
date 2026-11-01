#include "extras/features/music_player/music_player_queue.h"

#include "extras/features/music_player/music_player_widgets.h"
#include "extras/ui/utils/extras_tooltip.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "styles/style_extras_styles.h"
#include "styles/style_chat.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"

namespace Extras::MusicPlayer {
namespace {

constexpr auto kSong = AudioMsgId::Type::Song;

} // namespace

class QueueRow final : public Ui::AbstractButton {
public:
	QueueRow(QWidget *parent, AudioMsgId track, int index)
	: AbstractButton(parent)
	, _track(track)
	, _index(index)
	, _wave(new Waveform(this))
	, _media(track.audio()->createMediaView()) {
		const auto document = track.audio();
		const auto song = document->song();
		_title = song && !song->title.isEmpty() ? song->title : document->filename();
		_artist = song ? song->performer : QString();
		_duration = Ui::FormatDurationText(document->duration() / 1000);
		_media->thumbnailWanted(track.contextId());
		document->session().downloaderTaskFinished(
		) | rpl::on_next([=] { update(); }, lifetime());
		setObjectName(u"music.track."_q + QString::number(index));
		setAccessibleName(_title + u" · "_q + _artist);
		ExtrasUi::installTooltip(this, [=] { return _title + u"\n"_q + _artist; }, st::topBarTooltip);
		setFocusPolicy(Qt::StrongFocus);
		setIsListItem(true);
		_wave->setGeometry(style::ConvertScale(10), style::ConvertScale(19),
			style::ConvertScale(18), style::ConvertScale(16));
		setClickedCallback([=] {
			const auto player = Media::Player::instance();
			player->playPause(_track, player->playlistContext(kSong));
		});
	}

	void refresh(const AudioMsgId &current, bool playing) {
		_current = current == _track;
		_wave->setVisible(_current);
		_wave->setPlaying(playing && _current);
		setDisabled(!Media::Player::instance()->playbackAllowed());
		update();
	}

protected:
	void paintEvent(QPaintEvent *event) override {
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto s = [](int value) { return style::ConvertScale(value); };
		p.setPen(Qt::NoPen);
		if (_current || isOver() || hasFocus()) {
			p.setBrush(_current ? softColor() : st::windowBgOver->c);
			p.drawRoundedRect(rect(), s(10), s(10));
		}
		p.setFont(st::extrasMusicCompactSmall);
		p.setPen(st::windowSubTextFg);
		if (!_current) {
			p.drawText(QRect(s(6), 0, s(26), height()), Qt::AlignCenter,
				QString::number(_index + 1).rightJustified(2, '0'));
		}
		const auto image = _media->thumbnail();
		_cover.paint(p, QRect(s(40), s(11), s(32), s(32)),
			image ? image->original() : QImage(), false, false, 6);
		const auto available = width() - s(146);
		p.setPen(_current ? accentColor() : st::windowFg->c);
		p.setFont(st::extrasMusicQueueTitle);
		p.drawTextLeft(s(84), s(10), width(), st::extrasMusicQueueTitle->elided(_title, available));
		p.setPen(_current ? accentColor() : st::windowSubTextFg->c);
		p.setFont(st::extrasMusicCompactSmall);
		p.drawTextLeft(s(84), s(30), width(), st::extrasMusicCompactSmall->elided(_artist, available));
		p.drawTextRight(s(12), s(20), width(), _duration);
	}

private:
	AudioMsgId _track;
	int _index = 0;
	Waveform *_wave = nullptr;
	std::shared_ptr<Data::DocumentMedia> _media;
	QString _title;
	QString _artist;
	QString _duration;
	bool _current = false;
	CoverPainter _cover;
};

PlayerQueue::PlayerQueue(QWidget *parent) : RpWidget(parent) {
	setObjectName(u"music.queue"_q);
}

void PlayerQueue::refresh() {
	const auto player = Media::Player::instance();
	const auto tracks = player->playlist(kSong);
	if (tracks != _tracks) {
		for (const auto row : _rows) { delete row; }
		_rows.clear();
		_tracks = tracks;
		for (auto i = 0; i != int(_tracks.size()); ++i) {
			const auto row = new QueueRow(this, _tracks[i], i);
			_rows.push_back(row);
			row->show();
		}
		resize(width(), int(_rows.size()) * style::ConvertScale(58));
		QResizeEvent resized(size(), size());
		resizeEvent(&resized);
	}
	const auto current = player->current(kSong);
	const auto playing = Media::Player::ShowPauseIcon(player->getState(kSong).state);
	for (const auto row : _rows) { row->refresh(current, playing); }
}

int PlayerQueue::count() const { return int(_tracks.size()); }

int PlayerQueue::currentTop() const {
	const auto i = ranges::find(_tracks, Media::Player::instance()->current(kSong));
	return i == end(_tracks) ? 0 : int(i - begin(_tracks)) * style::ConvertScale(58);
}

void PlayerQueue::resizeEvent(QResizeEvent *event) {
	RpWidget::resizeEvent(event);
	for (auto i = 0; i != int(_rows.size()); ++i) {
		_rows[i]->setGeometry(0, i * style::ConvertScale(58),
			width() - style::ConvertScale(8), style::ConvertScale(54));
	}
}

} // namespace Extras::MusicPlayer
