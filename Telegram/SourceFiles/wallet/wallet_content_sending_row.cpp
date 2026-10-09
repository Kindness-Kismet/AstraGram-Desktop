#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

[[nodiscard]] int DigitsHeight(const style::font &font) {
	return int(base::SafeRound(
		-font->metrics().tightBoundingRect(u"0123456789"_q).top()));
}

[[nodiscard]] float64 SettleRamp(crl::time t, crl::time from, crl::time till) {
	return std::clamp(float64(t - from) / (till - from), 0., 1.);
}

[[nodiscard]] int SendingRowDiamondCanvas() {
	return GramDiamondCanvas(st::walletSendingRowAmountFont);
}

[[nodiscard]] int SendingRowDiamondLeft(int width) {
	const auto canvas = SendingRowDiamondCanvas();
	const auto drawnRight = width - st::walletRowPadding.right();
	return int(base::SafeRound(drawnRight - canvas * kGramDiamondRight));
}

[[nodiscard]] float64 SendingRowAmountRight(int width) {
	const auto canvas = SendingRowDiamondCanvas();
	return SendingRowDiamondLeft(width)
		+ canvas * kGramDiamondLeft
		- st::walletSendingRowDiamondSkip;
}

[[nodiscard]] float64 SendingRowEntrance(crl::time elapsed) {
	const auto z = kSendingRowEntranceDamping;
	const auto wd = M_PI / kSendingRowEntrancePeak;
	const auto wn = wd / std::sqrt(1. - z * z);
	const auto t = float64(elapsed);
	return 1. - std::exp(-z * wn * t)
		* (std::cos(wd * t) + (z * wn / wd) * std::sin(wd * t));
}

[[nodiscard]] float64 SendingRowBump(crl::time elapsed) {
	if (elapsed < 0 || elapsed >= kSendingRowBumpDuration) {
		return 0.;
	}
	const auto t = float64(elapsed);
	if (elapsed <= kSendingRowBumpPress) {
		return std::sin(M_PI / 2. * t / kSendingRowBumpPress);
	}
	const auto u = t - kSendingRowBumpPress;
	const auto w = M_PI / kSendingRowBumpRelease;
	const auto sigma = -std::log(kSendingRowBumpRebound)
		/ kSendingRowBumpRelease;
	const auto bump = std::exp(-sigma * u)
		* (std::cos(w * u) + (sigma / w) * std::sin(w * u));
	if (elapsed <= kSendingRowBumpTaper) {
		return bump;
	}
	const auto x = (t - kSendingRowBumpTaper)
		/ (kSendingRowBumpDuration - kSendingRowBumpTaper);
	return bump * (1. - x * x * (3. - 2. * x));
}

[[nodiscard]] ClockStyle SendingRowClockStyle() {
	return {
		.size = st::walletSendingRowClockSize,
		.stroke = st::walletSendingRowClockStroke,
		.minuteHand = st::walletSendingRowClockMinuteHand,
		.hourHand = st::walletSendingRowClockHourHand,
	};
}

[[nodiscard]] Ui::StarBurstDescriptor SendingRowBurstDescriptor(
		QColor color) {
	const auto mirror = style::RightToLeft() ? -1. : 1.;
	return {
		.sides = {
			{
				.sign = -mirror,
				.count = 30,
				.angle = { -8., 18. },
				.reach = { 0.15, 0.55 },
			},
			{
				.sign = mirror,
				.count = 6,
				.angle = { -35., 5. },
				.reach = { 0.03, 0.10 },
			},
		},
		.delay = kSendingRowBurstDelay,
		.spread = kSendingRowBurstSpread,
		.lifeMin = kSendingRowBurstLifeMin,
		.lifeMax = kSendingRowBurstLifeMax,
		.fall = { 0., 0.04 },
		.startX = { 0., 0.5 },
		.startY = { -0.3, 0.5 },
		.size = { 0.008, 0.027 },
		.alpha = { 0.45, 0.95 },
		.twinkle = { 0.1, 1.9 },
		.appearTill = 0.2,
		.fadeAfter = 0.55,
		.deformation = 0.1,
		.color = color,
	};
}

// WHY: the surface paints outside the row, so it lives in `layer`, an
// unclipped ancestor of `bounds` (the list's wrap), and follows `bounds`:
// hidden with it and cut at its bottom edge, so a collapsing list hides it.
SendingHistoryRow::SendingHistoryRow(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::RpWidget*> layer,
	not_null<Ui::RpWidget*> bounds,
	HistoryRowContent content,
	std::shared_ptr<CollectibleMedia> media)
: Ui::AbstractButton(parent.get())
, _layer(layer)
, _bounds(bounds)
, _media(std::move(media))
, _shadow(st::walletInfoIslandShadow)
, _surface(Ui::CreateChild<Ui::RpWidget>(layer.get())) {
	Expects(_media || !content.itemAmount);

	_digitsHeight = DigitsHeight(st::walletSendingRowAmountFont);
	if (!content.itemAmount) {
		const auto canvas = SendingRowDiamondCanvas();
		_diamond = Lottie::MakeIcon({
			.name = u"gram"_q,
			.sizeOverride = { canvas, canvas },
			.frame = -1,
			.limitFps = true,
		});
	}
	_animation.init([=](crl::time now) {
		if (!_inView || !isVisible() || anim::Disabled()) {
			releaseSettleWait();
			return false;
		}
		tickGlare(now);
		if (_waiting && !glareOwed(now)) {
			auto waiting = *base::take(_waiting);
			startSettle(
				std::move(waiting.content),
				waiting.burst,
				std::move(waiting.done));
		}
		_surface->update();
		return true;
	});

	_surface->setAttribute(Qt::WA_TransparentForMouseEvents);
	_surface->raise();
	_surface->show();
	_surface->paintRequest(
	) | rpl::on_next([=] {
		paintSurface();
	}, _surface->lifetime());
	rpl::combine(
		geometryValue(),
		parent->geometryValue(),
		bounds->geometryValue()
	) | rpl::on_next([=] {
		updateSurfaceGeometry();
	}, lifetime());
	rpl::combine(
		shownValue(),
		bounds->shownValue()
	) | rpl::on_next([=](bool shown, bool boundsShown) {
		_surface->setVisible(shown && boundsShown);
	}, lifetime());

	setContent(std::move(content));
}

int SendingHistoryRow::surfaceSkip() const {
	return (_settle && _settle->burst) ? st::walletSendingRowBurstOutset : 0;
}

void SendingHistoryRow::updateSurfaceGeometry() {
	const auto extend = _shadow.extend();
	const auto shift = st::walletSendingRowEntranceShift;
	const auto skip = surfaceSkip();
	auto geometry = Ui::MapFrom(_layer, this, rect()).marginsAdded({
		0,
		shift + extend.top() + skip,
		0,
		shift + extend.bottom() + skip,
	});
	const auto limit = Ui::MapFrom(_layer, _bounds, _bounds->rect());
	geometry.setHeight(std::clamp(
		limit.y() + limit.height() - geometry.y(),
		0,
		geometry.height()));
	_surface->setGeometry(geometry);
}

void SendingHistoryRow::setContent(HistoryRowContent content) {
	const auto peerChanged = !_userpic || (_content.peer != content.peer);
	const auto chipChanged = (_content.collectible != content.collectible);
	_content = std::move(content);
	_text = PrepareHistoryRowText(_content);
	if (!_content.itemAmount) {
		const auto amountNano = _content.amountNano;
		const auto &font = st::walletSendingRowAmountFont;
		_amount.setContent({ .big = font, .small = font }, {
			.whole = RowAmountWhole(
				amountNano,
				RowAmountSign(_content.incoming)),
			.fraction = GramMinorPart(amountNano),
		});
	}
	if (chipChanged) {
		_chipLifetime.destroy();
		_chip = HistoryRowChipState();
		if (_content.itemAmount) {
			TrackHistoryRowChip(
				&_chip,
				_media,
				_content.collectible,
				[=] { _surface->update(); },
				_chipLifetime);
		}
	}
	if (peerChanged) {
		_userpicLifetime.destroy();
		if (const auto peer = _content.peer) {
			_userpic = std::make_unique<Ui::PeerUserpicView>(
				peer->createUserpicView());
			peer->session().downloaderTaskFinished(
			) | rpl::on_next([=] {
				_surface->update();
			}, _userpicLifetime);
		} else {
			_userpic = nullptr;
		}
	}
	accessibilityNameChanged();
	updateRowLayout();
	_surface->update();
}

void SendingHistoryRow::awaitDiamond() {
	_diamondAway = true;
	_surface->update();
}

void SendingHistoryRow::landDiamond(
		std::unique_ptr<Lottie::Icon> icon,
		crl::time loopStarted) {
	if (icon) {
		_diamond = std::move(icon);
		_diamondStarted = loopStarted;
	}
	_diamondAway = false;
	_bumpAt = (anim::Disabled() || _settle) ? 0 : crl::now();
	startAnimation();
	_surface->update();
}

void SendingHistoryRow::cancelDiamondAwait() {
	_diamondAway = false;
	_surface->update();
}

void SendingHistoryRow::scheduleBump(crl::time at) {
	_bumpAt = anim::Disabled() ? 0 : at;
	startAnimation();
}

void SendingHistoryRow::settle(
		HistoryRowContent content,
		bool burst,
		Fn<void()> done) {
	if (!_settle && (_waiting || glareOwed(crl::now()))) {
		_waiting = SendingRowSettleRequest{
			.content = std::move(content),
			.done = std::move(done),
			.burst = burst,
		};
		startAnimation();
		return;
	}
	startSettle(std::move(content), burst, std::move(done));
}

void SendingHistoryRow::startSettle(
		HistoryRowContent content,
		bool burst,
		Fn<void()> done) {
	if (!_settle) {
		_settle = std::make_unique<Settle>();
		_settle->finish.setCallback([=] {
			_settle->finished = true;
			crl::on_main(this, [=] {
				if (const auto done = _settle ? _settle->done : nullptr) {
					done();
				}
			});
		});
		if (burst && !anim::Disabled()) {
			const auto scope = WindowPaletteScope(this);
			_settle->burst = Ui::StarBurst::Make(
				SendingRowBurstDescriptor(st::windowActiveTextFg->c));
		}
		_settle->finish.callOnce(settleDuration() + kSendingRowSettlePaintWait);
		updateSurfaceGeometry();
	}
	const auto peer = content.peer;
	if (!peer || peer == _content.peer) {
		_settle->userpicLifetime.destroy();
		_settle->userpic = nullptr;
	} else if (!_settle->userpic || _settle->content.peer != peer) {
		_settle->userpicLifetime.destroy();
		_settle->userpic = std::make_unique<Ui::PeerUserpicView>(
			peer->createUserpicView());
		peer->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			_surface->update();
		}, _settle->userpicLifetime);
	}
	_settle->done = std::move(done);
	_settle->content = std::move(content);
	const auto &entry = _settle->content;
	_settle->text = PrepareHistoryRowText(entry, [=] {
		_surface->update();
	});
	const auto &font = st::walletRowAmountMinorLabel.style.font;
	_settle->fraction = font->width(GramMinorPart(entry.amountNano));
	_settle->scale = DigitsHeight(st::walletRowAmountMajorLabel.style.font)
		/ float64(_digitsHeight);
	accessibilityNameChanged();
	updateRowLayout();
	startAnimation();
	_surface->update();
}

bool SendingHistoryRow::glareOwed(crl::time now) const {
	return _started
		&& !anim::Disabled()
		&& _inView
		&& isVisible()
		&& (!_glareSeen || _glare.progress(now).has_value());
}

// WHY: a pass counts only when this run of the frame callback, which stops
// out of view, in a hidden window and with animations off, saw it whole after
// the first paint; none is born under a settle, so no pass is cut by one.
void SendingHistoryRow::tickGlare(crl::time now) {
	if (_settle) {
		return;
	}
	if (_started
		&& (_glare.birth > _started)
		&& (_glare.birth >= _animation.started())
		&& (now > _glare.death)) {
		_glareSeen = true;
	}
	_glare.tick(now, kSendingRowGlareDuration, kSendingRowGlarePause);
}

// The list decides again, as when a result finds the row in this state.
void SendingHistoryRow::releaseSettleWait() {
	if (auto waiting = base::take(_waiting)) {
		crl::on_main(this, [done = std::move(waiting->done)] {
			if (done) {
				done();
			}
		});
	}
}

bool SendingHistoryRow::surfaceShown() const {
	return _surface && !_surface->isHidden();
}

bool SendingHistoryRow::inView() const {
	return _inView;
}

bool SendingHistoryRow::settling() const {
	return (_settle && !_settle->finished) || _waiting.has_value();
}

bool SendingHistoryRow::settled() const {
	return _settle && _settle->finished;
}

QRectF SendingHistoryRow::diamondTarget(crl::time now) const {
	return surfaceTransform(now).mapRect(diamondCanvas(
		layout(),
		settleTarget(),
		settleProgress(now).elapsed));
}

int SendingHistoryRow::resizeGetHeight(int newWidth) {
	_amount.setAvailableWidth(int(SendingRowAmountRight(newWidth))
		- st::walletRowPadding.left()
		- st::walletSendingRowTextMinWidth);
	_layout = MeasureHistoryRow(_text, newWidth);
	if (_settle) {
		_settle->layout = MeasureHistoryRow(_settle->text, newWidth);
	}
	return rowLayout().height;
}

const HistoryRowLayout &SendingHistoryRow::rowLayout() const {
	return _settle ? _settle->layout : _layout;
}

void SendingHistoryRow::updateRowLayout() {
	if (const auto w = width()) {
		resizeToWidth(w);
	}
}

void SendingHistoryRow::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	const auto inView = (visibleBottom > visibleTop);
	if (_inView == inView) {
		return;
	}
	_inView = inView;
	if (_inView && !_animation.animating()) {
		_surface->update();
	}
}

QString SendingHistoryRow::accessibilityName() {
	const auto &amount = _amount.parts();
	const auto &content = _settle ? _settle->content : _content;
	auto parts = QStringList();
	for (const auto &text : {
		content.title,
		content.subtitle,
		content.date,
		(_content.itemAmount
			? (_settle ? _settle->text : _text).major.toString()
			: _settle
			? (_settle->text.major.toString()
				+ GramMinorPart(content.amountNano))
			: QString(amount.whole + amount.fraction)),
	}) {
		if (!text.isEmpty()) {
			parts.push_back(text);
		}
	}
	return parts.join(u", "_q);
}

SendingRowLayout SendingHistoryRow::layout() const {
	const auto w = width();
	const auto h = height();
	const auto rtl = style::RightToLeft();
	const auto mirror = [&](float64 left, float64 width) {
		return rtl ? (w - left - width) : left;
	};
	const auto &padding = st::walletRowPadding;
	const auto outset = st::walletSendingRowOutset;
	const auto pillLeft = st::walletRowIconLeft - outset;
	const auto pillRight = w - padding.right() + outset;
	auto result = SendingRowLayout();
	result.pill = QRect(
		int(mirror(pillLeft, pillRight - pillLeft)),
		0,
		pillRight - pillLeft,
		h);
	result.radius = PillRadius(result.pill);

	const auto size = st::walletRowIconSize;
	const auto center = rowLayout().avatarCenter;
	result.avatar = QRect(
		int(mirror(st::walletRowIconLeft, size)),
		center - size / 2,
		size,
		size);
	const auto shift = st::walletSendingRowClockShift;
	result.badge = QRectF(result.avatar).center()
		+ QPointF(rtl ? -shift : shift, shift);
	if (_content.itemAmount) {
		result.titleWidth = _layout.titleWidth;
		result.textWidth = _layout.textWidth;
		return result;
	}

	const auto canvas = SendingRowDiamondCanvas();
	const auto drawnLeft = SendingRowDiamondLeft(w)
		+ canvas * kGramDiamondLeft;
	const auto drawnWidth = canvas * (kGramDiamondRight - kGramDiamondLeft);
	result.diamond = QPoint(
		int(base::SafeRound(mirror(drawnLeft, drawnWidth)
			- canvas * kGramDiamondLeft)),
		int(base::SafeRound(h / 2.
			- canvas * (kGramDiamondTop + kGramDiamondBottom) / 2.)));

	const auto amountWidth = _amount.size().width();
	const auto amountLeft = SendingRowAmountRight(w) - amountWidth;
	const auto digitsCenter = (_amount.baseline() - _digitsHeight / 2.)
		* _amount.scale();
	result.amount = QPointF(
		mirror(amountLeft, amountWidth),
		h / 2. - digitsCenter);
	result.textWidth = std::max(
		int(amountLeft) - st::walletRowSkip - padding.left(),
		0);
	result.titleWidth = result.textWidth;
	return result;
}

SendingRowSettleTarget SendingHistoryRow::settleTarget() const {
	auto result = SendingRowSettleTarget();
	if (!_settle) {
		return result;
	}
	const auto place = PlaceRowAmount(
		_settle->text,
		_settle->layout.amountTop,
		width());
	result.major = place.major;
	result.minor = place.minor;
	result.boundary = result.minor.x() + _settle->fraction;
	const auto &font = st::walletRowAmountMinorLabel.style.font;
	result.anchor = QPointF(result.boundary, result.minor.y() + font->ascent);
	const auto emoji = st::emojiSize;
	const auto skip = (emoji - Ui::Text::AdjustCustomEmojiSize(emoji)) / 2;
	const auto &margin = st::walletRowIconMargin;
	const auto size = float64(st::walletRowMarkSize);
	const auto image = QPointF(
		result.boundary + margin.left() + skip,
		result.minor.y() + (font->height - emoji) / 2 + skip + margin.top());
	result.diamond = QRectF(
		image.x() + size * kRowEmojiDiamondLeft,
		image.y() + size * kRowEmojiDiamondTop,
		size * (kRowEmojiDiamondRight - kRowEmojiDiamondLeft),
		size * (kRowEmojiDiamondBottom - kRowEmojiDiamondTop));
	result.textWidth = _settle->layout.textWidth;
	result.titleWidth = _settle->layout.titleWidth;
	return result;
}

crl::time SendingHistoryRow::settleDuration() const {
	return (_settle && _settle->burst)
		? _settle->burst->duration()
		: kSendingRowSettleDiamond;
}

crl::time SendingHistoryRow::settleElapsed(crl::time now) const {
	return !_settle
		? crl::time(0)
		: _settle->finished
		? settleDuration()
		: _settle->started
		? (now - _settle->started)
		: crl::time(0);
}

SendingRowSettleProgress SendingHistoryRow::settleProgress(
		crl::time now) const {
	auto result = SendingRowSettleProgress();
	if (!_settle) {
		return result;
	}
	const auto t = anim::Disabled()
		? kSendingRowSettleDiamond
		: settleElapsed(now);
	const auto out = [](float64 x) {
		return anim::easeOutCubic(1., x);
	};
	result.elapsed = t;
	result.pill = out(SettleRamp(t, 0, kSendingRowSettlePill));
	result.badge = 1. - SettleRamp(
		t,
		kSendingRowSettleBadgeFrom,
		kSendingRowSettleBadgeTill);
	result.label = SettleRamp(
		t,
		kSendingRowSettleLabelFrom,
		kSendingRowSettleLabelTill);
	result.amount = out(SettleRamp(t, 0, kSendingRowSettleAmount));
	result.amountFade = SettleRamp(
		t,
		kSendingRowSettleAmountFadeFrom,
		kSendingRowSettleAmount);
	result.emoji = SettleRamp(
		t,
		kSendingRowSettleDiamondFadeFrom,
		kSendingRowSettleDiamond);
	return result;
}

QRectF SendingHistoryRow::diamondCanvas(
		const SendingRowLayout &layout,
		const SendingRowSettleTarget &target,
		crl::time elapsed) const {
	const auto canvas = float64(SendingRowDiamondCanvas());
	const auto middle = QPointF(
		(kGramDiamondLeft + kGramDiamondRight) / 2.,
		(kGramDiamondTop + kGramDiamondBottom) / 2.);
	auto centre = QPointF(layout.diamond) + middle * canvas;
	auto scale = 1.;
	if (_settle) {
		const auto t = elapsed;
		const auto swell = kSendingRowSettleDiamondSwell;
		centre += (target.diamond.center() - centre) * anim::easeOutCubic(
			1.,
			SettleRamp(t, 0, kSendingRowSettleDiamondMove));
		if (t < kSendingRowSettleDiamondSwellTill) {
			scale = 1. + (swell - 1.) * anim::easeOutCubic(
				1.,
				SettleRamp(t, 0, kSendingRowSettleDiamondSwellTill));
		} else {
			const auto x = SettleRamp(
				t,
				kSendingRowSettleDiamondSwellTill,
				kSendingRowSettleDiamond);
			const auto end = target.diamond.width()
				/ (canvas * (kGramDiamondRight - kGramDiamondLeft));
			scale = swell + (end - swell) * x * x * (3. - 2. * x);
		}
	}
	const auto side = canvas * scale;
	return QRectF(centre - middle * side, QSizeF(side, side));
}

QTransform SendingHistoryRow::surfaceTransform(crl::time now) const {
	auto result = QTransform();
	if (anim::Disabled()) {
		return result;
	}
	const auto pill = QRectF(layout().pill);
	const auto center = pill.center();
	const auto fade = 1. - settleProgress(now).pill;
	const auto elapsed = _started ? (now - _started) : crl::time(0);
	if (elapsed < kSendingRowEntranceDuration) {
		const auto progress = 1. - (1. - SendingRowEntrance(elapsed)) * fade;
		const auto scale = kSendingRowEntranceScale
			+ (1. - kSendingRowEntranceScale) * progress;
		const auto offset = st::walletSendingRowEntranceShift
			* (1. - progress);
		result.translate(center.x(), center.y() + offset);
		result.scale(scale, scale);
		result.translate(-center.x(), -center.y());
	}
	const auto bump = (_bumpAt ? SendingRowBump(now - _bumpAt) : 0.) * fade;
	if (bump != 0.) {
		const auto scale = 1. - kSendingRowBumpShrink * bump;
		const auto drop = (kSendingRowBumpDrop - kSendingRowBumpShrink / 2.)
			* pill.height()
			* bump;
		result.translate(center.x(), center.y() + drop);
		result.scale(scale, scale);
		result.translate(-center.x(), -center.y());
	}
	return result;
}

void SendingHistoryRow::paintSurface() {
	auto p = Painter(_surface.get());
	const auto now = crl::now();
	if (!_started) {
		_started = now;
		_glare.death = now + kSendingRowEntranceDuration;
	}
	if (!_diamondStarted) {
		_diamondStarted = _started;
	}
	if (_settle && !_settle->started && !_settle->finished) {
		_settle->started = now;
		_settle->finish.callOnce(settleDuration());
	}
	startAnimation();
	auto hq = PainterHighQualityEnabler(p);
	const auto layout = this->layout();
	const auto target = settleTarget();
	const auto progress = settleProgress(now);
	p.translate(
		0,
		(st::walletSendingRowEntranceShift
			+ _shadow.extend().top()
			+ surfaceSkip()));
	p.setTransform(surfaceTransform(now), true);
	const auto disabled = anim::Disabled();
	const auto elapsed = now - _started;

	const auto fade = 1. - progress.pill;
	const auto inset = int(base::SafeRound(
		st::walletSendingRowOutset * progress.pill));
	const auto pill = layout.pill.marginsRemoved({ inset, 0, inset, 0 });
	const auto radius = PillRadius(pill);
	const auto accent = st::windowActiveTextFg->c;
	p.setOpacity(fade);
	if (fade > 0.) {
		Dialogs::PaintPillBackground(p, _shadow, pill, radius);
	}
	if (const auto glare = (disabled || fade <= 0.)
			? std::optional<float64>()
			: _glare.progress(now)) {
		const auto stroke = float64(st::walletSendingRowGlareStroke);
		const auto half = stroke / 2.;
		const auto pillWidth = float64(pill.width());
		const auto pillHeight = float64(pill.height());
		const auto slope = kSendingRowGlareLag * pillWidth / pillHeight;
		PaintGlare(
			p,
			QRectF(pill).marginsAdded({ half, half, half, half }),
			radius + half,
			ComputeGlareBand(
				*glare,
				pillWidth + slope * pillHeight,
				st::walletSendingRowGlareWidth),
			{
				.stroke = stroke,
				.slope = slope,
				.border = kSendingRowGlareBorder,
				.background = kSendingRowGlareBackground,
			},
			accent);
	}
	p.setOpacity(1.);
	paintAvatar(
		p,
		layout,
		(st::walletSendingRowClockSize / 2. + st::walletSendingRowClockCutout)
			* progress.badge,
		progress.label);
	if (progress.badge > 0.) {
		p.setOpacity(progress.badge);
		PaintClock(
			p,
			SendingRowClockStyle(),
			layout.badge,
			SendingClockPose(disabled ? 0 : elapsed),
			accent);
		p.setOpacity(1.);
	}
	paintTexts(p, layout, target, progress);
	if (_content.itemAmount) {
		paintItemAmount(p, progress, now);
		paintChip(p);
		return;
	}
	if (_settle) {
		paintSettleAmount(p, layout, target, progress, now);
	} else {
		_amount.paint(p, layout.amount, { .digits = st::windowBoldFg->c });
	}
	if (const auto burst = (_settle && !disabled)
			? _settle->burst.get()
			: nullptr) {
		const auto canvas = SendingRowDiamondCanvas();
		burst->paint(p, {
			.origin = target.diamond.center(),
			.emitter = canvas * (kGramDiamondRight - kGramDiamondLeft),
			.extent = float64(width()),
			.elapsed = progress.elapsed,
		});
	}
	if (progress.emoji < 1.) {
		p.setOpacity(1. - progress.emoji);
		paintDiamond(
			p,
			diamondCanvas(layout, target, progress.elapsed),
			now);
		p.setOpacity(1.);
	}
}

void SendingHistoryRow::paintAvatar(
		Painter &p,
		const SendingRowLayout &layout,
		float64 cutout,
		float64 swap) {
	p.save();
	if (cutout > 0.) {
		auto clip = QPainterPath();
		clip.addRect(QRectF(layout.avatar));
		auto cut = QPainterPath();
		cut.addEllipse(layout.badge, cutout, cutout);
		p.setClipPath(clip.subtracted(cut));
	}
	const auto paint = [&](
			const HistoryRowContent &content,
			Ui::PeerUserpicView *userpic) {
		if (content.peer && userpic) {
			content.peer->paintUserpic(
				p,
				*userpic,
				layout.avatar.x(),
				layout.avatar.y(),
				layout.avatar.width());
		} else {
			PaintRowAvatar(p, layout.avatar, content.avatar);
		}
	};
	const auto next = _settle ? &_settle->content : nullptr;
	const auto same = !next
		|| (next->peer
			? (next->peer == _content.peer)
			: (!_content.peer && next->avatar == _content.avatar));
	if (same) {
		paint(_content, _userpic.get());
	} else {
		if (swap < 1.) {
			paint(_content, _userpic.get());
		}
		if (swap > 0.) {
			p.setOpacity(swap);
			paint(*next, _settle->userpic.get());
		}
	}
	p.restore();
}

void SendingHistoryRow::paintTexts(
		Painter &p,
		const SendingRowLayout &layout,
		const SendingRowSettleTarget &target,
		const SendingRowSettleProgress &progress) {
	const auto left = st::walletRowPadding.left();
	const auto draw = [&](
			const Ui::Text::String &text,
			const style::FlatLabel &st,
			HistoryRowLine line,
			bool breakEverywhere,
			int available,
			float64 opacity) {
		if (opacity <= 0. || !line.lines) {
			return;
		}
		p.setOpacity(opacity);
		p.setPen(st.textFg);
		text.draw(p, {
			.position = { left, line.top },
			.outerWidth = width(),
			.availableWidth = available,
			.elisionLines = line.lines,
			.elisionBreakEverywhere = breakEverywhere,
		});
	};
	const auto settle = _settle.get();
	const auto line = [&](
			const Ui::Text::String &text,
			HistoryRowLine textLine,
			const Ui::Text::String *next,
			HistoryRowLine nextLine,
			bool same,
			const style::FlatLabel &st,
			bool breakEverywhere,
			int available) {
		if (!next) {
			draw(text, st, textLine, breakEverywhere, available, 1.);
		} else if (same) {
			draw(*next, st, nextLine, breakEverywhere, available, 1.);
		} else {
			draw(
				text,
				st,
				textLine,
				breakEverywhere,
				available,
				1. - progress.label);
			draw(
				*next,
				st,
				nextLine,
				breakEverywhere,
				available,
				progress.label);
		}
	};
	const auto titleWidth = settle
		? int(base::SafeRound(layout.titleWidth
			+ (target.titleWidth - layout.titleWidth) * progress.amount))
		: layout.titleWidth;
	const auto textWidth = settle
		? int(base::SafeRound(layout.textWidth
			+ (target.textWidth - layout.textWidth) * progress.amount))
		: layout.textWidth;
	line(
		_text.title,
		_layout.title,
		settle ? &settle->text.title : nullptr,
		settle ? settle->layout.title : HistoryRowLine(),
		settle && (settle->content.title == _content.title),
		st::walletRowTitleLabel,
		true,
		titleWidth);
	line(
		_text.subtitle,
		_layout.subtitle,
		settle ? &settle->text.subtitle : nullptr,
		settle ? settle->layout.subtitle : HistoryRowLine(),
		settle && (settle->content.subtitle == _content.subtitle),
		st::walletRowSubtitleLabel,
		true,
		textWidth);
	line(
		_text.date,
		_layout.date,
		settle ? &settle->text.date : nullptr,
		settle ? settle->layout.date : HistoryRowLine(),
		settle && (settle->content.date == _content.date),
		st::walletRowDateLabel,
		false,
		textWidth);
	p.setOpacity(1.);
}

void SendingHistoryRow::paintSettleAmount(
		Painter &p,
		const SendingRowLayout &layout,
		const SendingRowSettleTarget &target,
		const SendingRowSettleProgress &progress,
		crl::time now) {
	const auto &content = _settle->content;
	const auto &color = RowAmountColor(
		content.incoming,
		content.pending,
		content.failed);
	if (progress.amountFade < 1.) {
		const auto from = _amount.scale();
		const auto natural = QPointF(
			_amount.naturalWidth(),
			_amount.baseline());
		const auto start = layout.amount + natural * from;
		const auto anchor = start + (target.anchor - start) * progress.amount;
		const auto scale = from + (_settle->scale - from) * progress.amount;
		p.save();
		p.setOpacity(1. - progress.amountFade);
		p.translate(anchor);
		p.scale(scale, scale);
		p.translate(-natural);
		_amount.paint(p, {
			.digits = anim::color(st::windowBoldFg, color, progress.amount),
		});
		p.restore();
	}
	const auto draw = [&](
			const Ui::Text::String &text,
			QPoint position,
			const style::FlatLabel &st,
			QRect clip,
			float64 opacity) {
		if (opacity <= 0.) {
			return;
		}
		p.save();
		p.setOpacity(opacity);
		p.setClipRect(clip, Qt::IntersectClip);
		p.setPen(color);
		text.draw(p, {
			.position = position,
			.availableWidth = text.maxWidth(),
			.palette = &st.palette,
			.now = now,
		});
		p.restore();
	};
	const auto h = height();
	draw(
		_settle->text.major,
		target.major,
		st::walletRowAmountMajorLabel,
		rect(),
		progress.amountFade);
	draw(
		_settle->text.minor,
		target.minor,
		st::walletRowAmountMinorLabel,
		QRect(0, 0, target.boundary, h),
		progress.amountFade);
	draw(
		_settle->text.minor,
		target.minor,
		st::walletRowAmountMinorLabel,
		QRect(target.boundary, 0, width() - target.boundary, h),
		progress.emoji);
}

void SendingHistoryRow::paintItemAmount(
		Painter &p,
		const SendingRowSettleProgress &progress,
		crl::time now) {
	const auto &text = _settle ? _settle->text : _text;
	const auto &next = _settle ? _settle->content : _content;
	const auto place = PlaceRowAmount(text, rowLayout().amountTop, width());
	p.setPen(anim::color(
		RowAmountColor(_content.incoming, false, _content.failed),
		RowAmountColor(next.incoming, false, next.failed),
		progress.label));
	const auto draw = [&](
			const Ui::Text::String &string,
			QPoint position,
			const style::FlatLabel &st) {
		string.draw(p, {
			.position = position,
			.availableWidth = string.maxWidth(),
			.palette = &st.palette,
			.now = now,
		});
	};
	draw(text.major, place.major, st::walletRowAmountMajorLabel);
	draw(text.minor, place.minor, st::walletRowAmountMinorLabel);
}

void SendingHistoryRow::paintChip(Painter &p) {
	const auto &padding = st::walletRowPadding;
	const auto outer = std::max(width() - padding.left() - padding.right(), 0);
	const auto origin = QPoint(
		style::RightToLeft() ? padding.right() : padding.left(),
		rowLayout().chipTop);
	p.translate(origin);
	PaintHistoryRowChipPlate(p, outer, _chip);
	PaintHistoryRowChipArtwork(p, outer, _chip, _media, _content.collectible);
	PaintHistoryRowChipText(p, outer, _chip);
	p.translate(-origin);
}

void SendingHistoryRow::paintDiamond(
		QPainter &p,
		QRectF canvas,
		crl::time now) {
	if (_diamondAway || !_diamond || !_diamond->valid()) {
		return;
	}
	AdvanceSendingDiamond(_diamond.get(), _diamondStarted, now);
	const auto size = SendingRowDiamondCanvas();
	const auto fade = 1. - settleProgress(now).pill;
	const auto bump = (_bumpAt && !anim::Disabled())
		? (SendingRowBump(now - _bumpAt) * fade)
		: 0.;
	const auto swell = 1. + kSendingRowBumpSwell * std::max(bump, 0.);
	const auto centre = canvas.topLeft() + QPointF(
		canvas.width() * (kGramDiamondLeft + kGramDiamondRight) / 2.,
		canvas.height() * (kGramDiamondTop + kGramDiamondBottom) / 2.);
	const auto target = QRectF(
		centre + (canvas.topLeft() - centre) * swell,
		canvas.size() * swell);
	p.drawImage(target, _diamond->frame(QSize(size, size), nullptr).image);
}

void SendingHistoryRow::startAnimation() {
	if (anim::Disabled() || _animation.animating()) {
		return;
	}
	_animation.start();
}

not_null<SendingHistoryRow*> AddSendingHistoryRow(
		not_null<Ui::VerticalLayout*> slot,
		not_null<Ui::RpWidget*> layer,
		not_null<Ui::RpWidget*> bounds,
		const HistoryRowContent &content,
		std::shared_ptr<CollectibleMedia> media,
		Fn<void()> clicked) {
	const auto result = slot->add(object_ptr<SendingHistoryRow>(
		slot,
		layer,
		bounds,
		content,
		std::move(media)));
	result->setClickedCallback(std::move(clicked));
	return result;
}

[[nodiscard]] bool ShowsCollectible(const TransferItem &item) {
	return (item.kind == TransferItem::Kind::Collectible)
		&& !item.collectible.isEmpty();
}

[[nodiscard]] QString RowStatusSubtitle(TransferItem::Status status) {
	using Status = TransferItem::Status;
	switch (status) {
	case Status::Pending:
		return tr::lng_channel_earn_history_pending(tr::now);
	case Status::Failure:
		return tr::lng_channel_earn_history_failed(tr::now);
	case Status::Success:
		return QString();
	}
	Unexpected("Status in RowStatusSubtitle.");
}

} // namespace ContentDetails

} // namespace Wallet
