#include "extras/features/music_player/music_player_floating.h"

#include "extras/features/music_player/music_player_panel.h"
#include "base/timer.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_panel.h"
#include "styles/style_extras_styles.h"
#include "styles/style_media_player.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/tooltip.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <cmath>

namespace Extras::MusicPlayer {
namespace {

constexpr auto kSong = AudioMsgId::Type::Song;
constexpr auto kFrameInterval = crl::time(80);

} // namespace

class FloatingButton final : public Ui::RippleButton, public Ui::AbstractTooltipShower {
public:
	explicit FloatingButton(QWidget *parent)
	: RippleButton(parent, st::defaultRippleAnimation)
	, _animation([=] { update(); }) {
		const auto side = st::extrasMusicFloatingSize + 2 * st::extrasMusicFloatingShadow;
		resize(side, side);
		setObjectName(u"music.floating"_q);
		setAccessibleName(tr::extras_MusicExpand(tr::now));
		setFocusPolicy(Qt::StrongFocus);
	}

	void setTrack(QString title, bool playing) {
		_title = std::move(title);
		_playing = playing;
		refreshAnimation();
		update();
	}

	void setExpanded(bool expanded) {
		_expanded = expanded;
		Ui::Tooltip::Hide();
		update();
	}

	void refreshAnimation() {
		if (_playing && isVisible() && !anim::Disabled()
			&& QGuiApplication::applicationState() == Qt::ApplicationActive) {
			if (!_animation.isActive()) {
				_animation.callEach(kFrameInterval);
			}
		} else {
			_animation.cancel();
		}
	}

	QString tooltipText() const override {
		return _expanded ? QString() : _title + u"\n"_q + (_playing
			? tr::extras_MusicPlaying(tr::now) : tr::extras_MusicPaused(tr::now));
	}
	QPoint tooltipPos() const override { return mapToGlobal(rect().center()); }
	bool tooltipWindowActive() const override { return window()->isActiveWindow(); }

protected:
	void showEvent(QShowEvent *event) override {
		RippleButton::showEvent(event);
		refreshAnimation();
	}
	void hideEvent(QHideEvent *event) override {
		_animation.cancel();
		Ui::Tooltip::Hide();
		RippleButton::hideEvent(event);
	}
	void enterEventHook(QEnterEvent *event) override {
		RippleButton::enterEventHook(event);
		Ui::Tooltip::Show(700, this);
	}
	void leaveEventHook(QEvent *event) override {
		Ui::Tooltip::Hide();
		RippleButton::leaveEventHook(event);
	}
	QImage prepareRippleMask() const override {
		const auto side = st::extrasMusicFloatingSize;
		return Ui::RippleAnimation::EllipseMask(QSize(side, side));
	}
	QPoint prepareRippleStartPosition() const override {
		const auto shadow = st::extrasMusicFloatingShadow;
		return mapFromGlobal(QCursor::pos()) - QPoint(shadow, shadow);
	}
	void paintEvent(QPaintEvent *event) override {
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto shadow = st::extrasMusicFloatingShadow;
		const auto circle = QRectF(shadow, shadow,
			st::extrasMusicFloatingSize, st::extrasMusicFloatingSize);
		p.setPen(Qt::NoPen);
		for (auto spread = shadow; spread > 0; --spread) {
			auto color = st::windowShadowFg->c;
			color.setAlpha(3);
			p.setBrush(color);
			p.drawEllipse(circle.adjusted(-spread, -spread / 2., spread, spread));
		}
		p.setBrush((_expanded || isOver()) ? st::lightButtonBgOver : st::windowBg);
		p.setPen(QPen(st::windowBgRipple, st::lineWidth));
		p.drawEllipse(circle);
		paintRipple(p, shadow, shadow);
		if (!_playing) {
			st::mediaPlayerPauseIcon.paintInCenter(p, circle.toRect());
			return;
		}
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowActiveTextFg);
		const auto now = (_animation.isActive() ? crl::now() : crl::time(0)) / 650.;
		const auto stroke = style::ConvertScale(3);
		const auto spacing = style::ConvertScale(6);
		for (auto i = 0; i != 3; ++i) {
			const auto height = style::ConvertScale(12)
				+ style::ConvertScale(6) * std::sin(now + i * 1.7);
			const auto rect = QRectF(circle.center().x() + (i - 1) * spacing - stroke / 2.,
				circle.center().y() - height / 2., stroke, height);
			p.drawRoundedRect(rect, stroke / 2., stroke / 2.);
		}
	}

private:
	base::Timer _animation;
	QString _title;
	bool _playing = false;
	bool _expanded = false;
};

FloatingPlayer::FloatingPlayer(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller)
: QObject(parent.get())
, _parent(parent)
, _panelParent(controller->widget()->bodyWidget())
, _controller(controller)
, _button(parent)
, _dropdown(_panelParent, st::extrasMusicCompactDropdown)
, _playlist(_panelParent, controller) {
	_dropdown->setObjectName(u"music.popup"_q);
	_dropdown->setAutoHiding(false);
	_dropdown->hide();
	_playlist->setObjectName(u"music.queue"_q);
	_playlist->setAutoHiding(false);
	_button->setClickedCallback([=] { toggle(); });
	_playlist->sizeValue(
	) | rpl::on_next([=] { updatePosition(); }, _lifetime);
	rpl::merge(
		_parent->positionValue() | rpl::to_empty,
		_panelParent->sizeValue() | rpl::to_empty
	) | rpl::on_next([=] { updatePosition(); }, _lifetime);
	_controller->window().boxShownValue(
	) | rpl::filter([](bool shown) { return shown; }
	) | rpl::on_next([=] { collapse(); }, _lifetime);
	using namespace Media::Player;
	rpl::merge(
		instance()->updatedNotifier() | rpl::to_empty,
		instance()->trackChanged() | rpl::to_empty,
		instance()->stops(kSong),
		instance()->stops(AudioMsgId::Type::Voice)
	) | rpl::on_next([=] { refresh(); }, _lifetime);
	instance()->closePlayerRequests(
	) | rpl::on_next([=] { collapse(); }, _lifetime);
	qApp->installEventFilter(this);
	// 原生窗口激活信号必须进入应用事件队列后才能启动动画。
	QObject::connect(qApp, &QGuiApplication::applicationStateChanged, this,
		[=] {
			_button->refreshAnimation();
			if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
				collapse();
			}
		}, Qt::QueuedConnection);
	refresh();
}

FloatingPlayer::~FloatingPlayer() {
	qApp->removeEventFilter(this);
	_lifetime.destroy();
	if (_panel) {
		_panel->dismiss();
	}
	_dropdown.destroy();
	_playlist.destroy();
	_button.destroy();
}

void FloatingPlayer::setAvailableRect(QRect rect) {
	_available = rect;
	if (_available.isEmpty()) {
		collapse();
	}
	updatePosition();
	_button->setVisible(_active && !_available.isEmpty());
}

void FloatingPlayer::refresh() {
	using namespace Media::Player;
	_type = instance()->getActiveType();
	const auto type = _type;
	const auto current = instance()->current(type);
	const auto document = current.audio();
	const auto state = instance()->getState(type);
	const auto item = document ? document->owner().message(current.contextId()) : nullptr;
	const auto expiring = item && item->media() && item->media()->ttlSeconds();
	_active = document && !expiring && state.id == current && !IsStoppedOrStopping(state.state);
	if (!_active) {
		_button->hide();
		collapse();
		return;
	}
	const auto song = document->song();
	const auto title = type == AudioMsgId::Type::Voice
		? (document->isVideoMessage()
			? tr::extras_MusicVideo(tr::now) : tr::extras_MusicVoice(tr::now))
		: (song && !song->title.isEmpty()) ? song->title : document->filename();
	_button->setTrack(title, ShowPauseIcon(state.state));
	if (_panel) {
		_panel->setPlaybackType(type);
	}
	if (type != kSong && _playlistShown) {
		_playlistShown = _playlistOnly = false;
		_playlist->hideIgnoringEnterEvents();
		updatePosition();
	}
	_button->setVisible(!_available.isEmpty());
	raise();
}

void FloatingPlayer::ensurePanel() {
	if (_panel) {
		return;
	}
	_panel = _dropdown->setOwnedWidget(object_ptr<CompactPanel>(
		_dropdown, _panelParent, _controller, _type));
	_panel->collapseRequests(
	) | rpl::on_next([=] { collapse(); }, _panel->lifetime());
	_panel->playlistRequests(
	) | rpl::on_next([=] { togglePlaylist(); }, _panel->lifetime());
}

void FloatingPlayer::toggle() {
	if (!_active) {
		return;
	}
	if (_playlistOnly) {
		_playlistOnly = _playlistShown = false;
		_playlist->hideIgnoringEnterEvents();
		_dropdown->showAnimated(Ui::PanelAnimation::Origin::BottomRight);
		return;
	}
	if (_shown) {
		collapse();
		return;
	}
	ensurePanel();
	_shown = true;
	_button->setExpanded(true);
	updatePosition();
	raise();
}

void FloatingPlayer::collapse() {
	_shown = _playlistShown = _playlistOnly = false;
	_button->setExpanded(false);
	_playlist->hideIgnoringEnterEvents();
	_dropdown->hideAnimated();
	if (_panel) {
		_panel->dismiss();
	}
}

void FloatingPlayer::togglePlaylist() {
	_playlistShown = !_playlistShown;
	if (!_playlistShown) {
		_playlistOnly = false;
		_playlist->hideIgnoringEnterEvents();
		return;
	}
	updatePosition();
	_playlist->showFromOther();
	if (_playlist->isHidden()) {
		_playlistShown = _playlistOnly = false;
	}
	updatePosition();
	raise();
}

void FloatingPlayer::updatePosition() {
	if (_available.isEmpty() || _positioning) {
		return;
	}
	_positioning = true;
	const auto guard = gsl::finally([&] { _positioning = false; });
	const auto margin = st::extrasMusicFloatingMargin;
	const auto buttonLeft = std::max(_available.x() + (_available.width() - _button->width()) / 2,
		_available.right() + 1 - margin - _button->width());
	_button->move(buttonLeft, _available.bottom() + 1 - margin - _button->height());
	if (_panel) {
		updatePanelPosition();
	}
}

void FloatingPlayer::updatePanelPosition() {
	const auto margin = st::extrasMusicFloatingMargin;
	const auto &padding = _dropdown->st().padding;
	const auto bounds = _panelParent->rect().marginsRemoved(
		QMargins(margin, margin, margin, margin));
	const auto contentWidth = std::min(st::extrasMusicCompactWidth,
		bounds.width() - padding.left() - padding.right());
	_panel->resizeToWidth(contentWidth);
	const auto anchor = _button->mapTo(_panelParent, QPoint(
		_button->width(),
		st::extrasMusicFloatingShadow - st::extrasMusicFloatingGap));
	// 面板只受主窗口边界限制，聊天列表仅提供入口位置。
	_dropdown->resizeToContent();
	_dropdown->move(
		std::clamp(anchor.x() + padding.right() - _dropdown->width(),
			bounds.left(), bounds.right() + 1 - _dropdown->width()),
		std::clamp(anchor.y() + padding.bottom() - _dropdown->height(),
			bounds.top(), bounds.bottom() + 1 - _dropdown->height()));
	_panel->setMenuBounds(bounds);
	if (_playlistShown) {
		updatePlaylistPosition(contentWidth,
			_dropdown->y() + _dropdown->height(), bounds);
	}
	if (_shown && !_playlistOnly && _dropdown->isHidden()) {
		_dropdown->showAnimated(Ui::PanelAnimation::Origin::BottomRight);
	}
}

void FloatingPlayer::updatePlaylistPosition(
		int contentWidth,
		int bottom,
		const QRect &bounds) {
	const auto &padding = _dropdown->st().padding;
	const auto above = _dropdown->y() - bounds.top();
	_playlistOnly = (above < style::ConvertScale(96));
	const auto queueBottom = _playlistOnly
		? bottom
		: _dropdown->y() + padding.top();
	_playlist->setAvailableSize(QSize(
		contentWidth + padding.left() + padding.right(),
		std::max(1, queueBottom - bounds.top())));
	_playlist->move(_dropdown->x(), queueBottom - _playlist->height());
	if (_playlistOnly) {
		_dropdown->hideFast();
	}
}

void FloatingPlayer::raise() {
	_button->raise();
	if (_shown) {
		_dropdown->raise();
	}
	if (_playlistShown) {
		_playlist->raise();
	}
	if (_panel) {
		_panel->raiseMenus();
	}
}

bool FloatingPlayer::contains(QWidget *widget) const {
	return widget && (widget == _button.data() || _button->isAncestorOf(widget)
		|| widget == _dropdown.data() || _dropdown->isAncestorOf(widget)
		|| widget == _playlist.data() || _playlist->isAncestorOf(widget)
		|| (_panel && _panel->ownsMenu(widget)));
}

bool FloatingPlayer::eventFilter(QObject *object, QEvent *event) {
	if ((object == _parent.get() || object == _panelParent.get())
		&& event->type() == QEvent::Hide) {
		collapse();
	}
	if (!_shown) {
		return false;
	}
	const auto widget = qobject_cast<QWidget*>(object);
	if (event->type() == QEvent::MouseButtonPress && widget && !contains(widget)) {
		// 原生窗口事件交给控件分发；向父控件传播的点击按实际命中位置判断。
		const auto global = static_cast<QMouseEvent*>(event)->globalPos();
		const auto window = widget->window();
		if (!contains(window->childAt(window->mapFromGlobal(global)))) {
			collapse();
		}
	} else if (event->type() == QEvent::KeyPress
		&& widget
		&& widget->window() == _panelParent->window()
		&& !_controller->window().isLayerShown()
		&& static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape
		&& (!_panel || !_panel->ownsMenu(widget))) {
		if (_playlistOnly) {
			toggle();
		} else if (_playlistShown) {
			togglePlaylist();
		} else {
			collapse();
			_button->setFocus(Qt::OtherFocusReason);
		}
		return true;
	}
	return false;
}

} // namespace Extras::MusicPlayer
