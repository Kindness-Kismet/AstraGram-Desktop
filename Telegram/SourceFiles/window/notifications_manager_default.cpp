/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/notifications_manager_default.h"

#include "platform/platform_notifications_manager.h"
#include "platform/platform_specific.h"
#include "core/application.h"
#include "core/ui_integration.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/shadow.h"
#include "ui/emoji_config.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/ui_utility.h"
#include "data/data_premium_limits.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "data/stickers/data_custom_emoji.h"
#include "dialogs/ui/dialogs_layout.h"
#include "window/window_controller.h"
#include "storage/file_download.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_item_preview.h"
#include "base/platform/base_platform_last_input.h"
#include "base/call_delayed.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QPainterPath>
#include <QtGui/QScreen>

// AyuGram includes
#include "extras/extras_settings.h"
#include "extras/features/streamer_mode/streamer_mode.h"
#include "extras/utils/telegram_helpers.h"


namespace Window {
namespace Notifications {
namespace Default {
namespace {

[[nodiscard]] int notifyWidth() {
	const auto corner = Core::App().settings().notificationsCorner();
	return Core::Settings::IsTopCenterCorner(corner) ? st::notifyWidth * 1.5 : st::notifyWidth;
}

// 窗口尺寸 = 卡片尺寸 + 四周阴影边距。
[[nodiscard]] int windowWidth() {
	return notifyWidth() + 2 * ShadowSkip();
}

[[nodiscard]] int windowHeight(int cardHeight) {
	return cardHeight + 2 * ShadowSkip();
}

// 起点按卡片边缘对齐屏幕，所以要抵掉窗口里的阴影边距。
[[nodiscard]] QPoint notificationStartPosition() {
	const auto corner = Core::App().settings().notificationsCorner();
	const auto r = NotificationDisplayRect(Core::App().activePrimaryWindow());
	const auto isLeft = Core::Settings::IsLeftCorner(corner);
	const auto isTop = Core::Settings::IsTopCorner(corner);
	const auto skip = ShadowSkip();
	auto x = (isLeft == rtl())
		? (r.x() + r.width() - windowWidth() - st::notifyDeltaX + skip)
		: (r.x() + st::notifyDeltaX - skip);
	const auto y = isTop ? r.y() : (r.y() + r.height());

	if (Core::Settings::IsTopCenterCorner(corner)) {
		x = (r.x() + r.width() / 2 - windowWidth() / 2);
	}

	return QPoint(x, y);
}

internal::Widget::Direction notificationShiftDirection() {
	auto isTop = Core::Settings::IsTopCorner(Core::App().settings().notificationsCorner());
	return isTop ? internal::Widget::Direction::Down : internal::Widget::Direction::Up;
}

} // namespace

std::unique_ptr<Manager> Create(System *system) {
	return std::make_unique<Manager>(system);
}

int ShadowSkip() {
	const auto extend = Ui::BoxShadow::ExtendFor(st::notifyShadow);
	return std::max({
		extend.left(),
		extend.top(),
		extend.right(),
		extend.bottom(),
	});
}

// 底色取主题窗口色；描边是白色高光，暗色下压低。
void PaintCard(QPainter &p, const QRect &card, bool highlighted) {
	static const auto shadow = Ui::BoxShadow(st::notifyShadow);
	const auto radius = std::min(st::notifyRadius, card.height() / 2);
	shadow.paint(p, card, radius);

	const auto dark = st::notificationBg->c.lightness() < 128;
	const auto border = QColor(255, 255, 255, dark ? 31 : 178);
	const auto half = st::notifyBorderWidth / 2.;

	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(highlighted ? st::lightButtonBgOver : st::notificationBg);
	p.drawRoundedRect(card, radius, radius);
	p.setPen(QPen(border, st::notifyBorderWidth));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(
		QRectF(card).marginsRemoved({ half, half, half, half }),
		radius - half,
		radius - half);
}

Manager::Manager(System *system)
: Notifications::Manager(system)
, _inputCheckTimer([=] { checkLastInput(); }) {
	system->settingsChanged(
	) | rpl::on_next([=](ChangeType change) {
		settingsChanged(change);
	}, _lifetime);
}

Manager::QueuedNotification::QueuedNotification(NotificationFields &&fields)
: history(fields.item->history())
, topicRootId(fields.item->topicRootId())
, peer(history->peer)
, reaction(fields.reactionId)
, author(!fields.reactionFrom
	? fields.item->notificationHeader()
	: (fields.reactionFrom != peer)
	? fields.reactionFrom->name()
	: QString())
, item((fields.forwardedCount < 2) ? fields.item.get() : nullptr)
, forwardedCount(fields.forwardedCount)
, fromScheduled(reaction.empty() && (fields.item->out() || peer->isSelf())
	&& fields.item->isFromScheduled()) {
}

QPixmap Manager::hiddenUserpicPlaceholder() const {
	if (_hiddenUserpicPlaceholder.isNull()) {
		const auto ratio = style::DevicePixelRatio();
		_hiddenUserpicPlaceholder = Ui::PixmapFromImage(
			LogoNoMargin().scaled(
				st::notifyPhotoSize * ratio,
				st::notifyPhotoSize * ratio,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation));
		_hiddenUserpicPlaceholder.setDevicePixelRatio(ratio);
	}
	return _hiddenUserpicPlaceholder;
}

bool Manager::hasReplyingNotification() const {
	for (const auto &notification : _notifications) {
		if (notification->isReplying()) {
			return true;
		}
	}
	return false;
}

void Manager::settingsChanged(ChangeType change) {
	if (change == ChangeType::Corner) {
		auto startPosition = notificationStartPosition();
		auto shiftDirection = notificationShiftDirection();
		for (const auto &notification : _notifications) {
			notification->updatePosition(startPosition, shiftDirection);
		}
		if (_hideAll) {
			_hideAll->updatePosition(startPosition, shiftDirection);
		}
	} else if (change == ChangeType::MaxCount) {
		int allow = Core::App().settings().notificationsCount();
		for (int i = _notifications.size(); i != 0;) {
			const auto &notification = _notifications[--i];
			if (notification->isUnlinked()) continue;
			if (--allow < 0) {
				notification->unlinkHistory();
			}
		}
		if (allow > 0) {
			for (int i = 0; i != allow; ++i) {
				showNextFromQueue();
			}
		}
	} else if ((change == ChangeType::DemoIsShown)
			|| (change == ChangeType::DemoIsHidden)) {
		_demoIsShown = (change == ChangeType::DemoIsShown);
		_demoMasterOpacity.start(
			[=] { demoMasterOpacityCallback(); },
			_demoIsShown ? 1. : 0.,
			_demoIsShown ? 0. : 1.,
			st::notifyFastAnim);
	}
}

void Manager::demoMasterOpacityCallback() {
	for (const auto &notification : _notifications) {
		notification->updateOpacity();
	}
	if (_hideAll) {
		_hideAll->updateOpacity();
	}
}

float64 Manager::demoMasterOpacity() const {
	return _demoMasterOpacity.value(_demoIsShown ? 0. : 1.);
}

void Manager::checkLastInput() {
	auto replying = hasReplyingNotification();
	auto waiting = false;
	const auto lastInputTime = base::Platform::LastUserInputTimeSupported()
		? std::make_optional(Core::App().lastNonIdleTime())
		: std::nullopt;
	for (const auto &notification : _notifications) {
		if (!notification->checkLastInput(replying, lastInputTime)) {
			waiting = true;
		}
	}
	if (waiting) {
		_inputCheckTimer.callOnce(300);
	}
}

void Manager::startAllHiding() {
	if (!hasReplyingNotification()) {
		for (const auto &notification : _notifications) {
			notification->startHiding();
		}
		if (_hideAll && _queuedNotifications.size() < 2) {
			_hideAll->startHiding();
		}
	}
}

void Manager::stopAllHiding() {
	for (const auto &notification : _notifications) {
		notification->stopHiding();
	}
	if (_hideAll) {
		_hideAll->stopHiding();
	}
}

void Manager::showNextFromQueue() {
	auto guard = gsl::finally([this] {
		if (_positionsOutdated) {
			moveWidgets();
		}
	});
	if (_queuedNotifications.empty()) {
		return;
	}
	int count = Core::App().settings().notificationsCount();
	for (const auto &notification : _notifications) {
		if (notification->isUnlinked()) continue;
		--count;
	}
	if (count <= 0) {
		return;
	}

	auto startPosition = notificationStartPosition();
	auto startShift = 0;
	auto shiftDirection = notificationShiftDirection();
	do {
		auto queued = _queuedNotifications.front();
		_queuedNotifications.pop_front();

		subscribeToSession(&queued.history->session());
		_notifications.push_back(std::make_unique<Notification>(
			this,
			queued.history,
			queued.topicRootId,
			queued.monoforumPeerId,
			queued.peer,
			queued.author,
			queued.item,
			queued.reaction,
			queued.forwardedCount,
			queued.fromScheduled,
			startPosition,
			startShift,
			shiftDirection));
		--count;
	} while (count > 0 && !_queuedNotifications.empty());

	_positionsOutdated = true;
	checkLastInput();
}

void Manager::subscribeToSession(not_null<Main::Session*> session) {
	auto i = _subscriptions.find(session);
	if (i == _subscriptions.end()) {
		i = _subscriptions.emplace(session).first;
		session->account().sessionChanges(
		) | rpl::on_next([=] {
			_subscriptions.remove(session);
		}, i->second.lifetime);
	} else if (i->second.subscription) {
		return;
	}
	session->downloaderTaskFinished(
	) | rpl::on_next([=] {
		auto found = false;
		for (const auto &notification : _notifications) {
			if (const auto history = notification->maybeHistory()) {
				if (&history->session() == session) {
					notification->updatePeerPhoto();
					found = true;
				}
			}
		}
		if (!found) {
			_subscriptions[session].subscription.destroy();
		}
	}, i->second.subscription);
}

void Manager::moveWidgets() {
	// 窗口带阴影边距，起点与间距都要扣掉，卡片才能保持 notifyDeltaY。
	const auto skip = ShadowSkip();
	const auto step = st::notifyDeltaY - 2 * skip;
	auto shift = st::notifyDeltaY - skip;
	int lastShift = 0, lastShiftCurrent = 0, count = 0;
	for (int i = _notifications.size(); i != 0;) {
		const auto &notification = _notifications[--i];
		if (notification->isUnlinked()) continue;

		notification->changeShift(shift);
		shift += notification->height() + step;

		lastShiftCurrent = notification->currentShift();
		lastShift = shift;

		++count;
	}

	if (count > 1 || !_queuedNotifications.empty()) {
		if (!_hideAll) {
			_hideAll = std::make_unique<HideAllButton>(this, notificationStartPosition(), lastShiftCurrent, notificationShiftDirection());
		}
		_hideAll->changeShift(lastShift);
		_hideAll->stopHiding();
	} else if (_hideAll) {
		_hideAll->startHidingFast();
	}
}

void Manager::changeNotificationHeight(Notification *notification, int newHeight) {
	auto deltaHeight = newHeight - notification->height();
	if (!deltaHeight) return;

	notification->addToHeight(deltaHeight);
	auto it = std::find_if(_notifications.cbegin(), _notifications.cend(), [notification](auto &item) {
		return (item.get() == notification);
	});
	if (it != _notifications.cend()) {
		for (auto i = _notifications.cbegin(); i != it; ++i) {
			auto &notification = *i;
			if (notification->isUnlinked()) continue;

			notification->addToShift(deltaHeight);
		}
	}
	if (_hideAll) {
		_hideAll->addToShift(deltaHeight);
	}
}

void Manager::unlinkFromShown(Notification *remove) {
	if (remove) {
		if (remove->unlinkHistory()) {
			_positionsOutdated = true;
		}
	}
	showNextFromQueue();
}

void Manager::removeWidget(internal::Widget *remove) {
	if (remove == _hideAll.get()) {
		_hideAll.reset();
	} else if (remove) {
		const auto it = ranges::find(
			_notifications,
			remove,
			&std::unique_ptr<Notification>::get);
		if (it != end(_notifications)) {
			_notifications.erase(it);
			_positionsOutdated = true;
		}
	}
	showNextFromQueue();
}

void Manager::doShowNotification(NotificationFields &&fields) {
	_queuedNotifications.emplace_back(std::move(fields));
	showNextFromQueue();
}

void Manager::doClearAll() {
	_queuedNotifications.clear();
	for (const auto &notification : _notifications) {
		notification->unlinkHistory();
	}
	showNextFromQueue();
}

void Manager::doClearAllFast() {
	_queuedNotifications.clear();
	base::take(_notifications);
	base::take(_hideAll);
}

void Manager::doClearFromTopic(not_null<Data::ForumTopic*> topic) {
	const auto history = topic->history();
	const auto topicRootId = topic->rootId();
	for (auto i = _queuedNotifications.begin(); i != _queuedNotifications.cend();) {
		if (i->history == history && i->topicRootId == topicRootId) {
			i = _queuedNotifications.erase(i);
		} else {
			++i;
		}
	}
	for (const auto &notification : _notifications) {
		if (notification->unlinkHistory(history, topicRootId, PeerId())) {
			_positionsOutdated = true;
		}
	}
	showNextFromQueue();
}

void Manager::doClearFromSublist(not_null<Data::SavedSublist*> sublist) {
	const auto history = sublist->owningHistory();
	const auto sublistPeerId = sublist->sublistPeer()->id;
	for (auto i = _queuedNotifications.begin(); i != _queuedNotifications.cend();) {
		if (i->history == history && i->monoforumPeerId == sublistPeerId) {
			i = _queuedNotifications.erase(i);
		} else {
			++i;
		}
	}
	for (const auto &notification : _notifications) {
		if (notification->unlinkHistory(history, MsgId(), sublistPeerId)) {
			_positionsOutdated = true;
		}
	}
	showNextFromQueue();
}

void Manager::doClearFromHistory(not_null<History*> history) {
	for (auto i = _queuedNotifications.begin(); i != _queuedNotifications.cend();) {
		if (i->history == history) {
			i = _queuedNotifications.erase(i);
		} else {
			++i;
		}
	}
	for (const auto &notification : _notifications) {
		if (notification->unlinkHistory(history)) {
			_positionsOutdated = true;
		}
	}
	showNextFromQueue();
}

void Manager::doClearFromSession(not_null<Main::Session*> session) {
	for (auto i = _queuedNotifications.begin(); i != _queuedNotifications.cend();) {
		if (&i->history->session() == session) {
			i = _queuedNotifications.erase(i);
		} else {
			++i;
		}
	}
	for (const auto &notification : _notifications) {
		if (notification->unlinkSession(session)) {
			_positionsOutdated = true;
		}
	}
	showNextFromQueue();
}

void Manager::doClearFromItem(not_null<HistoryItem*> item) {
	_queuedNotifications.erase(std::remove_if(_queuedNotifications.begin(), _queuedNotifications.end(), [&](auto &queued) {
		return (queued.item == item);
	}), _queuedNotifications.cend());

	auto showNext = false;
	for (const auto &notification : _notifications) {
		if (notification->unlinkItem(item)) {
			showNext = true;
		}
	}
	if (showNext) {
		// This call invalidates _notifications iterators.
		showNextFromQueue();
	}
}

bool Manager::doSkipToast() const {
	return Platform::Notifications::SkipToastForCustom();
}

void Manager::doMaybePlaySound(Fn<void()> playSound) {
	Platform::Notifications::MaybePlaySoundForCustom(std::move(playSound));
}

void Manager::doMaybeFlashBounce(Fn<void()> flashBounce) {
	Platform::Notifications::MaybeFlashBounceForCustom(std::move(flashBounce));
}

void Manager::doUpdateAll() {
	for (const auto &notification : _notifications) {
		notification->updateNotifyDisplay();
	}
}

Manager::~Manager() {
	clearAllFast();
}

namespace internal {

Widget::Widget(
	not_null<Manager*> manager,
	QPoint startPosition,
	int shift,
	Direction shiftDirection)
: _manager(manager)
, _startPosition(startPosition)
, _direction(shiftDirection)
, _shift(shift)
, _shiftAnimation([=](crl::time now) {
	return shiftAnimationCallback(now);
}) {
	setWindowOpacity(0.);

	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint)
		| Qt::WindowStaysOnTopHint
		| Qt::BypassWindowManagerHint
		| Qt::NoDropShadowWindowHint
		| Qt::Tool);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_TranslucentBackground);
#ifdef _DEBUG
	setObjectName(u"notification"_q);
#endif

	Ui::Platform::InitOnTopPanel(this);

	_a_opacity.start([this] { opacityAnimationCallback(); }, 0., 1., st::notifyFastAnim);
}

QRect Widget::cardRect() const {
	const auto skip = ShadowSkip();
	return rect().marginsRemoved({ skip, skip, skip, skip });
}

void Widget::paintCard(QPainter &p, bool highlighted) const {
	PaintCard(p, cardRect(), highlighted);
}

void Widget::opacityAnimationCallback() {
	updateOpacity();
	update();
	if (!_a_opacity.animating() && _hiding) {
		if (underMouse()) {
			// The notification is leaving from under the cursor, but in such case leave hook is not
			// triggered automatically. But we still want the manager to start hiding notifications
			// (see #28813).
			manager()->startAllHiding();
		}
		manager()->removeWidget(this);  // Deletes `this`
	}
}

bool Widget::shiftAnimationCallback(crl::time now) {
	if (anim::Disabled()) {
		now += st::notifyFastAnim;
	}
	const auto dt = (now - _shiftAnimation.started())
		/ float64(st::notifyFastAnim);
	if (dt >= 1.) {
		_shift.finish();
	} else {
		_shift.update(dt, anim::linear);
	}
	moveByShift();
	return (dt < 1.);
}

void Widget::hideSlow() {
	if (anim::Disabled()) {
		_hiding = true;
		base::call_delayed(
			st::notifySlowHide,
			this,
			[=, guard = _hidingDelayed.make_guard()] {
				if (guard && _hiding) {
					hideFast();
				}
			});
	} else {
		hideAnimated(st::notifySlowHide, anim::easeInCirc);
	}
}

void Widget::hideFast() {
	hideAnimated(st::notifyFastAnim, anim::linear);
}

void Widget::hideStop() {
	if (_hiding) {
		_hiding = false;
		_hidingDelayed = {};
		_a_opacity.start([this] { opacityAnimationCallback(); }, 0., 1., st::notifyFastAnim);
	}
}

void Widget::hideAnimated(float64 duration, const anim::transition &func) {
	_hiding = true;
	// Stop the previous animation so as to make sure that the notification
	// is fully restored before hiding it again.
	// Relates to https://github.com/telegramdesktop/tdesktop/issues/28811.
	_a_opacity.stop();
	_a_opacity.start([this] { opacityAnimationCallback(); }, 1., 0., duration, func);
}

void Widget::updateOpacity() {
	setWindowOpacity(_a_opacity.value(_hiding ? 0. : 1.) * _manager->demoMasterOpacity());
}

void Widget::changeShift(int top) {
	_shift.start(top);
	_shiftAnimation.start();
}

void Widget::updatePosition(QPoint startPosition, Direction shiftDirection) {
	_startPosition = startPosition;
	_direction = shiftDirection;
	moveByShift();
}

void Widget::addToHeight(int add) {
	auto newHeight = height() + add;
	auto newPosition = computePosition(newHeight);
	updateGeometry(newPosition.x(), newPosition.y(), width(), newHeight);
	Ui::ForceFullRepaintSync(this);
}

void Widget::updateGeometry(int x, int y, int width, int height) {
	move(x, y);
	setFixedSize(width, height);
	update();
}

void Widget::addToShift(int add) {
	_shift.add(add);
	moveByShift();
}

void Widget::moveByShift() {
	move(computePosition(height()));
}

QPoint Widget::computePosition(int height) const {
	auto realShift = int(base::SafeRound(_shift.current()));
	if (_direction == Direction::Up) {
		realShift = -realShift - height;
	}
	return QPoint(_startPosition.x(), _startPosition.y() + realShift);
}

ActionButton::ActionButton(QWidget *parent, const style::icon &icon)
: AbstractButton(parent)
, _icon(icon) {
	resize(st::notifyActionSize, st::notifyActionSize);
	setCursor(style::cur_pointer);
	hide();
}

void ActionButton::setShown(float64 shown) {
	if (_shown == shown) {
		return;
	}
	_shown = shown;
	setVisible(_shown > 0.);
	update();
}

void ActionButton::paintEvent(QPaintEvent *e) {
	if (_shown <= 0.) {
		return;
	}
	Painter p(this);
	p.setOpacity(_shown);

	// 图标与圆底同色：暗色纯白，亮色深灰，悬停时圆底加深。
	const auto dark = st::notificationBg->c.lightness() < 128;
	const auto color = dark ? QColor(255, 255, 255) : QColor(0x21, 0x21, 0x21);
	auto bg = color;
	bg.setAlpha(isOver() ? 41 : 23);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawEllipse(rect());
	}
	_icon.paintInCenter(p, rect(), color);
}

Notification::Notification(
	not_null<Manager*> manager,
	not_null<History*> history,
	MsgId topicRootId,
	PeerId monoforumPeerId,
	not_null<PeerData*> peer,
	const QString &author,
	HistoryItem *item,
	const Data::ReactionId &reaction,
	int forwardedCount,
	bool fromScheduled,
	QPoint startPosition,
	int shift,
	Direction shiftDirection)
: Widget(manager, startPosition, shift, shiftDirection)
, _peer(peer)
, _started(crl::now())
, _history(history)
, _topic(history->peer->forumTopicFor(topicRootId))
, _topicRootId(topicRootId)
, _sublist(history->peer->monoforumSublistFor(monoforumPeerId))
, _monoforumPeerId(monoforumPeerId)
, _userpicView(_peer->userpicPaintingPeer()->createUserpicView())
, _author(author)
, _reaction(reaction)
, _item(item)
, _forwardedCount(forwardedCount)
, _fromScheduled(fromScheduled)
, _close(this, st::notifyCloseIcon)
, _reply(this, tr::lng_notification_reply(), st::defaultBoxButton) {
	_reply->setTextTransform(Ui::RoundButtonTextTransform::ToUpper);
#ifdef _DEBUG
	_close->setObjectName(u"notification.close"_q);
	_reply->setObjectName(u"notification.reply"_q);
#endif
	Lang::Updated(
	) | rpl::on_next([=] {
		InvokeQueued(this, [=] {
			updateActionsGeometry();
			prepareReplyCache();
			update();
		});
	}, lifetime());

	if (_topic) {
		_topic->destroyed(
		) | rpl::on_next([=] {
			unlinkHistory();
		}, lifetime());
	}

	const auto minHeight = windowHeight(st::notifyMinHeight);
	auto position = computePosition(minHeight);
	updateGeometry(position.x(), position.y(), windowWidth(), minHeight);

	_userpicLoaded = !Ui::PeerUserpicLoading(_userpicView);
	updateNotifyDisplay();

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, &QTimer::timeout, [=] { startHiding(); });

	_close->setClickedCallback([this] {
		unlinkHistoryInManager();
	});
	_close->setAcceptBoth(true);
	_reply->setClickedCallback([this] {
		showReplyField();
	});
	updateActionsGeometry();
	_reply->hide();
	prepareReplyCache();

	style::PaletteChanged(
	) | rpl::on_next([=] {
		updateNotifyDisplay();
		prepareReplyCache();
		update();
	}, lifetime());

	show();

	if (ExtrasSettings::getInstance().streamerMode()) {
		ExtrasFeatures::StreamerMode::hideWidgetWindow(this);
	}
}

// 关闭按钮在右上角，回复文字按钮在右下角。
void Notification::updateActionsGeometry() {
	const auto skip = ShadowSkip();
	const auto right = skip + st::notifyActionsPos.x();
	const auto top = skip + st::notifyActionsPos.y();
	_close->moveToRight(right, top);
	_reply->moveToRight(
		skip + st::notifyReplyButtonPos.x(),
		skip + st::notifyMinHeight
			- st::notifyReplyButtonPos.y()
			- _reply->height());
}

// 回复按钮淡入时，先用渐变底色遮住它下方的正文。
void Notification::prepareReplyCache() {
	const auto reply = Ui::GrabWidget(_reply);
	const auto fadeWidth = st::notifyFadeRight.width();
	const auto top = st::notifyTextTop + st::semiboldFont->height;
	const auto right = st::notifyReplyButtonPos.x() - st::notifyBorderWidth;
	const auto w = _reply->width() + right + fadeWidth;
	const auto h = st::notifyMinHeight - top - st::notifyBorderWidth;
	auto image = QImage(
		QSize(w, h) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		Painter p(&image);
		st::notifyFadeRight.fill(p, style::rtlrect(0, 0, fadeWidth, h, w));
		p.fillRect(
			style::rtlrect(fadeWidth, 0, w - fadeWidth, h, w),
			st::notificationBg);
		p.drawPixmapRight(right, _reply->y() - ShadowSkip() - top, w, reply);
	}
	_replyCache = Ui::PixmapFromImage(std::move(image));
}

bool Notification::checkLastInput(
		bool hasReplyingNotifications,
		std::optional<crl::time> lastInputTime) {
	if (!_waitingForInput) return true;

	using namespace Platform::Notifications;
	const auto waitForUserInput = WaitForInputForCustom()
		&& lastInputTime.has_value()
		&& (*lastInputTime <= _started);

	if (!waitForUserInput) {
		_waitingForInput = false;
		if (!hasReplyingNotifications) {
			_hideTimer.start(st::notifyWaitLongHide);
		}
		return true;
	}
	return false;
}

void Notification::replyResized() {
	const auto padding = st::notifyReplyPadding;
	changeHeight(windowHeight(
		st::notifyMinHeight
			+ padding.top()
			+ _replyArea->height()
			+ padding.bottom()));
}

void Notification::replyCancel() {
	unlinkHistoryInManager();
}

void Notification::paintEvent(QPaintEvent *e) {
	repaintText();

	Painter p(this);
	p.setClipRect(e->rect());
	paintCard(p);
	p.drawImage(cardRect().topLeft(), _cache);

	const auto shown = a_actionsOpacity.value(_actionsVisible ? 1. : 0.);
	if (!canReply() || _replyArea || shown <= 0.) {
		return;
	}
	const auto border = st::notifyBorderWidth;
	const auto radius = st::notifyRadius - border;
	auto clip = QPainterPath();
	clip.addRoundedRect(
		cardRect().marginsRemoved({ border, border, border, border }),
		radius,
		radius);
	p.setClipPath(clip, Qt::IntersectClip);
	p.setOpacity(shown);
	p.drawPixmapRight(
		ShadowSkip() + border,
		ShadowSkip() + st::notifyTextTop + st::semiboldFont->height,
		width(),
		_replyCache);
}

// 回复入口只在能回复且没有展开输入框时显示。
void Notification::actionsOpacityCallback() {
	const auto shown = a_actionsOpacity.value(_actionsVisible ? 1. : 0.);
	_close->setShown(shown);
	_reply->setVisible(_actionsVisible
		&& !a_actionsOpacity.animating()
		&& canReply()
		&& !_replyArea);
	update();
}

void Notification::customEmojiCallback() {
	if (_textsRepaintScheduled) {
		return;
	}
	_textsRepaintScheduled = true;
	crl::on_main(this, [=] { repaintText(); });
}

void Notification::repaintText() {
	if (!_textsRepaintScheduled) {
		return;
	}
	_textsRepaintScheduled = false;
	if (_cache.isNull()) {
		return;
	}
	Painter p(&_cache);
	const auto adjusted = Ui::Text::AdjustCustomEmojiSize(st::emojiSize);
	const auto skip = (adjusted - st::emojiSize + 1) / 2;
	const auto margin = QMargins{ skip, skip, skip, skip };
	p.setCompositionMode(QPainter::CompositionMode_Clear);
	p.fillRect(_titleRect.marginsAdded(margin), Qt::transparent);
	p.fillRect(_textRect.marginsAdded(margin), Qt::transparent);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	paintTitle(p);
	paintText(p);
	update();
}

void Notification::paintTitle(Painter &p) {
	p.setPen(st::dialogsNameFg);
	p.setFont(st::semiboldFont);
	_titleCache.draw(p, {
		.position = _titleRect.topLeft(),
		.availableWidth = _titleRect.width(),
		.palette = &st::dialogsTextPalette,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.pausedEmoji = On(PowerSaving::kEmojiChat),
		.pausedSpoiler = On(PowerSaving::kChatSpoiler),
		.elisionLines = 1,
	});

	// 昵称右侧的小圆点，昵称占满一行时不画。
	const auto size = st::notifyDotSize;
	const auto left = _titleRect.x() + _titleCache.maxWidth() + st::notifyDotSkip;
	if (left + size > _titleRect.x() + _titleRect.width()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::dialogsUnreadBg);
	p.drawEllipse(QRect(
		left,
		_titleRect.y() + (_titleRect.height() - size) / 2,
		size,
		size));
}

void Notification::paintText(Painter &p) {
	p.setPen(st::dialogsTextFg);
	p.setFont(st::dialogsTextFont);
	_textCache.draw(p, {
		.position = _textRect.topLeft(),
		.availableWidth = _textRect.width(),
		.palette = &st::dialogsTextPalette,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.pausedEmoji = On(PowerSaving::kEmojiChat),
		.pausedSpoiler = On(PowerSaving::kChatSpoiler),
		.elisionHeight = _textRect.height(),
	});
}

void Notification::updateNotifyDisplay() {
	if (!_history || (!_item && _forwardedCount < 2)) {
		return;
	}

	const auto options = manager()->getNotificationOptions(
		_item,
		(_reaction.empty()
			? Data::ItemNotificationType::Message
			: Data::ItemNotificationType::Reaction));
	_hideReplyButton = options.hideReplyButton;

	// 内容层只覆盖卡片的最小高度，背景与阴影由 paintCard 负责。
	const auto w = cardRect().width();
	auto img = QImage(
		QSize(w, st::notifyMinHeight) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	img.setDevicePixelRatio(style::DevicePixelRatio());
	img.fill(Qt::transparent);

	{
		Painter p(&img);

		if (!options.hideNameAndPhoto) {
			if (_fromScheduled && _history->peer->isSelf()) {
				Ui::EmptyUserpic::PaintSavedMessages(p, st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), w, st::notifyPhotoSize);
				_userpicLoaded = true;
			} else if (_history->peer->isRepliesChat()) {
				Ui::EmptyUserpic::PaintRepliesMessages(p, st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), w, st::notifyPhotoSize);
				_userpicLoaded = true;
			} else {
				_userpicView = _history->peer->createUserpicView();
				_history->peer->loadUserpic();
				_history->peer->paintUserpicLeft(p, _userpicView, st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), w, st::notifyPhotoSize);
			}
		} else {
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), manager()->hiddenUserpicPlaceholder());
			_userpicLoaded = true;
		}

		const auto textLeft = st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft;
		const auto itemWidth = w - textLeft - st::notifyRightSkip;
		// 昵称一行为关闭按钮留出位置。
		const auto actionsWidth = st::notifyActionSize;
		const auto textTop = st::notifyTextTop + st::semiboldFont->height;

		QRect rectForName(textLeft, st::notifyTextTop, itemWidth - actionsWidth, st::semiboldFont->height);
		const auto reminder = _fromScheduled && _history->peer->isSelf();
		if (!options.hideNameAndPhoto) {
			if (_fromScheduled) {
				static const auto emoji = Ui::Emoji::Find(QString::fromUtf8("\xF0\x9F\x93\x85"));
				const auto size = Ui::Emoji::GetSizeNormal()
					/ style::DevicePixelRatio();
				const auto top = rectForName.top() + (st::semiboldFont->height - size) / 2;
				Ui::Emoji::Draw(p, emoji, Ui::Emoji::GetSizeNormal(), rectForName.left(), top);
				rectForName.setLeft(rectForName.left() + size + st::semiboldFont->spacew);
			}
			const auto chatTypeIcon = _topic
				? nullptr
				: Dialogs::Ui::ChatTypeIcon(_history->peer);
			if (chatTypeIcon) {
				chatTypeIcon->paint(p, rectForName.topLeft(), w);
				rectForName.setLeft(rectForName.left()
					+ chatTypeIcon->width()
					+ st::dialogsChatTypeSkip);
			}
		}

		const auto composeText = !options.hideMessageText
			|| (!_reaction.empty() && !options.hideNameAndPhoto);
		if (composeText) {
			auto old = base::take(_textCache);
			_textCache = Ui::Text::String(itemWidth);
			auto r = QRect(
				textLeft,
				textTop,
				itemWidth,
				2 * st::dialogsTextFont->height);
			const auto text = !_reaction.empty()
				? (!_author.isEmpty()
					? Ui::Text::Colorized(_author).append(' ')
					: TextWithEntities()
				).append(Manager::ComposeReactionNotification(
					_item,
					_reaction,
					options.hideMessageText))
				: _item
				? _item->toPreview({
					.hideSender = reminder,
					.generateImages = false,
					.spoilerLoginCode = options.spoilerLoginCode,
				}).text
				: ((!_author.isEmpty()
						? Ui::Text::Colorized(_author)
						: TextWithEntities()
					).append(_forwardedCount > 1
						? ('\n' + tr::lng_forward_messages(
							tr::now,
							lt_count,
							_forwardedCount))
						: QString()));
			const auto options = TextParseOptions{
				(TextParseColorized
					| TextParseMarkdown
					| (_forwardedCount > 1 ? TextParseMultiline : 0)),
				0,
				0,
				Qt::LayoutDirectionAuto,
			};
			const auto context = Core::TextContext({
				.session = &_history->session(),
				.repaint = [=] { customEmojiCallback(); },
			});
			_textCache.setMarkedText(
				st::dialogsTextStyle,
				text,
				options,
				context);
			_textRect = r;
			paintText(p);
			if (!_textCache.hasPersistentAnimation() && !_topic) {
				_textCache = Ui::Text::String();
			}
		} else {
			p.setFont(st::dialogsTextFont);
			p.setPen(st::dialogsTextFgService);
			p.drawText(
				textLeft,
				textTop + st::dialogsTextFont->ascent,
				st::dialogsTextFont->elided(
					tr::lng_notification_preview(tr::now),
					itemWidth));
		}

		const auto topicWithChat = [&]() -> TextWithEntities {
			const auto name = st::wrap_rtl(_history->peer->name());
			return _topic
				? _topic->titleWithIcon().append(u" ("_q + name + ')')
				: TextWithEntities{ name };
		};
		auto title = options.hideNameAndPhoto
			? TextWithEntities{ u"AstraGram"_q }
			: reminder
			? tr::lng_notification_reminder(tr::now, tr::marked)
			: topicWithChat();
		const auto fullTitle = manager()->addTargetAccountName(
			std::move(title),
			&_history->session());
		const auto context = Core::TextContext({
			.session = &_history->session(),
			.repaint = [=] { customEmojiCallback(); },
		});
		_titleCache.setMarkedText(
			st::semiboldTextStyle,
			fullTitle,
			Ui::NameTextOptions(),
			context);
		_titleRect = rectForName;
		paintTitle(p);
	}

	_cache = std::move(img);
	actionsOpacityCallback();
	update();
}

void Notification::updatePeerPhoto() {
	if (_userpicLoaded) {
		return;
	}
	_userpicView = _peer->createUserpicView();
	if (Ui::PeerUserpicLoading(_userpicView)) {
		return;
	}
	_userpicLoaded = true;

	const auto outerWidth = cardRect().width();
	Painter p(&_cache);
	p.setCompositionMode(QPainter::CompositionMode_Clear);
	p.fillRect(
		style::rtlrect(
			QRect(
				st::notifyPhotoPos,
				QSize(st::notifyPhotoSize, st::notifyPhotoSize)),
			outerWidth),
		Qt::transparent);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	_peer->paintUserpicLeft(
		p,
		_userpicView,
		st::notifyPhotoPos.x(),
		st::notifyPhotoPos.y(),
		outerWidth,
		st::notifyPhotoSize);
	_userpicView = {};
	update();
}

bool Notification::unlinkItem(HistoryItem *deleted) {
	auto unlink = (_item && _item == deleted);
	if (unlink) {
		_item = nullptr;
		unlinkHistory();
	}
	return unlink;
}

bool Notification::canReply() const {
	return !_hideReplyButton
		&& (_item != nullptr)
		&& !Core::App().passcodeLocked()
		&& (Core::App().settings().notifyView()
			<= Core::Settings::NotifyView::ShowPreview);
}

void Notification::unlinkHistoryInManager() {
	manager()->unlinkFromShown(this);
}

void Notification::toggleActionButtons(bool visible) {
	if (_actionsVisible != visible) {
		_actionsVisible = visible;
		_close->clearState();
		_reply->clearState();
		_reply->hide();
		a_actionsOpacity.start([this] { actionsOpacityCallback(); }, _actionsVisible ? 0. : 1., _actionsVisible ? 1. : 0., st::notifyActionsDuration);
	}
}

void Notification::showReplyField() {
	if (!canReply()) {
		return;
	}
	raise();
	activateWindow();

	if (_replyArea) {
		_replyArea->setFocus();
		return;
	}
	stopHiding();

	const auto skip = ShadowSkip();
	const auto padding = st::notifyReplyPadding;
	const auto top = skip + st::notifyMinHeight + padding.top();

	_replyArea.create(
		this,
		st::notifyReplyArea,
		Ui::InputField::Mode::MultiLine,
		tr::lng_message_ph());
	_replyArea->resize(
		cardRect().width()
			- padding.left()
			- padding.right()
			- st::notifyReplySkip
			- st::notifySendReply.width,
		st::notifySendReply.height);
	_replyArea->moveToLeft(skip + padding.left(), top);
	_replyArea->show();
	_replyArea->setFocus();
	_replyArea->setMaxLength(
		Data::PremiumLimits(&_item->history()->session()).messageLengthCurrent());
	_replyArea->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
	const auto session = &_item->history()->session();
	InitMessageFieldHandlers({
		.session = session,
		.field = _replyArea.data(),
	});
	const auto peer = _item->history()->peer;
	Ui::Emoji::SuggestionsController::Init(
		this,
		_replyArea.data(),
		session,
		{
			.suggestCustomEmoji = true,
			.allowCustomWithoutPremium = [=](
					not_null<DocumentData*> emoji) {
				return Data::AllowEmojiWithoutPremium(peer, emoji);
			},
		});

	// Catch mouse press event to activate the window.
	QCoreApplication::instance()->installEventFilter(this);
	_replyArea->heightChanges(
	) | rpl::on_next([=] {
		replyResized();
	}, _replyArea->lifetime());
	_replyArea->submits(
	) | rpl::on_next([=] { sendReply(); }, _replyArea->lifetime());
	_replyArea->cancelled(
	) | rpl::on_next([=] {
		replyCancel();
	}, _replyArea->lifetime());

	_replySend.create(this, st::notifySendReply);
	_replySend->moveToRight(skip + padding.right(), top);
	_replySend->show();
	_replySend->setClickedCallback([this] { sendReply(); });

	actionsOpacityCallback();

	replyResized();
	update();
}

void Notification::sendReply() {
	if (!_history) return;

	manager()->notificationReplied(
		myId(),
		_replyArea->getTextWithAppliedMarkdown());

	manager()->startAllHiding();
}

Notifications::Manager::NotificationId Notification::myId() const {
	if (!_history) {
		return {};
	}
	return { .contextId = {
		.sessionId = _history->session().uniqueId(),
		.peerId = _history->peer->id,
		.topicRootId = _topicRootId,
	}, .msgId = _item ? _item->id : ShowAtUnreadMsgId };
}

void Notification::changeHeight(int newHeight) {
	manager()->changeNotificationHeight(this, newHeight);
}

bool Notification::unlinkHistory(
		History *history,
		MsgId topicRootId,
		PeerId monoforumPeerId) {
	const auto unlink = _history
		&& (history == _history || !history)
		&& (topicRootId == _topicRootId || !topicRootId)
		&& (monoforumPeerId == _monoforumPeerId || !monoforumPeerId);
	if (unlink) {
		hideFast();
		_history = nullptr;
		_topic = nullptr;
		_item = nullptr;
	}
	return unlink;
}

bool Notification::unlinkSession(not_null<Main::Session*> session) {
	const auto unlink = _history && (&_history->session() == session);
	if (unlink) {
		// Custom emoji in title and text caches are owned by the session,
		// while the widget outlives it for the hide animation, so caches
		// must be destroyed right here. The already rendered _cache image
		// is still painted, so don't re-render it from the empty strings.
		_titleCache = Ui::Text::String();
		_textCache = Ui::Text::String();
		_textsRepaintScheduled = false;
		hideFast();
		_history = nullptr;
		_topic = nullptr;
		_sublist = nullptr;
		_item = nullptr;
	}
	return unlink;
}

void Notification::enterEventHook(QEnterEvent *e) {
	if (!_history) {
		return;
	}
	manager()->stopAllHiding();
	toggleActionButtons(true);
}

void Notification::leaveEventHook(QEvent *e) {
	if (!_history) {
		return;
	}
	manager()->startAllHiding();
	toggleActionButtons(false);
}

void Notification::startHiding() {
	if (!_history) return;
	hideSlow();
}

void Notification::mousePressEvent(QMouseEvent *e) {
	if (!_history || !cardRect().contains(e->pos())) return;

	if (e->button() == Qt::RightButton) {
		unlinkHistoryInManager();
	} else {
		e->ignore();
		manager()->notificationActivated(myId(), {
			.allowNewWindow = true,
		});
	}
}

bool Notification::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::MouseButtonPress) {
		if (auto receiver = qobject_cast<QWidget*>(o)) {
			if (isAncestorOf(receiver)) {
				raise();
				activateWindow();
			}
		}
	}
	return false;
}

void Notification::stopHiding() {
	if (!_history) return;
	_hideTimer.stop();
	Widget::hideStop();
}

HideAllButton::HideAllButton(
	not_null<Manager*> manager,
	QPoint startPosition,
	int shift,
	Direction shiftDirection)
: Widget(manager, startPosition, shift, shiftDirection) {
	setCursor(style::cur_pointer);

	const auto height = windowHeight(st::notifyHideAllHeight);
	auto position = computePosition(height);
	updateGeometry(position.x(), position.y(), windowWidth(), height);

	style::PaletteChanged(
	) | rpl::on_next([=] {
		update();
	}, lifetime());

	show();

	if (ExtrasSettings::getInstance().streamerMode()) {
		ExtrasFeatures::StreamerMode::hideWidgetWindow(this);
	}
}

void HideAllButton::startHiding() {
	hideSlow();
}

void HideAllButton::startHidingFast() {
	hideFast();
}

void HideAllButton::stopHiding() {
	hideStop();
}

void HideAllButton::enterEventHook(QEnterEvent *e) {
	_mouseOver = true;
	update();
}

void HideAllButton::leaveEventHook(QEvent *e) {
	_mouseOver = false;
	update();
}

void HideAllButton::mousePressEvent(QMouseEvent *e) {
	_mouseDown = true;
}

void HideAllButton::mouseReleaseEvent(QMouseEvent *e) {
	auto mouseDown = base::take(_mouseDown);
	if (mouseDown && _mouseOver) {
		manager()->clearAll();
	}
}

void HideAllButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.setClipRect(e->rect());
	paintCard(p, _mouseOver);

	p.setFont(st::defaultLinkButton.font);
	p.setPen(_mouseOver ? st::lightButtonFgOver : st::lightButtonFg);
	p.drawText(cardRect(), tr::lng_notification_hide_all(tr::now), style::al_center);
}

} // namespace internal
} // namespace Default
} // namespace Notifications
} // namespace Window
