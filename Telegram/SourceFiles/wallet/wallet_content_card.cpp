#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

[[nodiscard]] BalancePalette CardBalancePalette() {
	return {
		.mark = st::activeButtonFg->c,
		.amount = st::activeButtonFg->c,
		.secondary = st::activeButtonFg->c,
	};
}

[[nodiscard]] BalancePalette SettledBalancePalette() {
	return {
		.mark = st::windowActiveTextFg->c,
		.amount = st::windowBoldFg->c,
		.secondary = st::windowSubTextFg->c,
	};
}

[[nodiscard]] Wallet::AmountStyle MoneyAmountStyle() {
	return {
		.big = st::walletCardBalanceMajorLabel.style.font,
		.small = st::walletCardBalanceMinorLabel.style.font,
		.additionWidth = st::walletCardMarkSize,
		.additionSkip = st::walletCardIconMargin.right(),
		.tickerSkip = st::walletCardTickerSkip,
	};
}

[[nodiscard]] float64 BalanceAmountScale(float64 progress) {
	const auto settled = st::walletBalanceHeaderMajorFont->height
		/ float64(st::walletCardBalanceMajorLabel.style.font->height);
	return 1. + (settled - 1.) * progress;
}

[[nodiscard]] float64 BalanceFiatScale(float64 progress) {
	const auto settled = st::walletBalanceHeaderFiatFont->height
		/ float64(st::walletCardFiatLabel.style.font->height);
	return 1. + (settled - 1.) * progress;
}

[[nodiscard]] float64 BalanceSettledTop() {
	const auto block = st::walletBalanceHeaderMajorFont->height
		+ st::walletBalanceHeaderLineSkip
		+ st::walletBalanceHeaderFiatFont->height;
	return (st::separatePanelTitleHeight - block) / 2.
		- st::separatePanelTitleHeight;
}

// The balance rows ride the folding card: each anchor is a card-local
// rest offset placed in Content coordinates against the card's rest rect
// and mapped through the fold transform, and it then travels to its
// landed position in the title band. The x weight is the square of the
// fold progress, so the row leaves the folded quad through its top edge
// instead of sliding out through the narrowing left corner.
[[nodiscard]] QPointF BalanceRowPosition(
		const CardFold &fold,
		int restTop,
		float64 landedTop) {
	const auto anchor = fold.transform.map(QPointF(
		fold.rest.x() + st::walletCardContentLeft,
		fold.rest.y() + restTop));
	const auto landedLeft = float64(st::separatePanelTitleLeft);
	return QPointF(
		anchor.x() + (landedLeft - anchor.x()) * fold.fold * fold.fold,
		anchor.y() + (landedTop - anchor.y()) * fold.fold);
}

[[nodiscard]] QRectF MarkInkBounds(const QImage &image) {
	auto left = image.width();
	auto top = image.height();
	auto right = -1;
	auto bottom = -1;
	for (auto y = 0; y != image.height(); ++y) {
		for (auto x = 0; x != image.width(); ++x) {
			if (qAlpha(image.pixel(x, y))) {
				left = std::min(left, x);
				top = std::min(top, y);
				right = std::max(right, x);
				bottom = std::max(bottom, y);
			}
		}
	}
	if (right < 0) {
		return QRectF();
	}
	const auto ratio = image.devicePixelRatio();
	return QRectF(
		left / ratio,
		top / ratio,
		(right - left + 1) / ratio,
		(bottom - top + 1) / ratio);
}

BalanceInk::BalanceInk() {
	const auto &font = st::walletCardBalanceMajorLabel.style.font;
	const auto canvas = GramDiamondCanvas(font);
	_markFrame = QRectF(
		-int(base::SafeRound(canvas * kGramDiamondLeft)),
		font->ascent - int(base::SafeRound(canvas * kGramDiamondBottom)),
		canvas,
		canvas);
	_markLottieVisible = QRectF(
		_markFrame.x() + canvas * kGramDiamondLeft,
		_markFrame.y() + canvas * kGramDiamondTop,
		canvas * (kGramDiamondRight - kGramDiamondLeft),
		canvas * (kGramDiamondBottom - kGramDiamondTop));
	_markLottie = Lottie::MakeIcon({
		.name = u"gram_white"_q,
		.sizeOverride = { canvas, canvas },
	});
}

void BalanceInk::setContent(
		CreditsAmount amount,
		const QString &fiat,
		BalanceStyle style) {
	_balance = amount;
	_fiatText = fiat;
	_style = style;
	refresh();
}

void BalanceInk::setOuterWidth(int outerWidth) {
	if (_outerWidth == outerWidth) {
		return;
	}
	_outerWidth = outerWidth;
	refresh();
}

void BalanceInk::playMark(Fn<void()> repaint) {
	if (!_markLottie->valid()) {
		return;
	}
	_markRepaint = repaint;
	_markLottie->animate(
		std::move(repaint),
		0,
		_markLottie->framesCount() - 1);
}

void BalanceInk::setMarkTint(Fn<CardMarkTint()> tint) {
	_markTint = std::move(tint);
}

void BalanceInk::refresh() {
	const auto &fiatFont = st::walletCardFiatLabel.style.font;
	const auto exact = (_style != BalanceStyle::Balance);
	const auto amountNano = _balance.whole() * Ui::kNanosInOne
		+ _balance.nano();
	const auto precise = exact
		? Ui::FormatTonAmount(amountNano)
		: Ui::FormattedTonAmount();
	const auto minor = exact
		? (precise.nanoString.isEmpty()
			? QString()
			: (precise.separator + precise.nanoString))
		: _balance.nano()
		? GramMinorPart(amountNano)
		: QString();
	const auto ticker = GramTicker();
	const auto cardWidth = _outerWidth
		- st::walletCardMargin.left()
		- st::walletCardMargin.right();
	const auto qrLeft = CardQrRect(cardWidth).x();
	const auto sign = (_style == BalanceStyle::Minus)
		? QString(kMinus)
		: (_style == BalanceStyle::Plus)
		? u"+"_q
		: QString();
	const auto full = exact
		? (sign + precise.wholeString)
		: GramMajorPart(amountNano);
	_painter.setContent(MoneyAmountStyle(), {
		.whole = full,
		.fraction = minor,
		.ticker = ticker,
	});
	_painter.setAvailableWidth(_outerWidth
		? (qrLeft - st::walletCardContentSkip - st::walletCardContentLeft)
		: 0);

	_fiat = QPainterPath();
	_fiat.addText(0, fiatFont->ascent, fiatFont, _fiatText);
	_fiatWidth = fiatFont->width(_fiatText);

	_markCard = Ui::Earn::IconCurrencyMono(
		st::walletCardMarkSize,
		CardBalancePalette().mark);
	_markSettled = Ui::Earn::IconCurrencyTwoTone(
		st::walletCardMarkSize,
		SettledBalancePalette().mark);
	_markTop = Ui::Earn::AlignedMarkTop(
		st::walletCardBalanceMajorLabel.style.font,
		_markCard);
	_markMonoVisible = MarkInkBounds(_markCard).translated(0., _markTop);
}

QRectF BalanceInk::amountRect(const CardFold &fold) const {
	const auto scale = BalanceAmountScale(fold.fold);
	const auto width = _painter.size().width() * scale;
	const auto height = st::walletCardBalanceMajorLabel.style.font->height
		* scale;
	const auto position = BalanceRowPosition(
		fold,
		st::walletCardBalanceTop,
		BalanceSettledTop());
	return QRectF(position.x(), position.y(), width, height);
}

QRectF BalanceInk::fiatRect(const CardFold &fold) const {
	const auto scale = BalanceFiatScale(fold.fold);
	const auto width = _fiatWidth * scale;
	const auto height = st::walletCardFiatLabel.style.font->height * scale;
	const auto position = BalanceRowPosition(
		fold,
		st::walletCardFiatTop,
		BalanceSettledTop()
			+ st::walletBalanceHeaderMajorFont->height
			+ st::walletBalanceHeaderLineSkip);
	return QRectF(position.x(), position.y(), width, height);
}

QRectF BalanceInk::markVisible(float64 fold) const {
	const auto &from = _markLottieVisible;
	const auto &to = _markMonoVisible;
	return QRectF(
		from.x() + (to.x() - from.x()) * fold,
		from.y() + (to.y() - from.y()) * fold,
		from.width() + (to.width() - from.width()) * fold,
		from.height() + (to.height() - from.height()) * fold);
}

QRectF BalanceInk::markDrawRect(
		float64 fold,
		const QRectF &box,
		const QRectF &visible) const {
	const auto target = markVisible(fold);
	const auto k = target.height() / visible.height();
	return QRectF(
		target.center().x() - (visible.center().x() - box.x()) * k,
		target.y() - (visible.y() - box.y()) * k,
		box.width() * k,
		box.height() * k);
}

void BalanceInk::paintMark(
		QPainter &p,
		float64 fold,
		const QImage &mono,
		bool card) const {
	const auto monoBox = QRectF(
		0.,
		_markTop,
		st::walletCardMarkSize,
		st::walletCardMarkSize);
	if (_markLottieVisible.height() <= 0.
		|| _markMonoVisible.height() <= 0.) {
		p.drawImage(monoBox, mono);
		return;
	}
	// WHY: both passes map their own diamond onto one shared box, so the
	// diamond crossing the folding card's edge stays one shape, lottie
	// inside and mono outside, with no step at the seam.
	if (card && _markLottie->valid()) {
		if (!_markLottie->animating() && _markLottie->frameIndex() != 0) {
			_markLottie->jumpTo(0, _markRepaint);
		}
		const auto frame = _markLottie->frame();
		p.drawImage(
			markDrawRect(fold, _markFrame, _markLottieVisible),
			_markTint ? _markTinter.apply(frame, _markTint()) : frame);
	} else {
		p.drawImage(markDrawRect(fold, monoBox, _markMonoVisible), mono);
	}
}

void BalanceInk::paintPass(
		QPainter &p,
		const CardFold &fold,
		const BalancePalette &palette,
		const QImage &mark,
		bool card,
		float64 secondaryOpacity,
		BalanceInkLayer layer) const {
	const auto primary = (layer != BalanceInkLayer::Secondary);
	const auto secondary = (layer != BalanceInkLayer::Primary);
	p.save();
	p.setTransform(groupTransform(fold), true);
	if (primary) {
		paintMark(p, fold.fold, mark, card);
	}
	_painter.paint(p, {
		.digits = primary ? palette.amount : QColor(Qt::transparent),
		.ticker = palette.secondary,
		.tickerOpacity = secondary ? secondaryOpacity : 0.,
	});
	p.restore();
	if (!secondary) {
		return;
	}

	const auto fiat = fiatRect(fold);
	const auto fiatScale = BalanceFiatScale(fold.fold);
	p.save();
	p.setOpacity(p.opacity() * secondaryOpacity);
	p.translate(fiat.x(), fiat.y());
	p.scale(fiatScale, fiatScale);
	p.fillPath(_fiat, palette.secondary);
	p.restore();
}

QTransform BalanceInk::groupTransform(const CardFold &fold) const {
	const auto amount = amountRect(fold);
	const auto foldScale = BalanceAmountScale(fold.fold);
	const auto fit = _painter.scale();
	auto result = QTransform();
	result.translate(amount.x(), amount.y());
	result.scale(foldScale, foldScale);
	result.translate(0., (1. - fit) * _painter.naturalHeight() / 2.);
	result.scale(fit, fit);
	return result;
}

void BalanceInk::paint(
		QPainter &p,
		const CardFold &fold,
		const QRegion &cardOutline,
		QRect clip,
		BalanceInkPart part,
		BalanceInkLayer layer) const {
	const auto ink = boundingRect(fold);
	const auto inside = cardOutline.intersected(QRegion(clip));
	if (part != BalanceInkPart::Outside && inside.intersects(ink)) {
		const auto secondaryOpacity = (layer == BalanceInkLayer::Secondary)
			? 1.
			: st::walletCardSecondaryOpacity;
		p.save();
		p.setClipRegion(inside, Qt::IntersectClip);
		paintPass(
			p,
			fold,
			CardBalancePalette(),
			_markCard,
			true,
			secondaryOpacity,
			layer);
		p.restore();
	}
	const auto outside = QRegion(clip) - inside;
	if (part != BalanceInkPart::Card && outside.intersects(ink)) {
		p.save();
		p.setClipRegion(outside, Qt::IntersectClip);
		paintPass(
			p,
			fold,
			SettledBalancePalette(),
			_markSettled,
			false,
			1.,
			layer);
		p.restore();
	}
}

QRect BalanceInk::boundingRect(const CardFold &fold) const {
	const auto amount = amountRect(fold);
	const auto fiat = fiatRect(fold);
	return amount.united(fiat).toAlignedRect();
}

QRect BalanceInk::markRect(QRect cardRest) const {
	const auto fit = _painter.scale();
	const auto origin = QPointF(
		cardRest.x() + st::walletCardContentLeft,
		cardRest.y()
			+ st::walletCardBalanceTop
			+ (1. - fit) * _painter.naturalHeight() / 2.);
	const auto top = int(base::SafeRound(_markTop));
	return QRectF(
		origin + QPointF(0., top * fit),
		QSizeF(st::walletCardMarkSize, st::walletCardMarkSize) * fit
	).toAlignedRect();
}

QRect BalanceInk::markPaintRect(const CardFold &fold) const {
	if (_markLottieVisible.height() <= 0.) {
		return QRect();
	}
	return groupTransform(fold).mapRect(
		markDrawRect(fold.fold, _markFrame, _markLottieVisible)
	).toAlignedRect().marginsAdded({ 1, 1, 1, 1 });
}

[[nodiscard]] rpl::producer<TextWithEntities> CardNameValue(
		not_null<Main::Session*> session) {
	return Info::Profile::NameValue(
		session->user()
	) | rpl::map([](QString name) {
		return tr::marked(std::move(name));
	});
}

[[nodiscard]] rpl::producer<QString> CardAddressValue(
		not_null<Main::Session*> session) {
	return session->wallet().presenceValue(
	) | rpl::map([=](Presence) {
		return session->wallet().addressFriendly(false);
	});
}

void SetupCardBalance(
		not_null<BalanceInk*> ink,
		not_null<Main::Session*> session,
		Fn<void()> repaint,
		not_null<Ui::RpWidget*> owner,
		rpl::producer<int64> balanceNano,
		rpl::producer<QString> fiat) {
	const auto apply = [=](int64 nano, const QString &text) {
		const auto scope = WindowPaletteScope(owner);
		ink->setContent(
			CreditsAmount(
				nano / Ui::kNanosInOne,
				nano % Ui::kNanosInOne,
				CreditsType::Ton),
			text);
		repaint();
	};
	auto amount = balanceNano
		? std::move(balanceNano)
		: session->wallet().balanceNanoValue();
	if (fiat) {
		rpl::combine(
			std::move(amount),
			std::move(fiat)
		) | rpl::on_next(apply, owner->lifetime());
		return;
	}
	rpl::combine(
		std::move(amount),
		FiatRateValue(session)
	) | rpl::on_next([=](int64 nano, FiatRate rate) {
		apply(nano, FormatFiat(nano, rate));
	}, owner->lifetime());
}

void SetupCardMark(
		not_null<BalanceInk*> ink,
		not_null<Ui::RpWidget*> owner,
		std::shared_ptr<bool> played,
		Fn<void()> repaint) {
	// WHY: a box's layer is shown and hidden again synchronously inside its
	// show animation, so the card re-reads the window's activation on every
	// show and starts the play only once it stays visible.
	const auto open = owner->lifetime().make_state<bool>(false);
	owner->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return (e->type() == QEvent::Show);
	}) | rpl::map([=] {
		return rpl::combine(
			owner->windowActiveValue(),
			PowerSaving::OnValue(PowerSaving::kStickersChat),
			anim::Disables());
	}) | rpl::flatten_latest(
	) | rpl::on_next([=](bool active, bool saving, bool off) {
		*open = active && !saving && !off;
		if (*open && !*played) {
			InvokeQueued(owner, [=] {
				if (*open && !*played && owner->isVisible()) {
					*played = true;
					ink->playMark(repaint);
				}
			});
		}
	}, owner->lifetime());
}

Card::Card(
	QWidget *parent,
	std::shared_ptr<Main::SessionShow> show,
	rpl::producer<TextWithEntities> name,
	rpl::producer<QString> address)
: RpWidget(parent)
, _show(std::move(show))
, _nameStyle(st::defaultTextStyle)
, _look(crl::guard(this, [=] { invalidateCache(); })) {
	_nameStyle.font = st::walletCardNameFont->monospace();
	auto friendly = address
		? std::move(address)
		: CardAddressValue(&_show->session());
	std::move(friendly) | rpl::on_next([=](const QString &value) {
		setAddress(value);
	}, lifetime());

	std::move(name) | rpl::on_next([=](TextWithEntities name) {
		_name.setMarkedText(
			_nameStyle,
			tr::upper(std::move(name)),
			kMarkupTextOptions,
			Core::TextContext({
				.session = &_show->session(),
				.repaint = crl::guard(this, [=] { invalidateCache(); }),
		}));
		invalidateCache();
	}, lifetime());
}

void Card::setFold(const CardFold &fold) {
	_fold = fold;
	setVisible(fold.valid && fold.opacity > 0.);
	update();
}

const CardFold &Card::fold() const {
	return _fold;
}

void Card::invalidateCache() {
	_cache = QImage();
	if (const auto owner = _paintOwner.data()) {
		owner->update();
	} else {
		update();
	}
	_invalidations.fire({});
}

rpl::producer<> Card::invalidations() const {
	return _invalidations.events();
}

void Card::liftForeground() {
	_lifted = true;
	invalidateCache();
}

void Card::followCursor() {
	if (!_angle) {
		_angle = std::make_unique<CardAngle>();
	}
}

void Card::setLook(CardLook look, CardLookChange change) {
	_look.setLook(std::move(look), change);
}

rpl::producer<bool> Card::lookTransitionValue() const {
	return _look.runningValue();
}

CardMarkTint Card::markTint(const BalanceInk &ink) const {
	const auto card = QRect(QPoint(), restRect().size());
	return _look.markTint(card, ink.markRect(card));
}

float64 Card::paintAngle(not_null<QWidget*> paintOwner, QRectF rect) {
	if (!_angle) {
		return 0.;
	}
	_angle->track(this, paintOwner, rect);
	return _angle->value(crl::now());
}

QRect Card::restRect() const {
	return QRect(
		0,
		height() - st::walletCardHeight,
		width(),
		st::walletCardHeight);
}

void Card::setAddress(const QString &address) {
	_addressLine1 = _addressLine2 = QString();
	if (address.size() == kAddressLength) {
		_addressLine1 = GroupedAddressLine(address, 0);
		_addressLine2 = GroupedAddressLine(address, kAddressLength / 2);
	}
	invalidateCache();
}

void Card::validateCache(float64 angle) {
	const auto ratio = style::DevicePixelRatio();
	const auto size = restRect().size() * ratio;
	// WHY: the cached fold image carries the sweep, so it is keyed by the
	// angle it was painted at.
	if (!_cache.isNull() && _cache.size() == size && _cacheAngle == angle) {
		return;
	}
	_cacheAngle = angle;
	_cache = QImage(size, QImage::Format_ARGB32_Premultiplied);
	_cache.setDevicePixelRatio(ratio);
	_cache.fill(Qt::transparent);
	auto q = Painter(&_cache);
	auto hq = PainterHighQualityEnabler(q);
	paintContent(q, angle);
}

void Card::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.translate(-x(), -y());
	paintFold(p, _fold, this);
}

void Card::paintFold(
		Painter &p,
		const CardFold &fold,
		not_null<QWidget*> paintOwner) {
	if (!fold.valid || fold.opacity <= 0.) {
		return;
	}
	_paintOwner = paintOwner.get();
	const auto bounds = fold.quad.boundingRect();
	const auto angle = paintAngle(paintOwner, p.transform().mapRect(bounds));
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(p.opacity() * fold.opacity);
	if (fold.transform.type() <= QTransform::TxTranslate) {
		p.translate(bounds.topLeft());
		paintContent(p, angle);
	} else {
		validateCache(angle);
		p.setTransform(fold.transform, true);
		p.drawImage(QRectF(fold.rest), _cache);
	}
	p.restore();
}

void Card::paintForeground(Painter &p, const CardFold &fold) {
	if (!_lifted || !fold.valid || fold.opacity <= 0.) {
		return;
	}
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(p.opacity() * fold.opacity);
	p.setTransform(fold.transform, true);
	p.translate(fold.rest.topLeft());
	paintFace(p);
	p.restore();
}

void Card::paintContent(Painter &p, float64 angle) {
	_look.paint(p, QRect(QPoint(), restRect().size()), angle);
	if (!_lifted) {
		paintFace(p);
	}
}

void Card::paintFace(Painter &p) {
	const auto size = restRect().size();
	const auto qr = CardQrRect(size.width());
	const auto layout = ComputeCardTextLayout(size, _nameStyle.font);
	const auto foreground = [&](Painter &q, QColor ink) {
		PaintCardQrPlate(q, qr);
		st::walletCardQrIcon.paintInCenter(q, qr, CardQrIconFg());

		q.setPen(st::activeButtonFg);
		_name.drawElided(q, layout.nameLeft, layout.nameTop, layout.nameMax);

		if (!_addressLine1.isEmpty()) {
			q.setPen(ink);
			q.setFont(layout.addressFont);
			q.save();
			q.translate(layout.addressBaseline, st::walletCardAddressSkip);
			q.rotate(90);
			q.drawText(0, 0, _addressLine1);
			q.drawText(0, -layout.addressFont->height, _addressLine2);
			q.restore();
		}
	};
	_look.paintForeground(p, QRect(QPoint(), size), foreground);
}

} // namespace ContentDetails

} // namespace Wallet
