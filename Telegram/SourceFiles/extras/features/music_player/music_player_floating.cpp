#include "extras/features/music_player/music_player_floating.h"

#include "extras/features/music_player/music_player_panel.h"
#include "extras/features/music_player/music_player_widgets.h"
#include "extras/ui/utils/extras_tooltip.h"
#include "crl/crl_on_main.h"
#include "core/application.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "styles/style_extras_styles.h"
#include "styles/style_chat.h"
#include "ui/painter.h"
#include "ui/widgets/shadow.h"
#include "window/window_session_controller.h"

#include <QtCore/QCoreApplication>

namespace Extras::MusicPlayer {
namespace {

constexpr auto kSong = AudioMsgId::Type::Song;

} // namespace

class FloatingButton final : public Ui::AbstractButton {
public:
	explicit FloatingButton(QWidget *parent)
	: AbstractButton(parent)
	, _shadow(st::defaultPopupMenu.shadow)
	, _wave(new Waveform(this)) {
		const auto margins = _shadow.extend();
		const auto side = st::extrasMusicFloatingSize;
		resize(side + margins.left() + margins.right(), side + margins.top() + margins.bottom());
		const auto waveSize = style::ConvertScale(24);
		_wave->setGeometry(margins.left() + (side - waveSize) / 2,
			margins.top() + (side - waveSize) / 2, waveSize, waveSize);
		setObjectName(u"music.floating"_q);
		setAccessibleName(tr::extras_MusicExpand(tr::now));
		// 鼠标点击不保留焦点底色，键盘仍可用 Tab 聚焦入口。
		setFocusPolicy(Qt::TabFocus);
		ExtrasUi::installTooltip(this, [=] {
			return _title + u"\n"_q + (_playing
				? tr::extras_MusicPlaying(tr::now) : tr::extras_MusicPaused(tr::now));
		}, st::topBarTooltip);
	}

	void setTrack(QString title, bool playing) {
		_title = std::move(title);
		_playing = playing;
		_wave->setPlaying(playing);
		update();
	}

protected:
	void paintEvent(QPaintEvent *event) override {
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto circle = rect().marginsRemoved(_shadow.extend());
		p.setPen(Qt::NoPen);
		_shadow.paint(p, circle, st::extrasMusicFloatingSize / 2);
		p.setBrush(st::windowBg);
		p.drawEllipse(circle);
		if (isOver() || isDown() || hasFocus()) {
			p.setBrush(softColor());
			p.drawEllipse(circle.adjusted(isDown() ? 1 : 0, isDown() ? 1 : 0,
				isDown() ? -1 : 0, isDown() ? -1 : 0));
		}
	}

private:
	Ui::BoxShadow _shadow;
	Waveform *_wave = nullptr;
	QString _title;
	bool _playing = false;
};

FloatingPlayer::FloatingPlayer(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller)
: QObject(parent.get())
, _parent(parent)
, _controller(controller)
, _button(parent) {
	_button->setClickedCallback([=] { toggle(); });
	using namespace Media::Player;
	rpl::merge(instance()->updatedNotifier() | rpl::to_empty,
		instance()->trackChanged() | rpl::to_empty,
		instance()->stops(kSong), instance()->stops(AudioMsgId::Type::Voice)
	) | rpl::on_next([=] {
		// 切歌会先通知旧音轨停止，等当前操作完成再判断窗口是否关闭。
		crl::on_main(this, [=] { refresh(); });
	}, _lifetime);
	instance()->closePlayerRequests(
	) | rpl::on_next([=] { collapse(); }, _lifetime);
	Core::App().passcodeLockChanges(
	) | rpl::on_next([=] { refresh(); }, _lifetime);
	QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
		this, [=] { _panel.reset(); });
	refresh();
}

FloatingPlayer::~FloatingPlayer() {
	_lifetime.destroy();
	_panel.reset();
	_button.destroy();
}

void FloatingPlayer::setAvailableRect(QRect rect) {
	_available = rect;
	updatePosition();
	_button->setVisible(_active && !_available.isEmpty());
}

void FloatingPlayer::refresh() {
	using namespace Media::Player;
	_type = instance()->getActiveType();
	const auto current = instance()->current(_type);
	const auto document = current.audio();
	const auto state = instance()->getState(_type);
	if (_closedTrack && (current != _closedTrack || ShowPauseIcon(state.state))) {
		_closedTrack = AudioMsgId();
	}
	const auto item = document ? document->owner().message(current.contextId()) : nullptr;
	const auto expiring = item && item->media() && item->media()->ttlSeconds();
	// 跳转会重建播放实例，窗口可见性只比较文件与消息身份。
	const auto sameTrack = state.id.audio() == current.audio()
		&& state.id.contextId() == current.contextId();
	_active = document && !expiring && !Core::App().passcodeLocked()
		&& sameTrack && !IsStoppedOrStopping(state.state)
		&& current != _closedTrack;
	if (!_active) {
		_button->hide();
		collapse();
		return;
	}
	const auto song = document->song();
	const auto title = _type == AudioMsgId::Type::Voice
		? (document->isVideoMessage() ? tr::extras_MusicVideo(tr::now) : tr::extras_MusicVoice(tr::now))
		: (song && !song->title.isEmpty()) ? song->title : document->filename();
	_button->setTrack(title, ShowPauseIcon(state.state));
	if (_panel) { _panel->setPlaybackType(_type); }
	_button->setVisible(!_available.isEmpty());
}

void FloatingPlayer::toggle() {
	if (!_active) { return; }
	if (_panel && _panel->isVisible()) { collapse(); return; }
	if (!_panel) {
		_panel = std::make_unique<CompactPanel>(_controller, _type);
		_panel->collapseRequests(
		) | rpl::on_next([=] { collapse(); }, _panel->lifetime());
		_panel->closeRequests(
		) | rpl::on_next([=](const AudioMsgId &track) {
			_closedTrack = track;
			refresh();
		}, _panel->lifetime());
	}
	_panel->showMini(_button->mapToGlobal(QPoint(_button->width(), -st::extrasMusicFloatingGap)));
}

void FloatingPlayer::collapse() {
	if (_panel) { _panel->dismiss(); }
}

void FloatingPlayer::updatePosition() {
	if (_available.isEmpty()) { return; }
	const auto margin = st::extrasMusicFloatingMargin;
	const auto buttonLeft = std::max(_available.x() + (_available.width() - _button->width()) / 2,
		_available.right() + 1 - margin - _button->width());
	_button->move(buttonLeft, _available.bottom() + 1 - margin - _button->height());
}

void FloatingPlayer::raise() { _button->raise(); }

} // namespace Extras::MusicPlayer
