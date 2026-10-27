#include "wallet/wallet_collectibles_internal.h"

namespace Wallet::CollectiblesDetails {

bool CollectiblesGrid::touchOwned() const {
	return _touch && (_touch->phase != TouchPhase::Ignored);
}

int CollectiblesGrid::touchContacts() const {
	auto result = 0;
	for (const auto &[device, contacts] : _touchContacts) {
		result += contacts;
	}
	return result;
}

bool CollectiblesGrid::takeTouch(not_null<QTouchEvent*> e) {
	const auto device = DeviceOf(e);
	if (_touch && _touch->device && _touch->device != device) {
		interruptTouch();
		return false;
	}
	interruptTouch();
	_touch = std::nullopt;
	const auto &points = e->touchPoints();
	if (points.size() != 1
		|| touchContacts() > 1
		|| !_press.address.isEmpty()
		|| dragFollowing()) {
		return false;
	}
	const auto &point = points.front();
	const auto origin = point.screenPos().toPoint();
	const auto index = squareAt(mapFromGlobal(origin));
	if (index < 0 || !dragSource(_records[index].address)) {
		return false;
	}
	_touch = Touch{
		.address = _records[index].address,
		.origin = origin,
		.device = device,
		.id = point.id(),
	};
	return true;
}

void CollectiblesGrid::handleTouch(not_null<QTouchEvent*> e) {
	const auto type = e->type();
	if (!_touch || _touch->device != DeviceOf(e)) {
		return;
	} else if (type == QEvent::TouchCancel) {
		interruptTouch();
		_touch = std::nullopt;
		return;
	}
	const auto &points = e->touchPoints();
	const auto i = ranges::find(
		points,
		_touch->id,
		&QTouchEvent::TouchPoint::id);
	if (points.size() != 1 || i == points.end()) {
		interruptTouch();
	}
	const auto end = (type == QEvent::TouchEnd);
	switch (_touch->phase) {
	case TouchPhase::Waiting:
		if (end) {
			finishWaitingTouch(e);
		} else {
			_scroll->viewportEvent(e);
			if (type == QEvent::TouchBegin && _scrollPressing) {
				_touchTimer.callOnce(QApplication::startDragTime());
			}
		}
		break;
	case TouchPhase::Held: {
		const auto global = i->screenPos().toPoint();
		moveHeldTouch(global);
		if (end) {
			finishHeldTouch(global);
		}
	} break;
	case TouchPhase::Ignored:
		if (end) {
			_touch = std::nullopt;
		}
		break;
	}
}

void CollectiblesGrid::holdTouch() {
	if (!_touch || _touch->phase != TouchPhase::Waiting) {
		return;
	}
	finishTravel();
	const auto address = _touch->address;
	const auto origin = _touch->origin;
	const auto index = squareAt(mapFromGlobal(origin));
	if (index < 0
		|| _records[index].address != address
		|| !dragSource(address)) {
		return;
	}
	pressAt(address, origin);
	_touch->phase = TouchPhase::Held;
	SendTouchCancel(_scroll, _touch->device);
}

void CollectiblesGrid::moveHeldTouch(QPoint global) {
	if ((global - _touch->origin).manhattanLength()
		>= QApplication::startDragDistance()) {
		_touch->moved = true;
	}
	pointerMoved(global);
}

void CollectiblesGrid::finishHeldTouch(QPoint global) {
	const auto still = !base::take(_touch)->moved;
	const auto address = base::take(_press).address;
	endGesture(global);
	if (still) {
		showCardMenu(address, global);
	}
}

void CollectiblesGrid::finishWaitingTouch(not_null<QTouchEvent*> e) {
	const auto touch = *base::take(_touch);
	_touchTimer.cancel();
	const auto index = squareAt(mapFromGlobal(touch.origin));
	// The scroll clicks whatever square has moved under the finger.
	if (_scrollPressing
		&& (index < 0 || _records[index].address != touch.address)) {
		SendTouchCancel(_scroll, touch.device);
		return;
	}
	_scroll->viewportEvent(e);
}

void CollectiblesGrid::interruptTouch() {
	if (!touchOwned()) {
		return;
	}
	const auto waiting = (_touch->phase == TouchPhase::Waiting);
	_touch->phase = TouchPhase::Ignored;
	_touchTimer.cancel();
	if (waiting) {
		SendTouchCancel(_scroll, _touch->device);
	} else {
		endGesture(std::nullopt);
	}
}

void CollectiblesGrid::watchTouchWindow() {
	_touchContacts.clear();
	_touchFilter = nullptr;
	// WHY: Qt splits a second finger over a sibling widget away from the
	// grid, so only the window's own frames show every finger down.
	if (const auto handle = window()->windowHandle()) {
		_touchFilter.reset(base::install_event_filter(
			handle,
			[=](not_null<QEvent*> e) {
				observeTouchFrame(e);
				return base::EventFilterResult::Continue;
			}));
	}
}

void CollectiblesGrid::observeTouchFrame(not_null<QEvent*> e) {
	const auto touch = TouchScreenEvent(e);
	if (!touch) {
		return;
	}
	const auto device = DeviceOf(touch);
	const auto contacts = int(ranges::count_if(
		touch->touchPoints(),
		[](const QTouchEvent::TouchPoint &point) {
			return (int(point.state()) != int(Qt::TouchPointReleased));
		}));
	const auto outdated = [&](const auto &entry) {
		return !entry.first || (entry.first == device);
	};
	_touchContacts.erase(
		ranges::remove_if(_touchContacts, outdated),
		end(_touchContacts));
	if (contacts > 0) {
		_touchContacts.emplace_back(device, contacts);
	} else if (_touch && !touchOwned() && _touch->device == device) {
		_touch = std::nullopt;
	}
	if (_touch && !_touch->device) {
		interruptTouch();
		_touch = std::nullopt;
	}
	if (touchContacts() > 1) {
		interruptTouch();
	}
}

std::optional<FlightPose> CollectiblesGrid::dragSource(
		const QString &address) const {
	const auto index = indexOf(address);
	if (index < 0) {
		return std::nullopt;
	}
	const auto &gift = _records[index].gift;
	if (!gift
		|| !gift->unique
		|| !_host.painter
		|| ownCard().contains(address)
		|| _flights.contains(address)
		|| _held.contains(address)
		|| _returning.contains(address)
		|| _leaving == address) {
		return std::nullopt;
	}
	return squarePose(address);
}

bool CollectiblesGrid::dragFollowing() const {
	return _drag.gift && _drag.gift->following();
}

void CollectiblesGrid::pressAt(const QString &address, QPoint global) {
	const auto source = dragSource(address);
	_press = Press{
		.address = address,
		.origin = global,
		.grab = (source
			? (QPointF(_host.body->mapFromGlobal(global)) - source->center)
			: QPointF()),
		.draggable = source.has_value(),
	};
}

void CollectiblesGrid::pointerMoved(QPoint global) {
	if (dragFollowing()) {
		_drag.gift->move(_host.body->mapFromGlobal(global));
	} else if (_press.draggable
		&& ((global - _press.origin).manhattanLength()
			>= QApplication::startDragDistance())) {
		startDrag(global);
	}
}

void CollectiblesGrid::startDrag(QPoint global) {
	_press.draggable = false;
	const auto address = _press.address;
	const auto source = dragSource(address);
	const auto i = source ? _squares.find(address) : end(_squares);
	const auto button = (i != end(_squares)) ? i->second.gift.get() : nullptr;
	auto player = button ? button->takePlayer() : nullptr;
	const auto frame = button ? button->currentStickerFrame() : QImage();
	if (frame.isNull()) {
		return;
	}
	const auto document = i->second.record.gift->document.get();
	const auto press = base::take(_press);
	holdSticker(address);
	setLifted(address);
	_drag = Drag{
		.address = address,
		.document = document,
		.firstFrame = frame,
		.generation = ++_lastDragGeneration,
		.gift = std::make_unique<DraggedGift>(DraggedGiftArgs{
			.body = _host.body,
			.titleExtension = st::separatePanelTitleHeight,
			.source = *source,
			.cursor = _host.body->mapFromGlobal(global),
			.grab = press.grab,
			.player = std::move(player),
			.firstFrame = frame,
			.document = document,
			.frameSize = st::giftBoxStickerSize,
			.outline = true,
		}),
	};
	setGripped(address);
	if (_hint) {
		_hint->record();
	}
	if (_host.bring) {
		_host.bring();
	}
}

void CollectiblesGrid::endGesture(std::optional<QPoint> release) {
	const auto following = dragFollowing();
	_press = Press();
	auto released = std::optional<FlightPose>();
	if (following && release) {
		_drag.gift->move(_host.body->mapFromGlobal(*release));
		released = _drag.gift->pose();
	}
	if (mouseGrabber() == this) {
		releaseMouse();
	}
	if (!following || !dragFollowing()) {
		return;
	} else if (released) {
		dropDrag(*released);
	} else {
		returnDrag();
	}
}

void CollectiblesGrid::clearPress() {
	if (base::take(_press).grabbed && mouseGrabber() == this) {
		releaseMouse();
	}
}

void CollectiblesGrid::dropDrag(const FlightPose &released) {
	const auto address = _drag.address;
	const auto generation = _drag.generation;
	const auto index = indexOf(address);
	const auto gift = (index >= 0) ? _records[index].gift : nullptr;
	if (!gift || !gift->unique || ownCard().contains(address)) {
		vanishDrag();
		return;
	}
	const auto fold = _host.fold();
	const auto local = CardFoldLocal(fold, released.center);
	if (!local
		|| fold.opacity <= 0.
		|| !CardFoldOutline(fold).containsPoint(
			released.center,
			Qt::OddEvenFill)) {
		returnDrag();
		return;
	}
	if (!addDragged(gift, *local)) {
		returnDrag();
		return;
	}
	landDrag(generation, released.side);
	_drag.gift = nullptr;
}

bool CollectiblesGrid::addDragged(
		std::shared_ptr<const Data::StarGift> gift,
		QPointF local) {
	auto &own = ownCard();
	const auto stickers = own.shown().stickers;
	const auto card = _host.fold().rest.size();
	const auto center = CardStickerClampedCenter(local, card);
	if (!own.ready()) {
		showRefusal(OwnCardChange::NotReady);
		return false;
	} else if (int(stickers.size()) >= Data::kWalletCardMaxStickers) {
		showRefusal(OwnCardChange::Limit);
		return false;
	} else if (!center) {
		return false;
	}
	auto desired = stackedCard();
	desired.stickers.push_back({
		.address = _drag.address,
		.area = CardStickerArea(
			*center,
			CardStickerPreferredSide(card),
			0.,
			card),
		.gift = std::move(gift),
	});
	const auto change = own.save(std::move(desired));
	if (change != OwnCardChange::Applied) {
		showRefusal(change);
		return false;
	}
	setLifted(QString());
	return true;
}

void CollectiblesGrid::landDrag(uint64 generation, float64 releasedSide) {
	if (_drag.generation != generation) {
		return;
	}
	const auto address = _drag.address;
	auto artwork = _drag.gift ? _drag.gift->takeArtwork() : TravelArtwork();
	if (const auto target = shownPose(address)) {
		const auto scale = releasedSide / target->side;
		landSticker(address, scale, QImage(), std::move(artwork));
	} else {
		releaseSticker(address);
	}
	dragFinished(generation);
}

void CollectiblesGrid::returnDrag() {
	const auto address = _drag.address;
	const auto generation = _drag.generation;
	if (!squarePose(address)) {
		vanishDrag();
		return;
	}
	_drag.gift->returnTo([=] {
		return squarePose(address);
	}, [=](bool arrived) {
		if (arrived) {
			restoreSquare(generation);
		}
		dragFinished(generation);
	});
	releaseSticker(address);
}

void CollectiblesGrid::restoreSquare(uint64 generation) {
	if (_drag.generation != generation || !_drag.gift || !_drag.document) {
		return;
	}
	auto artwork = _drag.gift->takeArtwork();
	const auto i = _squares.find(_drag.address);
	if (i != end(_squares) && i->second.gift) {
		i->second.gift->restorePlayer(
			not_null(_drag.document),
			std::move(artwork.playback.player),
			_drag.firstFrame);
	}
}

void CollectiblesGrid::vanishDrag() {
	const auto address = _drag.address;
	const auto generation = _drag.generation;
	_drag.gift->vanish([=] { dragFinished(generation); });
	setLifted(QString());
	releaseSticker(address);
}

void CollectiblesGrid::dragFinished(uint64 generation) {
	if (_drag.generation != generation) {
		return;
	}
	setLifted(QString());
	setGripped(QString());
	crl::on_main(this, [=] {
		if (_drag.generation == generation) {
			_drag = Drag();
		}
	});
}

void CollectiblesGrid::finishTravel() {
	if (_drag.gift) {
		_drag.gift->finish();
	}
}

} // namespace Wallet::CollectiblesDetails
