#include "history/view/media/history_view_gram_transfer_internal.h"

namespace HistoryView {

namespace GramTransferInternal {}

using namespace GramTransferInternal;

namespace GramTransferInternal {

GramTransferCardPart::GramTransferCardPart(
	GramTransferOrigin origin,
	GramTransferHandover handover)
: _origin(std::move(origin))
, _detailsLink(std::make_shared<LambdaClickHandler>([
		weak = base::make_weak(this)](ClickContext context) {
	if (weak) {
		weak->showDetails(context);
	}
}))
, _amount(
	CardAmountStyle(),
	SignedAmount(_origin.action.amount, _origin.action.outgoing))
, _address(FriendlyAddress(_origin.action.address))
, _identity(
	ReadableIdentity(_origin.view->data(), !_address.isEmpty()).toUpper())
, _ribbonTextWidth(_origin.action.outgoing ? OutgoingRibbonTextWidth() : 0) {
	adopt(std::move(handover));
	watchRead();
}

GramTransferHandover GramTransferCardPart::takeHandover() {
	return {
		.mark = std::move(_mark),
		.transition = std::move(_transition),
		.clock = std::move(_clock),
		.angle = _angle,
		.markStarted = std::exchange(_markStarted, false),
	};
}

// WHY: a sent or failed transfer refreshes its view, so the card that showed
// the clock is replaced by a new one. The new card continues what the old one
// was showing instead of restarting it: the mark keeps its frame, a sending
// or waiting card keeps its glare, and a settling card keeps its transition.
void GramTransferCardPart::adopt(GramTransferHandover &&handover) {
	_angle = handover.angle;
	if (handover.mark) {
		_mark = std::move(handover.mark);
		_markStarted = handover.markStarted;
		_heavyPending = true;
		if (_mark->valid() && _mark->animating()) {
			_mark->animate([view = _origin.view] {
				if (const auto strong = view.get()) {
					strong->repaint();
				}
			}, _mark->frameIndex(), _mark->framesCount() - 1);
		}
	}
	if (sending()) {
		if (handover.clock) {
			_clock = std::move(handover.clock);
			_heavyPending = true;
			attachClock();
		}
		return;
	} else if (anim::Disabled()) {
		return;
	} else if (handover.transition) {
		_transition = std::move(handover.transition);
		_heavyPending = true;
		if (!holding()) {
			animateTransition();
		}
	} else if (handover.clock) {
		_clock = std::move(handover.clock);
		validateReveal(crl::now());
		if (_clock) {
			_heavyPending = true;
			attachClock();
		}
	}
}

void GramTransferCardPart::watchRead() {
	const auto item = _origin.view->data();
	if (_origin.action.outgoing || !item->unread(item->history())) {
		return;
	}
	const auto id = _origin.action.itemId;
	_origin.session->data().histories().shownReads(
	) | rpl::filter([=](const Data::Histories::ShownRead &read) {
		return (read.shown->history()->peer->id == id.peer)
			&& (read.wasReadTill < id.msg)
			&& (id.msg <= read.readTill);
	}) | rpl::take(1) | rpl::on_next([=](
			const Data::Histories::ShownRead &read) {
		markRead(read.shown->fullId() == id);
	}, _readLifetime);
}

// WHY: a read covers the messages above the shown one too, so only a card on
// screen takes the start state, and it waits for its next paint: the first
// frame shows zeros even when the chat read it before painting it.
void GramTransferCardPart::markRead(bool shown) {
	const auto view = _origin.view.get();
	if (!view
		|| _transition
		|| anim::Disabled()
		|| (!shown
			&& !view->history()->owner().queryItemVisibility(view->data()))) {
		return;
	}
	_transition = std::make_unique<CardTransition>();
	_transition->read.emplace().fast = MakeCardMark(u"gram_white_fast"_q);
	validateMark();
	view->history()->owner().registerHeavyViewPart(view);
	view->repaint();
}

void GramTransferCardPart::validateRead(
		crl::time now,
		crl::time frame,
		bool paused) const {
	if (!_transition || !_transition->read) {
		return;
	}
	auto &read = *_transition->read;
	if (!read.started) {
		if (anim::Disabled()) {
			_transition = nullptr;
			if (const auto view = _origin.view.get()) {
				view->repaint();
			}
			return;
		} else if (!read.turn.at) {
			const auto view = _origin.view.get();
			const auto line = view
				? view->delegate()->elementGramReadLine()
				: nullptr;
			read.turn = line
				? line->join(now)
				: GramReadLine::Turn{ .at = now };
		}
		if (now < read.turn.at) {
			return;
		}
		// WHY: a card painted after its turn starts from the turn, so the
		// line keeps its order and spacing for a card off screen then.
		read.started = read.turn.at;
		read.angle = _angle->value(frame);
		_transition->glare.tick(read.started, kGlareDuration, kGlareTimeout);
		_transition->started = read.started + kReadRollDuration;
		_markStarted = true;
		animateTransition();
	}
	// As a paused sending wait does, a paused read gives both diamond plays up.
	if (paused) {
		read.fast = nullptr;
	}
	if (!read.settled && now >= _transition->started) {
		read.settled = true;
		if (now < _transition->started + kTransitionDuration) {
			_transition->fast = base::take(read.fast);
			startFastMark();
		}
		_transition->spin = StartSpin(
			SendingAngle(read.angle, _transition->started - read.started),
			_angle->value(frame));
		if (now < _transition->started + kBurstDuration) {
			_transition->burst = Ui::StarBurst::Make(CardBurstDescriptor());
		}
	}
}

bool GramTransferCardPart::holding() const {
	return _transition && _transition->read && !_transition->read->started;
}

bool GramTransferCardPart::rolling(crl::time now) const {
	return _transition
		&& _transition->read
		&& (holding() || now < _transition->started);
}

// The read plays an unread card's diamond, so its first play waits.
bool GramTransferCardPart::awaitingRead() const {
	if (holding()) {
		return true;
	}
	const auto view = _origin.view.get();
	return view
		&& !_origin.action.outgoing
		&& view->data()->unread(view->history());
}

bool GramTransferCardPart::playingRead(crl::time now) const {
	return _mark
		&& _transition
		&& _transition->read
		&& _transition->read->fast
		&& (now < _transition->started);
}

void GramTransferCardPart::startReveal(
		const SendingClock &clock,
		crl::time now) const {
	_transition = std::make_unique<CardTransition>();
	_transition->pose = Wallet::SendingClockPose(now - clock.started);
	_transition->started = now;
	_transition->glare = clock.glare; // never ticked: the pass ends, none starts
	_transition->spin = StartSpin(
		SendingAngle(clock.angle, now - clock.started),
		_angle ? _angle->value(now) : 0.);
	const auto view = _origin.view.get();
	if (view && !view->data()->hasFailed()) {
		_transition->burst = Ui::StarBurst::Make(CardBurstDescriptor());
	}
	_heavyPending = true;
	animateTransition();
}

void GramTransferCardPart::startFastMark() const {
	const auto fast = _transition->fast.get();
	if (!fast || !fast->valid()) {
		_transition->fast = nullptr;
		return;
	}
	_mark->jumpTo(0, nullptr);
}

void GramTransferCardPart::validateReveal(crl::time now) const {
	if (!_clock || sending()) {
		return;
	} else if (!_clock->settleAt) {
		_clock->settleAt = looping()
			? NextLoopStart(_mark.get(), _clock->loopStarted, now)
			: now;
	}
	if (_layout.sending || waitingForLoop(now)) {
		return;
	} else if (!_transition && !anim::Disabled()) {
		const auto boundary = looping();
		startReveal(*_clock, now);
		if (boundary) {
			_transition->fast = std::move(_clock->fast);
			startFastMark();
		}
	}
	_clock = nullptr;
}

void GramTransferCardPart::animateTransition() const {
	_transition->animation.init([weak = base::make_weak(this)](
			crl::time now) {
		const auto strong = weak.get();
		if (!strong || !strong->_transition) {
			return false;
		}
		auto &transition = *strong->_transition;
		// Frees the stars even while the card is not painted.
		if (transition.burst && now >= transition.started + kBurstDuration) {
			transition.burst = nullptr;
		}
		strong->advanceRead(now);
		if (const auto view = strong->_origin.view.get()) {
			view->repaint();
		}
		return !strong->transitionFinished(now);
	});
	_transition->animation.start();
}

bool GramTransferCardPart::transitionFinished(crl::time now) const {
	return !_transition
		|| (!holding()
			&& now >= _transition->started + kTransitionDuration
			&& !PlayingFast(*_transition));
}

Media::BubbleRoll GramTransferCardPart::bubbleRoll(QSize outer) const {
	if (!_transition) {
		return {};
	}
	const auto now = crl::now();
	const auto elapsed = now - _transition->started;
	if (const auto &read = _transition->read) {
		if (!read->started) {
			return {};
		}
		return { .scale = 1.
			+ BumpAmplitude(outer, kReadPopAmplitude)
				* BumpShape(kSettleBump, now - read->started)
			- BumpAmplitude(outer, kReadPressDepth)
				* BumpShape(kReadPress, elapsed) };
	}
	return { .scale = 1.
		+ BumpAmplitude(outer, kBumpAmplitude)
			* BumpShape(kSettleBump, elapsed) };
}

QMargins GramTransferCardPart::bubbleRollRepaintMargins(
		QSize outer) const {
	if (!_transition) {
		return {};
	}
	const auto amplitude = BumpAmplitude(outer, kBumpAmplitude);
	const auto x = int(std::ceil(amplitude * outer.width() / 2.));
	const auto y = int(std::ceil(amplitude * outer.height() / 2.));
	return QMargins(x, y, x, y);
}

GramTransferCardPart::~GramTransferCardPart() {
	if (const auto angle = _angle.get()) {
		angle->forget(this);
	}
	invalidate_weak_ptrs(this);
	_destroyed.fire({});
}

void GramTransferCardPart::showDetails(const ClickContext &context) {
	const auto origin = _origin;
	const auto show = GramTransferShow(origin, context);
	if (!show) {
		return;
	}
	auto details = ResolveGramTransfer(origin.session.get(), origin.action);
	const auto firstGrams = Wallet::ShowFirstGramsIfPending(show);
	Wallet::ShowTransactionDetails(
		show,
		std::move(details.item),
		details.partial,
		nullptr,
		[weak = base::make_weak(this), origin] {
			return weak && CurrentGramTransfer(origin);
		},
		rpl::merge(GramTransferInvalidations(origin), _destroyed.events()),
		[session = origin.session] {
			if (const auto strong = session.get()) {
				Wallet::ShowWallet(strong);
			}
		},
		(firstGrams
			? (Ui::LayerOption::KeepOther | Ui::LayerOption::ShowAfterOther)
			: Ui::LayerOptions(Ui::LayerOption::KeepOther)));
}

QSize GramTransferCardPart::countOptimalSize() {
	const auto height = resolveLayout(st::chatUniqueGiftMaxWidth);
	return { st::chatUniqueGiftMaxWidth, height };
}

QSize GramTransferCardPart::countCurrentSize(int newWidth) {
	return { newWidth, resolveLayout(newWidth) };
}

int GramTransferCardPart::resolveLayout(int outerWidth) {
	const auto border = st::chatUniqueGiftBorder;
	const auto inset = st::walletCardContentLeft;
	const auto cardWidth = GramTransferCardWidth(outerWidth);
	const auto available = std::max(cardWidth - 2 * inset, 1);
	const auto tag = ResolveTag(_origin.action.outgoing, _origin.action.failed);
	_layout.badgeBg = tag.bg;
	_layout.sending = sending();
	const auto &badgeFont = st::msgServiceGiftBoxBadgeFont;
	const auto badgePadding = st::chatUniqueGiftBadgePadding;
	const auto badgeLimit = std::max(
		cardWidth - 2 * badgeFont->height
			- badgePadding.left() - badgePadding.right(),
		0);
	const auto badgeArea = std::min(badgeFont->width(tag.text), badgeLimit);
	_layout.badge = badgeFont->elided(tag.text, badgeArea);
	_layout.badgeTextWidth = badgeArea;
	const auto settled = _origin.action.outgoing
		? ResolveTag(true, false).text
		: tag.text;
	const auto settledArea = std::min(badgeFont->width(settled), badgeLimit);
	const auto ribbon = ComputeRibbon(settledArea);
	_layout.clockCenter = QPointF(cardWidth - ribbon.size, 0.)
		+ RibbonWordCenter(ribbon, badgeFont->elided(settled, settledArea));
	const auto reservedArea = std::min(
		std::max(_ribbonTextWidth, badgeArea),
		badgeLimit);
	const auto badgeTextWidth = reservedArea
		+ badgePadding.left() + badgePadding.right();
	const auto badgeHeight = badgePadding.top()
		+ badgeFont->height + badgePadding.bottom();
	const auto bandReach = badgePadding.top()
		+ int(std::ceil(badgeTextWidth / M_SQRT2))
		+ int(std::ceil(M_SQRT2 * badgeHeight));
	// WHY: the ribbon's painted strip is the 45-degree band
	// W - bandReach <= x - y, so a row is clear exactly when its right
	// edge sits above that line; this pushes a row down instead of under it.
	const auto clearOfBand = [&](int right) {
		return right - cardWidth + bandReach;
	};

	const auto markSize = st::walletChatCardMarkSize;
	_layout.markTop = std::max(
		st::walletChatCardMarkTop,
		clearOfBand((cardWidth + markSize) / 2));

	_amount.setAvailableWidth(available);
	const auto amountSize = _amount.size();
	const auto scaledWidth = int(std::ceil(amountSize.width()));
	const auto amountHeight = int(std::ceil(amountSize.height()));
	_layout.amountTop = std::max(
		_layout.markTop + markSize + st::walletChatCardAmountSkip,
		clearOfBand((cardWidth + scaledWidth) / 2));

	_layout.identity = st::walletCardNameFont->elided(_identity, available);
	const auto identityWidth = st::walletCardNameFont->width(_layout.identity);
	_layout.identityTop = std::max(
		_layout.amountTop + amountHeight + st::walletChatCardNameSkip,
		clearOfBand((cardWidth + identityWidth) / 2));
	auto bottom = _layout.identityTop + st::walletCardNameFont->height;

	_layout.addressLines = AddressLines(_address, available);
	if (!_layout.addressLines.isEmpty()) {
		const auto addressFont
			= st::walletDetailsCollectionLabel.style.font->monospace();
		auto widest = 0;
		for (const auto &line : _layout.addressLines) {
			accumulate_max(widest, addressFont->width(line));
		}
		_layout.addressTop = std::max(
			bottom + st::walletChatCardAddressSkip,
			clearOfBand((cardWidth + widest) / 2));
		bottom = _layout.addressTop
			+ int(_layout.addressLines.size()) * addressFont->height;
	}
	const auto cardHeight = bottom + st::walletChatCardBottom;
	_layout.card = QRect(border, border, cardWidth, cardHeight);
	return cardHeight + 2 * border;
}

void GramTransferCardPart::validateMark() const {
	if (_mark) {
		return;
	}
	_mark = MakeCardMark(u"gram_white"_q);
	if (const auto view = _origin.view.get()) {
		view->history()->owner().registerHeavyViewPart(view);
	}
}

QRect GramTransferCardPart::markPaintRect() const {
	const auto size = st::walletChatCardMarkPaintSize;
	const auto shift = (size - st::walletChatCardMarkSize) / 2;
	return QRect(
		(_layout.card.width() - size) / 2,
		_layout.markTop - shift - st::walletChatCardMarkRaise,
		size,
		size);
}

bool GramTransferCardPart::sending() const {
	const auto view = _origin.view.get();
	return view
		&& _origin.action.outgoing
		&& !_origin.action.failed
		&& view->data()->isSending();
}

bool GramTransferCardPart::looping() const {
	return _mark
		&& _clock
		&& _clock->loopStarted
		&& _clock->animation.animating()
		&& !anim::Disabled()
		&& !On(PowerSaving::kStickersChat);
}

bool GramTransferCardPart::waitingForLoop(crl::time now) const {
	return !_transition
		&& !sending()
		&& looping()
		&& (now < _clock->settleAt);
}

bool GramTransferCardPart::sendingLook(crl::time now) const {
	return _layout.sending || waitingForLoop(now);
}

void GramTransferCardPart::validateClock(crl::time now) const {
	if (waitingForLoop(now)) {
		return;
	} else if (!_layout.sending) {
		_clock = nullptr;
		return;
	} else if (!sending()) {
		if (_clock) {
			_clock->animation.stop();
		}
		return;
	} else if (_clock) {
		if (!_clock->animation.animating()) {
			attachClock();
		}
		return;
	}
	_clock = std::make_unique<SendingClock>();
	_clock->started = crl::now();
	_clock->angle = _angle ? _angle->value(_clock->started) : 0.;
	attachClock();
}

void GramTransferCardPart::attachClock() const {
	if (anim::Disabled()) {
		return;
	}
	_clock->animation.init([weak = base::make_weak(this)](crl::time now) {
		const auto strong = weak.get();
		if (!strong || !strong->_clock) {
			return false;
		}
		// WHY: settling drops the clock and this animation with it;
		// Basic::call runs a copy of this callback, so returning is safe
		// but nothing below may touch the clock.
		strong->validateReveal(now);
		const auto clock = strong->_clock.get();
		if (clock) {
			clock->glare.tick(now, kGlareDuration, kGlareTimeout);
			strong->advanceLoop(now);
		}
		if (const auto view = strong->_origin.view.get()) {
			view->repaint();
		}
		return clock && !anim::Disabled();
	});
	_clock->animation.start();
}

void GramTransferCardPart::validateLoop(
		crl::time now,
		bool paused) const {
	auto &clock = *_clock;
	if (paused) {
		clock.loopStarted = 0;
	} else if (!clock.loopStarted
		&& sending()
		&& clock.animation.animating()) {
		clock.loopStarted = now;
		clock.loop = 0;
		_markStarted = true;
		if (!clock.spare) {
			clock.spare = MakeCardMark(u"gram_white"_q);
		}
		if (!clock.fast) {
			clock.fast = MakeCardMark(u"gram_white_fast"_q);
		}
	}
	advanceLoop(now);
}

// WHY: the loop is the frame of the time since it started, so a card that
// replaces this one keeps its phase; each wrap paints the spare already on
// frame 0, as one icon jumped back from its last frame repaints a stale one.
void GramTransferCardPart::advanceLoop(crl::time now) const {
	if (!looping()) {
		return;
	}
	auto &clock = *_clock;
	const auto frames = _mark->framesCount();
	const auto position = LoopPosition(_mark.get(), clock.loopStarted, now);
	const auto loop = int(position / frames);
	if (loop != clock.loop) {
		clock.loop = loop;
		std::swap(_mark, clock.spare);
	}
	_mark->jumpTo(int(position % frames), nullptr);
	if (loop > 0) {
		clock.spare->jumpTo(0, nullptr);
	}
}

// WHY: the read plays the diamond by the time since its start, so a card
// painted late or handed over keeps the roll's clock, and it holds the
// last frame until the settle shows white-fast frame 0 in its place.
void GramTransferCardPart::advanceRead(crl::time now) const {
	if (!playingRead(now) || !_mark->valid()) {
		return;
	}
	const auto last = int64(_mark->framesCount() - 1);
	const auto position = LoopPosition(
		_mark.get(),
		_transition->read->started,
		now);
	_mark->jumpTo(int(std::min(position, last)), nullptr);
}

std::optional<Wallet::GlareBand> GramTransferCardPart::glarePass(
		crl::time now) const {
	const auto cycle = _transition
		? &_transition->glare
		: (_clock && sendingLook(now))
		? &_clock->glare
		: nullptr;
	const auto progress = cycle
		? cycle->progress(now)
		: std::optional<float64>();
	if (!progress) {
		return {};
	}
	return Wallet::ComputeGlareBand(
		*progress,
		_layout.card.width(),
		st::walletChatCardGlareWidth);
}

GramTransferCardPart::Sweep GramTransferCardPart::sweep(
		crl::time now,
		crl::time frame,
		bool still) const {
	const auto shared = _angle->value(frame);
	if (still) {
		return { shared };
	} else if (_clock && sendingLook(now)) {
		return {
			SendingAngle(_clock->angle, now - _clock->started),
			&_clock->background,
		};
	} else if (rolling(now)) {
		const auto &read = *_transition->read;
		if (read.started) {
			return {
				SendingAngle(read.angle, now - read.started),
				&_transition->background,
			};
		}
	} else if (_transition) {
		const auto elapsed = now - _transition->started;
		if (elapsed < kSpinDuration) {
			return {
				SpinAngle(_transition->spin, shared, elapsed),
				&_transition->background,
			};
		}
	}
	return { shared };
}

void GramTransferCardPart::paintBurst(QPainter &p, crl::time now) const {
	const auto cardWidth = float64(_layout.card.width());
	const auto cardHeight = float64(_layout.card.height());
	const auto mark = markPaintRect();
	const auto radius = st::walletCardRadius;
	auto clip = QPainterPath();
	clip.addRoundedRect(QRectF(0, 0, cardWidth, cardHeight), radius, radius);
	_transition->burst->paint(p, {
		.origin = QPointF(
			cardWidth / 2.,
			mark.y() + mark.height()
				* (Wallet::kGramDiamondTop + Wallet::kGramDiamondBottom)
				/ 2.),
		.emitter = float64(st::walletChatCardMarkSize),
		.extent = cardWidth,
		.elapsed = now - _transition->started,
		.clip = std::move(clip),
	});
}

bool GramTransferCardPart::hasHeavyPart() {
	return _mark || _clock || _transition;
}

void GramTransferCardPart::unloadHeavyPart() {
	_mark = nullptr;
	_clock = nullptr;
	_transition = nullptr;
	_markStarted = false;
	if (const auto angle = _angle.get()) {
		angle->forget(this);
	}
}

// The outline has to keep the card's own gradient under it, so the fill and
// the stroke are two passes: the background brush with no pen, then a pen
// whose gradient fades in and out with the pass and no brush at all.
void GramTransferCardPart::paintGlareBorder(
		QPainter &p,
		Wallet::GlareBand band) const {
	Wallet::PaintGlare(
		p,
		QRectF(0, 0, _layout.card.width(), _layout.card.height()),
		st::msgServiceGiftBoxRadius,
		band,
		{
			.stroke = float64(st::walletChatCardGlareStroke),
			.slope = 0.,
			.border = 1.,
			.background = 0.,
		},
		CardTickerFg());
}

void GramTransferCardPart::validateAngle(
		QPainter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context) const {
	const auto angle = context.st->gramCardAngle();
	if (_angle.get() != angle.get()) {
		if (const auto previous = _angle.get()) {
			previous->forget(this);
		}
		_angle = angle;
	}
	if (const auto widget = PaintWidget(p)) {
		// A swipe moves the card on screen, not the angle it is turned from.
		const auto shift = context.gestureHorizontal.visualTranslationFor(
			owner->parent()->data()->id.bare);
		const auto rect = p.transform().mapRect(QRectF(_layout.card));
		angle->track(this, widget, rect.translated(-shift, 0.));
	}
}

void GramTransferCardPart::validateBadge() const {
	if (_layout.sending) {
		return;
	}
	const auto key = RibbonKey{
		.text = _layout.badge,
		.bg = _layout.badgeBg,
		.textWidth = _layout.badgeTextWidth,
		.ratio = style::DevicePixelRatio(),
	};
	if (!_badge.isNull() && _badgeKey == key) {
		return;
	}
	_badgeKey = key;
	_badge = RenderRibbon(
		ComputeRibbon(_layout.badgeTextWidth),
		_layout.badge,
		_layout.badgeBg);
}

void GramTransferCardPart::paintSendingClock(
		QPainter &p,
		crl::time now) const {
	const auto pose = Wallet::SendingClockPose((_clock && !anim::Disabled())
		? (now - _clock->started)
		: 0);
	Wallet::PaintClock(
		p,
		CardClockStyle(),
		_layout.clockCenter,
		pose,
		CardTickerFg(),
		1.);
}

// The band fills the way a ripple fills its mask, from the clock out.
void GramTransferCardPart::paintReveal(
		QPainter &p,
		int cardWidth,
		crl::time now) const {
	const auto ribbon = ComputeRibbon(_layout.badgeTextWidth);
	auto &transition = *_transition;
	if (transition.wordsTextWidth != ribbon.textWidth
		|| transition.toText != _layout.badge) {
		transition.toWord = RenderRibbonWord(ribbon, _layout.badge);
		transition.toText = _layout.badge;
		transition.wordsTextWidth = ribbon.textWidth;
	}
	const auto elapsed = now - transition.started;
	const auto origin = QPointF(cardWidth - ribbon.size, 0.);
	const auto center = _layout.clockCenter - origin;
	const auto c = RevealProgress(
		elapsed,
		kRevealColorDelay,
		kRevealColorDuration);
	const auto color = anim::color(
		CardTickerFg(),
		_layout.badgeBg,
		1. - (1. - c) * (1. - c));
	const auto outer = st::walletChatCardClockSize / 2.;
	const auto inner = outer - st::walletChatCardClockStroke;
	const auto reach = RibbonReach(ribbon, center);
	const auto radius = outer
		+ (reach - outer) * RevealProgress(elapsed, 0, kRevealFillDuration);
	const auto scale = transition.read
		? 0.
		: std::max(1. - elapsed / float64(kRevealClockDuration), 0.);
	const auto w = RevealProgress(
		elapsed,
		kRevealWordDelay,
		kRevealWordDuration);
	const auto word = 1. - (1. - w) * (1. - w);
	const auto ratio = style::DevicePixelRatio();
	const auto size = QSize(ribbon.size, ribbon.size) * ratio;
	if (transition.frame.size() != size) {
		transition.frame = QImage(size, QImage::Format_ARGB32_Premultiplied);
	}
	transition.frame.setDevicePixelRatio(ratio);
	transition.frame.fill(Qt::transparent);
	{
		auto q = QPainter(&transition.frame);
		auto hq = PainterHighQualityEnabler(q);
		PaintRibbonBand(q, ribbon, color);
		q.setPen(Qt::NoPen);
		q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
		if (radius < reach) {
			auto outside = QPainterPath();
			outside.setFillRule(Qt::OddEvenFill);
			outside.addRect(QRectF(0., 0., ribbon.size, ribbon.size));
			outside.addEllipse(center, radius, radius);
			q.fillPath(outside, QColor(0, 0, 0));
		}
		if (scale > 0.) {
			q.setBrush(QColor(0, 0, 0));
			q.drawEllipse(center, inner * scale, inner * scale);
		}
		if (word > 0.) {
			const auto shift = st::walletChatCardRevealWordShift
				* (1. - word)
				/ M_SQRT2;
			q.setCompositionMode(QPainter::CompositionMode_SourceAtop);
			q.setOpacity(word);
			q.drawImage(QPointF(-shift, shift), transition.toWord);
			q.setOpacity(1.);
		}
		if (scale > 0.) {
			q.setCompositionMode(QPainter::CompositionMode_SourceOver);
			Wallet::PaintClock(
				q,
				CardClockStyle(),
				center,
				transition.pose,
				color,
				scale);
		}
	}
	p.drawImage(origin, transition.frame);
}

void GramTransferCardPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	const auto now = crl::now();
	const auto frame = context.now ? context.now : now;
	const auto paused = context.paused
		|| anim::Disabled()
		|| On(PowerSaving::kStickersChat);
	if (_transition && transitionFinished(now)) {
		_transition = nullptr;
	}
	validateAngle(p, owner, context);
	validateRead(now, frame, paused);
	// WHY: a card relaid out as settled keeps its clock until this paint,
	// so a stale paint shows the live pose and the reveal starts from it
	// here; a later replacement continues this transition.
	validateReveal(now);
	validateMark();
	validateClock(now);
	validateBadge();
	if (std::exchange(_heavyPending, false)) {
		if (const auto view = _origin.view.get()) {
			view->history()->owner().registerHeavyViewPart(view);
		}
	}
	const auto still = context.paused || anim::Disabled();
	const auto look = sendingLook(now);
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	const auto outer = QRect(0, 0, width(), height());
	const auto radius = st::msgServiceGiftBoxRadius;
	auto clip = QPainterPath();
	clip.addRoundedRect(outer, radius, radius);
	p.setClipPath(clip, Qt::IntersectClip);
	const auto sweep = this->sweep(now, frame, still);
	(sweep.own ? *sweep.own : _angle->background()).paint(
		p,
		_layout.card,
		sweep.angle);
	p.translate(_layout.card.topLeft());
	const auto cardWidth = _layout.card.width();
	const auto pass = glarePass(now);
	if (pass) {
		paintGlareBorder(p, *pass);
	}
	if (_transition && _transition->burst && !still) {
		paintBurst(p, now);
	}
	if (_mark->valid()) {
		const auto repaint = [view = _origin.view] {
			if (const auto strong = view.get()) {
				strong->repaint();
			}
		};
		if (_clock) {
			validateLoop(now, paused);
		} else if (playingRead(now)) {
			advanceRead(now);
		} else if (!paused && !_markStarted && !awaitingRead()) {
			_markStarted = true;
			_mark->animate(repaint, 0, _mark->framesCount() - 1);
		}
		if (!looping()
			&& !playingRead(now)
			&& !_mark->animating()
			&& _mark->frameIndex() != 0) {
			// The white diamond's last frame leads into frame 0, its rest.
			_mark->jumpTo(0, repaint);
		}
	}
	if (_transition && _transition->fast && !_transition->fastStarted) {
		// Started by its first paint, so no tick can render frame 1 first.
		_transition->fastStarted = true;
		const auto fast = _transition->fast.get();
		fast->animate(nullptr, 0, fast->framesCount() - 1);
	}
	const auto mark = markPaintRect();
	const auto icon = (_transition && PlayingFast(*_transition))
		? _transition->fast.get()
		: _mark.get();
	icon->paint(p, mark.x(), mark.y());
	const auto amountTopLeft = QPointF(
		(cardWidth - _amount.size().width()) / 2.,
		_layout.amountTop);
	const auto amountColors = Wallet::AmountColors{
		.digits = st::activeButtonFg->c,
		.ticker = CardTickerFg(),
	};
	if (rolling(now)) {
		const auto &read = *_transition->read;
		_amount.paintRolling(
			p,
			amountTopLeft,
			amountColors,
			ReadRollPositions(
				_amount.parts(),
				read.started ? (now - read.started) : crl::time()));
	} else {
		_amount.paint(p, amountTopLeft, amountColors);
	}
	p.setPen(CardTickerFg());
	p.setFont(st::walletCardNameFont);
	p.drawText(
		(cardWidth - st::walletCardNameFont->width(_layout.identity)) / 2,
		_layout.identityTop + st::walletCardNameFont->ascent,
		_layout.identity);
	const auto addressFont
		= st::walletDetailsCollectionLabel.style.font->monospace();
	if (pass) {
		auto gradient = QLinearGradient(
			QPointF(pass->from, 0),
			QPointF(pass->till, 0));
		gradient.setStops({
			{ 0., CardAddressFg() },
			{ 0.5, CardTickerFg() },
			{ 1., CardAddressFg() },
		});
		p.setPen(QPen(QBrush(gradient), 0));
	} else {
		p.setPen(CardAddressFg());
	}
	p.setFont(addressFont);
	auto top = _layout.addressTop;
	for (const auto &line : _layout.addressLines) {
		p.drawText(
			(cardWidth - addressFont->width(line)) / 2,
			top + addressFont->ascent,
			line);
		top += addressFont->height;
	}
	if (look) {
		paintSendingClock(p, now);
	} else if (!rolling(now)) {
		if (_transition && now < _transition->started + kRevealDuration) {
			paintReveal(p, cardWidth, now);
		} else {
			p.drawImage(
				QPointF(
					cardWidth - _badge.width() / _badge.devicePixelRatio(),
					0.),
				_badge);
		}
	}
	p.restore();
}

TextState GramTransferCardPart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	if (_layout.card.contains(point)) {
		auto result = TextState();
		result.link = _detailsLink;
		return result;
	}
	return {};
}

}

}
