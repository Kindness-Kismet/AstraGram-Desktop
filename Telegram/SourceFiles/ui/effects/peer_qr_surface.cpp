#include "ui/effects/peer_qr_surface_internal.h"

namespace Ui {

using namespace PeerQrSurfaceDetails;


struct PeerQrSurface::Card {
	CardLayout layout;
	CardContent content;
	std::optional<QrSwap> swap;
	QImage qrMask;
};

struct PeerQrSurface::Countdown {
	style::font font = st::peerQrUsernameLarge;
	std::vector<std::unique_ptr<AnimatedString>> glyphs;
};

struct PeerQrSurface::Control {
	base::weak_qptr<QWidget> widget;
	Fn<QPainterPath()> shape;
	Fn<float64()> opacity;
};

struct PeerQrSurface::ControlShapes {
	struct Fading {
		QPainterPath path;
		QPainterPath shown;
		std::vector<QRect> parts;
		float64 opacity = 0.;
	};
	QPainterPath shown;
	std::vector<Fading> fading;
};

int PeerQrCardHeight(int cardWidth, bool withUsername) {
	return ComputeCardLayout(cardWidth, withUsername).size.height();
}

float64 PeerQrBounceScale(float64 progress) {
	const auto phase = [&](float64 from, float64 till) {
		return (progress - from) / (till - from);
	};
	if (progress < 0. || progress >= 1.) {
		return 1.;
	} else if (progress <= kBouncePeakAt) {
		const auto rise = anim::easeOutCubic(1., phase(0., kBouncePeakAt));
		return 1. + (kBouncePeak - 1.) * rise;
	} else if (progress <= kBounceTroughAt) {
		const auto fall = anim::sineInOut(
			1.,
			phase(kBouncePeakAt, kBounceTroughAt));
		return kBouncePeak + (kBounceTrough - kBouncePeak) * fall;
	}
	const auto settle = anim::sineInOut(1., phase(kBounceTroughAt, 1.));
	return kBounceTrough + (1. - kBounceTrough) * settle;
}

PeerQrAppearance PeerQrDefaultAppearance() {
	return {
		.edge = QColor(0x0e, 0x0f, 0x0f),
		.center = QColor(0x36, 0x37, 0x38),
	};
}

QImage RenderPeerQrImage(PeerQrImageArgs args) {
	const auto layout = ComputeCardLayout(
		args.cardWidth,
		!args.usernameLines.isEmpty());
	auto content = PrepareCardContent(
		layout,
		args.link,
		args.colors.ink,
		args.shadowRadius,
		args.shadowShift,
		1);
	content.username = UsernameFit{
		.font = std::move(args.usernameFont),
		.lines = std::move(args.usernameLines),
	};
	const auto margin = QSize(2 * args.margin, 2 * args.margin);
	const auto size = layout.size + margin;
	const auto card = QRectF(
		QPointF(args.margin, args.margin),
		QSizeF(layout.size));
	const auto &appearance = args.appearance;
	auto result = ComposeBackground({
		.size = size,
		.center = appearance.center,
		.edge = appearance.edge,
		.gradientCenter = card.center(),
		.gradientRadius = kGradientRadiusToCard * args.cardWidth,
	});
	if (!result.isNull()) {
		const auto falloff = PeerQrLayerFalloff{
			.area = QRectF(QPointF(), QSizeF(size)),
			.band = args.layerBand,
		};
		auto p = QPainter(&result);
		PaintPeerQrEmojiRest(
			p,
			falloff,
			card.center() + args.layerOffset,
			card,
			args.emoji);
		PaintPeerQrGiftRest(
			p,
			falloff,
			card.center() + args.layerOffset,
			card,
			args.gift);
		p.translate(args.margin, args.margin);
		PaintCard(p, layout, content, CardFrame());
	}
	return result;
}

PeerQrSurface::PeerQrSurface(
	QWidget *parent,
	PeerQrAppearance appearance)
: RpWidget(parent)
, _appearance(std::move(appearance))
, _colors(ComputePeerQrColors(_appearance))
, _shownColors(_colors)
, _card(std::make_unique<Card>())
, _corners(Images::CornersMask(st::boxRadius))
, _animation([=](crl::time now) { return animationCallback(now); }) {
	setAttribute(Qt::WA_AcceptTouchEvents);
	sizeValue() | rpl::skip(1) | rpl::on_next([=] {
		composeNow();
	}, lifetime());
	PowerSaving::Changes() | rpl::on_next([=] {
		refreshFieldClock();
		checkAnimation();
		update();
	}, lifetime());
	if (!_appearance.emoji.empty()) {
		ensureField();
		_field->setPicks(
			_appearance.emoji,
			PeerQrEmojiChange::Instant,
			crl::now());
	}
	if (_appearance.gift) {
		_giftField = std::make_unique<PeerQrGiftField>(
			_giftSource,
			_appearance.gift,
			crl::guard(this, [=] { updateField(); }),
			true);
	}
}

PeerQrSurface::~PeerQrSurface() = default;

void PeerQrSurface::setLink(const QString &link, anim::type animated) {
	if (_link == link) {
		return;
	}
	const auto swap = (animated == anim::type::normal)
		&& !anim::Disabled()
		&& (_startedAt > 0)
		&& !_cardGeometry.isEmpty()
		&& !link.isEmpty();
	if (swap) {
		const auto startedAt = crl::now() + kSwapDelay;
		_card->swap = QrSwap{
			.qr = base::take(_card->content.qr),
			.data = base::take(_card->content.data),
			.startedAt = startedAt,
		};
		_bounceStartedAt = startedAt;
		_blooming = false;
	} else {
		_card->swap = std::nullopt;
	}
	_link = link;
	refreshQr();
	if (swap) {
		checkAnimation();
	}
}

void PeerQrSurface::setUsername(const QString &username) {
	const auto wasCountdown = (_countdown != nullptr);
	_countdown = nullptr;
	if (_username != username) {
		_username = username;
		refreshCard();
	} else if (wasCountdown) {
		update(labelUpdateRect());
	}
}

void PeerQrSurface::setCountdown(const QString &countdown) {
	if (_countdown && _username == countdown) {
		return;
	}
	const auto instant = !_countdown
		|| (_username == u"0:00"_q)
		|| (countdown == u"0:00"_q)
		|| anim::Disabled();
	const auto layoutChanged = _username.isEmpty() != countdown.isEmpty();
	if (!_countdown) {
		_countdown = std::make_unique<Countdown>();
	}
	_username = countdown;
	if (layoutChanged) {
		refreshCard();
	} else {
		refreshUsername(instant ? anim::type::instant : anim::type::normal);
		update(labelUpdateRect());
	}
}

void PeerQrSurface::setCardGeometry(QRect card) {
	if (_cardGeometry != card) {
		const auto moved = (_cardGeometry.center() != card.center());
		_cardGeometry = card;
		refreshCard();
		if (moved && _appearance.center.isValid()) {
			composeNow();
		}
	}
}

void PeerQrSurface::setCardPlacement(QPointF topLeft, float64 scale) {
	if (_cardPlacement
		&& _cardPlacement->topLeft == topLeft
		&& _cardPlacement->scale == scale) {
		return;
	}
	_cardPlacement = CardPlacement{ .topLeft = topLeft, .scale = scale };
	update();
}

void PeerQrSurface::setAppearance(
		PeerQrAppearance appearance,
		anim::type animated) {
	if (_appearance == appearance) {
		return;
	}
	using Change = PeerQrEmojiChange;
	const auto now = crl::now();
	const auto animatedNow = (animated == anim::type::normal)
		&& !anim::Disabled()
		&& (_startedAt > 0);
	const auto wasEmoji = !_appearance.emoji.empty();
	const auto isEmoji = !appearance.emoji.empty();
	const auto change = animatedNow ? Change::Swap : Change::Instant;
	if (isEmoji
		&& !_fieldLeaving
		&& SameBackdrop(_appearance, appearance)) {
		const auto entering = !_field;
		_appearance = std::move(appearance);
		ensureField();
		if (entering) {
			_field->setPicks(
				_appearance.emoji,
				animatedNow ? Change::Entry : Change::Instant,
				now);
			if (animatedNow) {
				_field->startEntry(now);
			}
		} else {
			_field->setPicks(_appearance.emoji, change, now);
		}
		if (animatedNow) {
			_dipStartedAt = now;
		}
		checkAnimation();
		return;
	}
	const auto wave = animatedNow && !_cardGeometry.isEmpty();
	auto from = wave ? currentPicture(now, false) : QImage();
	auto clean = (wave && _wave && !_wave->clean.isNull())
		? currentPicture(now, true)
		: QImage();
	if (wave && (_fieldLeaving || _giftLeaving)) {
		if (clean.isNull()) {
			clean = from;
		}
		auto p = QPainter(&from);
		if (_fieldLeaving) {
			paintField(p, now, QPainterPath());
		}
		if (_giftLeaving) {
			paintGift(p, _giftLeaving.get(), now, true, QPainterPath());
		}
		p.end();
		if (base::take(_fieldLeaving)) {
			_field = nullptr;
		}
		_giftLeaving = nullptr;
	}
	_appearance = std::move(appearance);
	if (isEmoji) {
		const auto entering = !wasEmoji || !_field;
		ensureField();
		if (entering) {
			_field->setPicks(
				_appearance.emoji,
				wave ? Change::Entry : Change::Instant,
				now);
			_entryPending = wave;
		} else {
			_field->setPicks(_appearance.emoji, change, now);
		}
		_fieldLeaving = false;
	} else if (_field) {
		_entryPending = false;
		_fieldLeaving = wave;
		if (!wave) {
			_field = nullptr;
		}
	}
	if (_giftField && _giftField->look() != _appearance.gift) {
		_giftLeaving = base::take(_giftField);
	}
	if (!wave) {
		_giftLeaving = nullptr;
	}
	if (_appearance.gift && !_giftField) {
		_giftField = std::make_unique<PeerQrGiftField>(
			_giftSource,
			_appearance.gift,
			crl::guard(this, [=] { updateField(); }),
			!wave);
		_entryPending = _entryPending || wave;
		checkAnimation();
	}
	_colors = ComputePeerQrColors(_appearance);
	composeNow();
	if (isEmoji) {
		_field->setTextColor(_colors.foreground);
	}
	if (!wave) {
		_wave = std::nullopt;
		if (base::take(_entryPending)) {
			startFieldEntry(now);
		}
		setShownColors(_colors);
		return;
	}
	const auto center = cardRect().center();
	_wave = Wave{
		.from = std::move(from),
		.clean = std::move(clean),
		.colorsFrom = _shownColors,
		.tint = ComputeWaveTint(_appearance),
		.center = center,
		.farthest = FarthestCorner(center, QSizeF(size())),
		.startedAt = now,
	};
	_bounceStartedAt = now;
	checkAnimation();
}

void PeerQrSurface::setEmojiSource(PeerQrEmojiSource source) {
	_emojiSource = source;
	if (_field) {
		_field->setSource(std::move(source));
	}
}

void PeerQrSurface::setGiftSource(PeerQrGiftSource source) {
	_giftSource = std::move(source);
	if (_giftField) {
		_giftField->setSource(_giftSource);
	}
	if (_giftLeaving) {
		_giftLeaving->setSource(_giftSource);
	}
}

void PeerQrSurface::startAnimations() {
	_startedAt = crl::now();
	_fieldFrozenAt = _fieldShift = 0;
	_blooming = true;
	refreshFieldClock();
	checkAnimation();
}

const PeerQrAppearance &PeerQrSurface::appearance() const {
	return _appearance;
}

const PeerQrColors &PeerQrSurface::colors() const {
	return _shownColors;
}

rpl::producer<PeerQrColors> PeerQrSurface::colorsValue() const {
	return _colorsChanges.events_starting_with_copy(_shownColors);
}

rpl::producer<not_null<QTouchEvent*>> PeerQrSurface::touchPresses() const {
	return _touchPresses.events();
}

PeerQrImageArgs PeerQrSurface::prepareImage() const {
	const auto scaled = [](int value) {
		const auto recovered = value * 100. / style::Scale();
		return kImageScale * int(base::SafeRound(recovered));
	};
	const auto scaledFont = [&](const style::font &font) {
		auto result = font->f;
		result.setPixelSize(scaled(font->f.pixelSize()));
		return result;
	};
	const auto metric = [&](int value) {
		return float64(scaled(value));
	};
	const auto width = scaled(st::peerQrCardWidth);
	const auto layout = ComputeCardLayout(width, !_username.isEmpty());
	const auto available = width - 2 * scaled(st::peerQrUsernameSkip);
	auto fit = UsernameFit();
	if (_cardGeometry.isEmpty()) {
		fit = FitUsername(
			_username,
			available,
			layout.band.height(),
			{
				scaledFont(st::peerQrUsernameLarge),
				scaledFont(st::peerQrUsernameMedium),
				scaledFont(st::peerQrUsernameSmall),
			},
			scaled(st::peerQrUsernameMin));
	} else if (!_card->content.username.lines.isEmpty()) {
		fit = _card->content.username;
		// WHY: the image keeps the screen's lines, and Qt advances do not scale
		// exactly, so a converted size that overflows shrinks until it fits.
		for (auto pixels = scaled(fit.font.pixelSize()); ; --pixels) {
			fit.font.setPixelSize(pixels);
			const auto metrics = QFontMetricsF(fit.font);
			const auto fits = LinesFit(
				fit.lines,
				metrics,
				available,
				layout.band.height());
			if (fits || pixels <= 1) {
				break;
			}
		}
	}
	return {
		.appearance = _appearance,
		.colors = _colors,
		.link = _link,
		.usernameFont = std::move(fit.font),
		.usernameLines = std::move(fit.lines),
		.cardWidth = width,
		.margin = (scaled(st::peerQrBoxWidth) - width) / 2,
		.shadowRadius = scaled(st::peerQrCardShadowRadius),
		.shadowShift = scaled(st::peerQrCardShadowShift),
		.emoji = ((_field && !_appearance.emoji.empty())
			? _field->prepareRest({
				.skipX = metric(st::peerQrEmojiSkipX),
				.skipY = metric(st::peerQrEmojiSkipY),
				.size = metric(st::peerQrEmojiSize),
				.offsetMin = metric(st::peerQrEmojiOffsetMin),
				.offsetMax = metric(st::peerQrEmojiOffsetMax),
				.bob = metric(st::peerQrEmojiBob),
			})
			: PeerQrEmojiRest()),
		.gift = (_giftField
			? _giftField->prepareRest({
				.modelSize = metric(st::peerQrGiftModelSize),
				.modelSkipX = metric(st::peerQrGiftModelSkipX),
				.modelSkipY = metric(st::peerQrGiftModelSkipY),
				.modelOffset = metric(st::peerQrGiftModelOffset),
				.symbolSize = metric(st::peerQrGiftSymbolSize),
				.symbolSkipX = metric(st::peerQrGiftSymbolSkipX),
				.symbolSkipY = metric(st::peerQrGiftSymbolSkipY),
				.symbolJitterX = metric(st::peerQrGiftSymbolJitterX),
				.symbolJitterY = metric(st::peerQrGiftSymbolJitterY),
			})
			: PeerQrGiftRest()),
		.layerOffset = _layer.offset()
			* (kImageScale * 100. / style::Scale()),
		.layerBand = metric(st::peerQrLayerBand),
	};
}

void PeerQrSurface::addControl(
		not_null<QWidget*> control,
		Fn<QPainterPath()> shape,
		Fn<float64()> opacity) {
	Expects(control->parentWidget() == this);

	_controls.push_back({
		.widget = base::make_weak(control),
		.shape = std::move(shape),
		.opacity = std::move(opacity),
	});
	update();
}

void PeerQrSurface::paintEvent(QPaintEvent *e) {
	const auto now = crl::now();
	if (backgroundStale()) {
		requestBackground();
	}
	const auto controls = controlShapes(e->rect());
	auto p = QPainter(this);
	if (_wave) {
		paintWave(p, now, controls.shown);
	} else {
		p.drawImage(0, 0, _background);
	}
	if (_cardGeometry.isEmpty()) {
		return;
	}
	if (layerShown()) {
		_layer.advance(now, fieldElapsed(now), st::peerQrDriftSpeed);
	}
	paintLayers(p, now, controls.shown);
	paintFadingUnderlays(p, now, controls);
	const auto still = anim::Disabled();
	const auto elapsed = (_startedAt > 0) ? (now - _startedAt) : 0;
	auto frame = still
		? CardFrame()
		: ComputeCardFrame(elapsed, _card->layout.logo.width() / 2.);
	const auto placed = cardScale();
	p.translate(cardRect().topLeft());
	if (placed != 1.) {
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.scale(placed, placed);
	}
	if (!still) {
		if (_startedAt > 0 && !_blooming) {
			frame.bloom = 1.;
		}
		if (const auto &swap = _card->swap) {
			frame.swap = &*swap;
			frame.swapElapsed = now - swap->startedAt;
		}
		const auto bounce = _bounceStartedAt
			? PeerQrBounceScale(
				float64(now - _bounceStartedAt) / kBounceDuration)
			: 1.;
		const auto scale = bounce * DipScale(_dipStartedAt, now);
		if (scale != 1.) {
			const auto center = QRectF(
				QPointF(),
				QSizeF(_card->layout.size)).center();
			p.translate(center);
			p.scale(scale, scale);
			p.translate(-center);
		}
	}
	PaintCard(p, _card->layout, _card->content, frame, !_countdown);
	if (_countdown) {
		paintCountdown(p);
	}
}

void PeerQrSurface::paintCountdown(QPainter &p) {
	const auto &fit = _card->content.username;
	if (fit.lines.isEmpty()) {
		return;
	}
	const auto &font = _countdown->font;
	const auto scale = float64(fit.font.pixelSize()) / font->f.pixelSize();
	const auto metrics = QFontMetricsF(fit.font);
	const auto height = metrics.height();
	const auto &band = _card->layout.band;
	auto top = band.y() + (band.height() - fit.lines.size() * height) / 2.;
	auto index = 0;
	p.save();
	p.setClipRect(band, Qt::IntersectClip);
	for (const auto &line : fit.lines) {
		const auto left = band.x()
			+ (band.width() - metrics.horizontalAdvance(line)) / 2.;
		for (auto i = 0; i != line.size(); ++i) {
			const auto &glyph = _countdown->glyphs[index++];
			if (anim::Disabled()) {
				glyph->finishAnimating();
			}
			p.save();
			p.translate(
				left + metrics.horizontalAdvance(line.left(i)),
				top + metrics.ascent() - font->ascent * scale);
			p.scale(scale, scale);
			glyph->draw(p, 0, 0, _shownColors.ink);
			p.restore();
		}
		top += height;
	}
	p.restore();
}

bool PeerQrSurface::eventHook(QEvent *e) {
	const auto type = e->type();
	if (type == QEvent::TouchBegin
		|| type == QEvent::TouchUpdate
		|| type == QEvent::TouchEnd
		|| type == QEvent::TouchCancel) {
		const auto touch = static_cast<QTouchEvent*>(e);
		const auto device = touch->device();
		if (device
			&& device->type() == base::TouchDevice::TouchScreen
			&& handleTouch(touch)) {
			touch->accept();
			return true;
		}
	} else if (type == QEvent::WindowDeactivate) {
		stopTouchSwipe();
	}
	return RpWidget::eventHook(e);
}

void PeerQrSurface::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton
		&& layerShown()
		&& !cardRect().contains(QPointF(e->pos()))
		&& !_touchId) {
		_layer.press(e->pos(), crl::now());
	} else {
		e->ignore();
	}
}

void PeerQrSurface::mouseMoveEvent(QMouseEvent *e) {
	if (_layer.pressed() && !_touchId) {
		_layer.move(e->pos(), crl::now());
		update();
	} else {
		e->ignore();
	}
}

void PeerQrSurface::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && _layer.pressed() && !_touchId) {
		_layer.release(crl::now(), !anim::Disabled());
		checkAnimation();
		update();
	} else {
		e->ignore();
	}
}

bool PeerQrSurface::handleTouch(not_null<QTouchEvent*> e) {
	const auto &points = e->touchPoints();
	switch (e->type()) {
	case QEvent::TouchBegin: {
		stopTouchSwipe();
		_touchId = std::nullopt;
		if (points.isEmpty() || !layerShown() || _layer.pressed()) {
			return false;
		}
		const auto &point = points.front();
		const auto position = point.pos();
		if (cardRect().contains(position) || childAt(position.toPoint())) {
			return false;
		}
		_touchId = point.id();
		_layer.press(position, crl::now());
		_touchPresses.fire_copy(e);
		return true;
	}
	case QEvent::TouchUpdate:
	case QEvent::TouchEnd: {
		if (!_touchId) {
			return false;
		}
		const auto i = ranges::find(
			points,
			*_touchId,
			&QTouchEvent::TouchPoint::id);
		if (i != points.end() && _layer.pressed()) {
			const auto state = int(i->state());
			if (state == int(Qt::TouchPointMoved)) {
				_layer.move(i->pos(), crl::now());
				update();
			} else if (state == int(Qt::TouchPointReleased)) {
				_layer.release(crl::now(), !anim::Disabled());
				checkAnimation();
				update();
			}
		}
		if (e->type() == QEvent::TouchEnd) {
			stopTouchSwipe();
			_touchId = std::nullopt;
		}
		// Qt synthesizes mouse events from any touch event left unaccepted.
		return true;
	}
	case QEvent::TouchCancel:
		if (!_touchId) {
			return false;
		}
		stopTouchSwipe();
		_touchId = std::nullopt;
		return true;
	default:
		return false;
	}
}

void PeerQrSurface::stopTouchSwipe() {
	if (_touchId && _layer.pressed()) {
		_layer.release(crl::now(), false);
		update();
	}
}

void PeerQrSurface::composeNow() {
	++_backgroundGeneration;
	applyBackground(prepareBackground()());
}

void PeerQrSurface::checkAnimation() {
	if (_startedAt > 0
		&& !_animation.animating()
		&& !(anim::Disabled()
			&& PowerSaving::On(PowerSaving::kChatBackground))) {
		_animation.start();
	}
}

bool PeerQrSurface::animationCallback(crl::time now) {
	const auto still = anim::Disabled();
	const auto frozen = PowerSaving::On(PowerSaving::kChatBackground);
	if (_dipStartedAt && (still || now >= _dipStartedAt + kDipDuration)) {
		_dipStartedAt = 0;
	}
	const auto whole = (_field && (!frozen || _field->animating(now)))
		|| (_giftField && (!frozen || _giftField->animating(now)))
		|| (layerShown() && _layer.animating());
	const auto refresh = [&](QRect region) {
		if (whole) {
			updateField();
		} else {
			update(region);
		}
	};
	if (_wave) {
		updateWave(now);
		update();
	} else if (still && !whole) {
		return false;
	} else if (!still && _blooming) {
		_blooming = (now - _startedAt < kBloomDuration);
		refresh(cardRect().toAlignedRect());
	} else if (!still && (_card->swap || _bounceStartedAt || _dipStartedAt)) {
		if (_card->swap && now >= _card->swap->startedAt + kSwapDuration) {
			_card->swap = std::nullopt;
		}
		if (_bounceStartedAt && now >= _bounceStartedAt + kBounceDuration) {
			_bounceStartedAt = 0;
		}
		refresh(cardUpdateRect());
	} else {
		refresh(logoRect());
	}
	return true;
}

QPointF PeerQrSurface::gradientCenter() const {
	return _cardGeometry.isEmpty()
		? QRectF(rect()).center()
		: cardRect().center();
}

bool PeerQrSurface::backgroundStale() const {
	return !_backgroundComposing
		&& _appearance.center.isValid()
		&& (gradientCenter() != _backgroundCenter);
}

Fn<QImage()> PeerQrSurface::prepareBackground() {
	const auto ratio = style::DevicePixelRatio();
	auto request = BackgroundRequest{
		.corners = _corners,
		.size = size() * ratio,
	};
	_backgroundCenter = gradientCenter();
	if (_appearance.center.isValid()) {
		const auto cardWidth = _cardGeometry.isEmpty()
			? st::peerQrCardWidth
			: _cardGeometry.width();
		request.center = _appearance.center;
		request.edge = _appearance.edge;
		request.gradientCenter = _backgroundCenter * ratio;
		request.gradientRadius = kGradientRadiusToCard * cardWidth * ratio;
	}
	return [request = std::move(request)] {
		return ComposeBackground(request);
	};
}

void PeerQrSurface::requestBackground() {
	_backgroundComposing = true;
	const auto generation = _backgroundGeneration;
	const auto weak = base::make_weak(this);
	crl::async([=, compose = prepareBackground()] {
		auto image = compose();
		crl::on_main(weak, [=, image = std::move(image)]() mutable {
			_backgroundComposing = false;
			if (_backgroundGeneration == generation) {
				applyBackground(std::move(image));
			} else {
				update();
			}
		});
	});
}

void PeerQrSurface::applyBackground(QImage image) {
	const auto ratio = style::DevicePixelRatio();
	if (image.size() == size() * ratio) {
		_background = std::move(image);
		_background.setDevicePixelRatio(ratio);
	}
	update();
}

void PeerQrSurface::setCovered(Fn<QRegion()> covered) {
	_covered = std::move(covered);
	update();
}

QRegion PeerQrSurface::covered() const {
	return _covered ? _covered() : QRegion();
}

void PeerQrSurface::updateField() {
	if (_wave) {
		update();
		return;
	}
	const auto region = covered();
	if (region.isEmpty()) {
		update();
	} else {
		update(QRegion(rect()) - region);
	}
}

QRectF PeerQrSurface::cardRect() const {
	return _cardPlacement
		? QRectF(
			_cardPlacement->topLeft,
			QSizeF(_cardGeometry.size()) * _cardPlacement->scale)
		: QRectF(_cardGeometry);
}

float64 PeerQrSurface::cardScale() const {
	return _cardPlacement ? _cardPlacement->scale : 1.;
}

QRect PeerQrSurface::logoRect() const {
	if (_cardGeometry.isEmpty()) {
		return rect();
	}
	const auto scale = cardScale();
	const auto logo = QRectF(_card->layout.logo);
	return QRectF(
		cardRect().topLeft() + logo.topLeft() * scale,
		logo.size() * scale).toAlignedRect();
}

QRect PeerQrSurface::cardUpdateRect() const {
	if (_cardGeometry.isEmpty()) {
		return rect();
	}
	const auto shadow = st::peerQrCardShadowRadius
		+ st::peerQrCardShadowShift;
	const auto half = std::max(
		_cardGeometry.width(),
		_cardGeometry.height()) / 2.;
	const auto grow = (shadow + (half + shadow) * (kBouncePeak - 1.))
		* cardScale();
	return cardRect().marginsAdded(
		{ grow, grow, grow, grow }).toAlignedRect();
}

QRect PeerQrSurface::labelUpdateRect() const {
	if (_cardGeometry.isEmpty()) {
		return rect();
	}
	const auto now = crl::now();
	const auto bounce = (!anim::Disabled() && _bounceStartedAt)
		? PeerQrBounceScale(float64(now - _bounceStartedAt) / kBounceDuration)
		: 1.;
	const auto scale = anim::Disabled()
		? 1.
		: bounce * DipScale(_dipStartedAt, now);
	const auto center = QRectF(QPointF(), QSizeF(_card->layout.size)).center();
	auto transform = QTransform();
	transform.translate(cardRect().x(), cardRect().y());
	transform.scale(cardScale(), cardScale());
	transform.translate(center.x(), center.y());
	transform.scale(scale, scale);
	transform.translate(-center.x(), -center.y());
	return transform.mapRect(QRectF(_card->layout.band)).toAlignedRect();
}

void PeerQrSurface::refreshCard() {
	if (_cardGeometry.isEmpty()) {
		*_card = Card();
		update();
		return;
	}
	const auto width = _cardGeometry.width();
	const auto layout = ComputeCardLayout(width, !_username.isEmpty());
	auto content = PrepareCardContent(
		layout,
		_link,
		_shownColors.ink,
		st::peerQrCardShadowRadius,
		st::peerQrCardShadowShift,
		style::DevicePixelRatio());
	*_card = Card{ layout, std::move(content) };
	refreshUsername();
	update();
}

void PeerQrSurface::refreshUsername(anim::type animated) {
	if (_cardGeometry.isEmpty()) {
		return;
	}
	const auto &previous = _card->content.username;
	auto fit = FitUsername(
		_username,
		_cardGeometry.width() - 2 * st::peerQrUsernameSkip,
		_card->layout.band.height(),
		{
			st::peerQrUsernameLarge->f,
			st::peerQrUsernameMedium->f,
			st::peerQrUsernameSmall->f,
		},
		st::peerQrUsernameMin);
	const auto animate = (animated == anim::type::normal)
		&& !anim::Disabled()
		&& (fit.font == previous.font)
		&& (fit.lines.size() == previous.lines.size());
	_card->content.username = std::move(fit);
	if (!_countdown) {
		return;
	}
	const auto &fitted = _card->content.username;
	_countdown->font = st::peerQrUsernameSmall;
	for (const auto &font : {
		st::peerQrUsernameLarge,
		st::peerQrUsernameMedium,
		st::peerQrUsernameSmall,
	}) {
		if (font->f == fitted.font) {
			_countdown->font = font;
			break;
		}
	}
	const auto text = _card->content.username.lines.join(QString());
	auto &glyphs = _countdown->glyphs;
	const auto sameSize = (text.size() == glyphs.size());
	glyphs.resize(text.size());
	for (auto i = 0; i != text.size(); ++i) {
		if (!glyphs[i]) {
			glyphs[i] = std::make_unique<AnimatedString>(
				_countdown->font,
				[=] { update(labelUpdateRect()); },
				AnimatedString::Options{
					.preserveIndex = true,
					.enforceByLetter = true,
				});
		}
		glyphs[i]->setText(text.mid(i, 1), animate && sameSize);
	}
}

void PeerQrSurface::refreshQr() {
	if (_cardGeometry.isEmpty()) {
		refreshCard();
		return;
	}
	auto &content = _card->content;
	content.data = _link.isEmpty() ? Qr::Data() : Qr::Encode(_link);
	content.qr = PrepareQrImage(
		content.data,
		_card->layout.qr.width(),
		_shownColors.ink,
		style::DevicePixelRatio());
	_card->qrMask = QImage();
	update(cardUpdateRect());
}

void PeerQrSurface::recolorQr() {
	auto &content = _card->content;
	content.ink = _shownColors.ink;
	update(cardUpdateRect());
	if (content.data.size <= 0) {
		return;
	}
	const auto side = _card->layout.qr.width();
	const auto ratio = style::DevicePixelRatio();
	if (_shownColors == _colors) {
		_card->qrMask = QImage();
		content.qr = PrepareQrImage(content.data, side, content.ink, ratio);
		return;
	} else if (_card->qrMask.isNull()) {
		_card->qrMask = PrepareQrImage(
			content.data,
			side,
			QColor(255, 255, 255),
			ratio);
	}
	content.qr = style::colorizeImage(_card->qrMask, content.ink);
	content.qr.setDevicePixelRatio(ratio);
}

void PeerQrSurface::setShownColors(const PeerQrColors &colors) {
	if (_shownColors == colors) {
		return;
	}
	_shownColors = colors;
	recolorQr();
	_colorsChanges.fire_copy(_shownColors);
}

void PeerQrSurface::ensureField() {
	if (_field) {
		return;
	}
	_field = std::make_unique<PeerQrEmojiField>(
		_emojiSource,
		crl::guard(this, [=] { updateField(); }));
	_field->setTextColor(_colors.foreground);
	checkAnimation();
}

void PeerQrSurface::refreshFieldClock() {
	if (!_startedAt) {
		return;
	}
	const auto now = crl::now();
	const auto frozen = PowerSaving::On(PowerSaving::kChatBackground);
	if (frozen && !_fieldFrozenAt) {
		_fieldFrozenAt = now;
	} else if (!frozen && _fieldFrozenAt) {
		_fieldShift += now - base::take(_fieldFrozenAt);
	}
}

crl::time PeerQrSurface::fieldElapsed(crl::time now) const {
	if (!_startedAt) {
		return 0;
	}
	const auto till = _fieldFrozenAt ? _fieldFrozenAt : now;
	return std::max(till - _startedAt - _fieldShift, crl::time());
}

PeerQrLayerFalloff PeerQrSurface::layerFalloff() const {
	return {
		.area = QRectF(rect()),
		.band = float64(st::peerQrLayerBand),
	};
}

bool PeerQrSurface::layerShown() const {
	return (_field && !_fieldLeaving) || (_giftField != nullptr);
}

PeerQrSurface::ControlShapes PeerQrSurface::controlShapes(QRect clip) const {
	auto result = ControlShapes();
	auto shown = std::vector<QPainterPath>();
	for (const auto &control : _controls) {
		const auto widget = control.widget.get();
		if (!widget || widget->isHidden()) {
			continue;
		}
		const auto opacity = control.opacity ? control.opacity() : 1.;
		if (opacity <= 0.) {
			continue;
		}
		auto shape = control.shape();
		if (shape.isEmpty()) {
			continue;
		}
		shape.translate(widget->pos());
		if (!shape.boundingRect().intersects(QRectF(clip))) {
			continue;
		}
		if (!control.opacity) {
			result.shown.addPath(shape);
			shown.push_back(std::move(shape));
			continue;
		}
		const auto part = shape.boundingRect().toAlignedRect();
		const auto i = ranges::find(
			result.fading,
			opacity,
			&ControlShapes::Fading::opacity);
		if (i != end(result.fading)) {
			i->path.addPath(shape);
			i->parts.push_back(part);
		} else {
			result.fading.push_back({
				.path = std::move(shape),
				.parts = { part },
				.opacity = opacity,
			});
		}
	}
	for (auto &fading : result.fading) {
		for (const auto &shape : shown) {
			const auto bounds = shape.boundingRect().toAlignedRect();
			const auto meets = [&](QRect part) {
				return part.intersects(bounds);
			};
			if (ranges::any_of(fading.parts, meets)) {
				fading.shown.addPath(shape);
			}
		}
	}
	return result;
}

QPainterPath PeerQrSurface::fieldClip(crl::time now, bool leaving) const {
	auto result = QPainterPath();
	result.addRoundedRect(QRectF(rect()), st::boxRadius, st::boxRadius);
	if (leaving && _wave) {
		const auto progress = std::clamp(
			float64(now - _wave->startedAt) / kWaveDuration,
			0.,
			1.);
		const auto radius = WaveRadius(_wave->farthest, progress);
		auto circle = QPainterPath();
		circle.addEllipse(_wave->center, radius, radius);
		result = result.subtracted(circle);
	}
	return result;
}

PeerQrEmojiPaint PeerQrSurface::fieldArgs(crl::time now) const {
	return {
		.falloff = layerFalloff(),
		.origin = QRectF(_cardGeometry).center() + _layer.offset(),
		.card = cardRect(),
		.now = now,
		.elapsed = fieldElapsed(now),
		.paused = PowerSaving::On(PowerSaving::kEmojiReactions),
	};
}

void PeerQrSurface::clipField(
		QPainter &p,
		crl::time now,
		bool leaving,
		const QPainterPath &shown) const {
	auto clip = fieldClip(now, leaving);
	if (!shown.isEmpty()) {
		const auto radius = float64(st::boxRadius);
		const auto inner = QRectF(rect()).marginsRemoved(
			{ radius, radius, radius, radius });
		// WHY: the shapes become even-odd holes in the field's own clip, which
		// needs them off its rounded corners; a leaving layer's clip has the
		// wave circle cut out, where such a hole would turn back into field.
		if ((leaving && _wave) || !inner.contains(shown.boundingRect())) {
			ClipOutside(p, QRectF(rect()), shown);
		} else {
			clip.addPath(shown);
		}
	}
	p.setClipPath(clip, Qt::IntersectClip);
}

void PeerQrSurface::paintField(
		QPainter &p,
		crl::time now,
		const QPainterPath &shown) {
	auto hq = PainterHighQualityEnabler(p);
	p.save();
	clipField(p, now, _fieldLeaving, shown);
	_field->paint(p, fieldArgs(now));
	p.restore();
}

void PeerQrSurface::paintGift(
		QPainter &p,
		not_null<PeerQrGiftField*> field,
		crl::time now,
		bool leaving,
		const QPainterPath &shown) {
	auto args = fieldArgs(now);
	args.paused = PowerSaving::On(PowerSaving::kStickersChat);
	auto hq = PainterHighQualityEnabler(p);
	p.save();
	clipField(p, now, leaving, shown);
	field->paint(p, args);
	p.restore();
}

void PeerQrSurface::paintLayers(
		QPainter &p,
		crl::time now,
		const QPainterPath &shown) {
	if (_field) {
		paintField(p, now, shown);
	}
	if (_giftLeaving) {
		paintGift(p, _giftLeaving.get(), now, true, shown);
	}
	if (_giftField) {
		paintGift(p, _giftField.get(), now, false, shown);
	}
}

void PeerQrSurface::paintFadingUnderlays(
		QPainter &p,
		crl::time now,
		const ControlShapes &controls) const {
	const auto behind = _field
		|| _giftLeaving
		|| _giftField
		|| (_wave && !_wave->clean.isNull());
	if (controls.fading.empty() || !behind) {
		return;
	}
	const auto tint = _shownColors.control.alphaF();
	auto hq = PainterHighQualityEnabler(p);
	for (const auto &fading : controls.fading) {
		const auto area = fading.path.boundingRect().toAlignedRect();
		// WHY: the control paints its tint at opacity * tint alpha after this,
		// so the clean backdrop goes once over the finished picture at the alpha
		// that makes the backdrop and the tint fade together as one layer.
		const auto alpha = fading.opacity * (1. - tint)
			/ (1. - fading.opacity * tint);
		p.save();
		p.setClipPath(fading.path, Qt::IntersectClip);
		ClipOutside(p, QRectF(area), fading.shown);
		p.setOpacity(alpha);
		paintClean(p, now, area, fading.parts);
		p.restore();
	}
}

void PeerQrSurface::startFieldEntry(crl::time now) {
	if (_field && !_fieldLeaving) {
		_field->startEntry(now);
	}
	if (_giftField) {
		_giftField->startEntry(now);
	}
}

void PeerQrSurface::updateWave(crl::time now) {
	const auto progress = anim::Disabled()
		? 1.
		: std::clamp(
			float64(now - _wave->startedAt) / kWaveDuration,
			0.,
			1.);
	const auto radius = WaveRadius(_wave->farthest, progress);
	if (_entryPending && radius >= _wave->farthest) {
		_entryPending = false;
		startFieldEntry(now);
	}
	if (progress >= 1.) {
		_wave = std::nullopt;
		if (base::take(_fieldLeaving)) {
			_field = nullptr;
		}
		_giftLeaving = nullptr;
		setShownColors(_colors);
		return;
	}
	const auto blend = anim::easeOutCubic(
		1.,
		std::clamp(radius / st::peerQrWaveBlend, 0., 1.));
	setShownColors((blend >= 1.)
		? _colors
		: MixColors(_wave->colorsFrom, _colors, blend));
}

void PeerQrSurface::paintWave(
		QPainter &p,
		crl::time now,
		const QPainterPath &clean) const {
	const auto &wave = *_wave;
	const auto progress = std::clamp(
		float64(now - wave.startedAt) / kWaveDuration,
		0.,
		1.);
	const auto radius = WaveRadius(wave.farthest, progress);
	if (radius < wave.farthest) {
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(0, 0, wave.from);
		if (!wave.clean.isNull() && !clean.isEmpty()) {
			p.save();
			p.setClipPath(clean, Qt::IntersectClip);
			p.drawImage(0, 0, wave.clean);
			p.restore();
		}
		auto circle = QPainterPath();
		circle.addEllipse(wave.center, radius, radius);
		p.save();
		p.setClipPath(circle);
		p.drawImage(0, 0, _background);
		p.restore();
	} else {
		p.drawImage(0, 0, _background);
	}
	PaintWaveGlow(p, rect(), rect(), wave.center, radius, progress, wave.tint);
}

void PeerQrSurface::paintClean(
		QPainter &p,
		crl::time now,
		QRect area,
		const std::vector<QRect> &parts) const {
	if (!_wave) {
		p.drawImage(0, 0, _background);
		return;
	}
	const auto &wave = *_wave;
	const auto progress = std::clamp(
		float64(now - wave.startedAt) / kWaveDuration,
		0.,
		1.);
	const auto radius = WaveRadius(wave.farthest, progress);
	const auto &old = wave.clean.isNull() ? wave.from : wave.clean;
	const auto side = (radius < wave.farthest)
		? CircleSide(wave.center, radius, parts)
		: WaveSide::Inside;
	if (side == WaveSide::Inside) {
		p.drawImage(0, 0, _background);
	} else if (side == WaveSide::Outside) {
		p.drawImage(0, 0, old);
	} else {
		auto circle = QPainterPath();
		circle.addEllipse(wave.center, radius, radius);
		p.save();
		// each pixel gets exactly one of the two pictures
		p.setRenderHint(QPainter::Antialiasing, false);
		p.save();
		ClipOutside(p, QRectF(area), circle);
		p.drawImage(0, 0, old);
		p.restore();
		p.setClipPath(circle, Qt::IntersectClip);
		p.drawImage(0, 0, _background);
		p.restore();
	}
	PaintWaveGlow(p, rect(), area, wave.center, radius, progress, wave.tint);
}

QImage PeerQrSurface::currentPicture(crl::time now, bool clean) const {
	if (!_wave) {
		return _background;
	}
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto area = QPainterPath();
	if (clean) {
		area.addRect(QRectF(rect()));
	}
	auto p = QPainter(&result);
	paintWave(p, now, area);
	p.end();
	return result;
}

} // namespace Ui
