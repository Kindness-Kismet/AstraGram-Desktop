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
		p.drawImage(
			markDrawRect(fold, _markFrame, _markLottieVisible),
			_markLottie->frame());
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
		float64 secondaryOpacity) const {
	p.save();
	p.setTransform(groupTransform(fold), true);
	paintMark(p, fold.fold, mark, card);
	_painter.paint(p, {
		.digits = palette.amount,
		.ticker = palette.secondary,
		.tickerOpacity = secondaryOpacity,
	});
	p.restore();

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
		QRect clip) const {
	const auto ink = boundingRect(fold);
	const auto inside = cardOutline.intersected(QRegion(clip));
	if (inside.intersects(ink)) {
		p.save();
		p.setClipRegion(inside, Qt::IntersectClip);
		paintPass(
			p,
			fold,
			CardBalancePalette(),
			_markCard,
			true,
			st::walletCardSecondaryOpacity);
		p.restore();
	}
	const auto outside = QRegion(clip) - inside;
	if (outside.intersects(ink)) {
		p.save();
		p.setClipRegion(outside, Qt::IntersectClip);
		paintPass(
			p,
			fold,
			SettledBalancePalette(),
			_markSettled,
			false,
			1.);
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

void SetupCardBalance(
		not_null<BalanceInk*> ink,
		not_null<Main::Session*> session,
		Fn<void()> repaint,
		not_null<Ui::RpWidget*> owner) {
	rpl::combine(
		session->wallet().balanceNanoValue(),
		FiatRateValue(session)
	) | rpl::on_next([=](int64 nano, FiatRate rate) {
		const auto scope = WindowPaletteScope(owner);
		ink->setContent(
			CreditsAmount(
				nano / Ui::kNanosInOne,
				nano % Ui::kNanosInOne,
				CreditsType::Ton),
			FormatFiat(nano, rate));
		repaint();
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
	rpl::producer<TextWithEntities> name)
: RpWidget(parent)
, _show(std::move(show))
, _nameStyle(st::defaultTextStyle) {
	_nameStyle.font = st::walletCardNameFont->monospace();
	_show->session().wallet().presenceValue(
	) | rpl::on_next([=](Presence) {
		refreshAddress();
	}, lifetime());

	std::move(name) | rpl::on_next([=](TextWithEntities name) {
		_name.setMarkedText(
			_nameStyle,
			tr::upper(std::move(name)),
			kMarkupTextOptions,
			Core::TextContext({
				.session = &_show->session(),
				.repaint = crl::guard(this, [=] { update(); }),
		}));
		invalidateCache();
		update();
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

QPolygonF Card::paintedQuad() const {
	return _fold.quad;
}

QPolygonF Card::paintedOutline() const {
	if (!_fold.valid) {
		return QPolygonF();
	}
	auto path = QPainterPath();
	path.addRoundedRect(
		QRectF(_fold.rest),
		st::walletCardRadius,
		st::walletCardRadius);
	return _fold.transform.map(path.toFillPolygon());
}

void Card::invalidateCache() {
	_cache = QImage();
}

void Card::followCursor() {
	if (!_angle) {
		_angle = std::make_unique<CardAngle>();
	}
}

QRectF Card::paintedRect() const {
	return _fold.quad.boundingRect().translated(-QPointF(pos()));
}

float64 Card::paintAngle() {
	if (!_angle) {
		return 0.;
	}
	_angle->track(this, this, paintedRect());
	return _angle->value(crl::now());
}

QRect Card::restRect() const {
	return QRect(
		0,
		height() - st::walletCardHeight,
		width(),
		st::walletCardHeight);
}

void Card::refreshAddress() {
	auto &wallet = _show->session().wallet();
	const auto address = wallet.addressFriendly(false);
	_addressLine1 = _addressLine2 = QString();
	if (address.size() == kAddressLength) {
		_addressLine1 = GroupedAddressLine(address, 0);
		_addressLine2 = GroupedAddressLine(address, kAddressLength / 2);
	}
	invalidateCache();
	update();
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
	if (!_fold.valid || _fold.opacity <= 0.) {
		return;
	}
	const auto angle = paintAngle();
	auto p = Painter(this);
	if (!_fold.fold) {
		auto hq = PainterHighQualityEnabler(p);
		p.translate(restRect().topLeft());
		paintContent(p, angle);
		return;
	}
	validateCache(angle);
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(_fold.opacity);
	p.translate(-x(), -y());
	p.setTransform(_fold.transform, true);
	p.drawImage(QRectF(_fold.rest), _cache);
}

void Card::paintContent(Painter &p, float64 angle) {
	const auto size = restRect().size();
	_background.paint(p, QRect(QPoint(), size), angle);

	const auto qr = CardQrRect(size.width());
	PaintCardQrPlate(p, qr);
	st::walletCardQrIcon.paintInCenter(p, qr, CardQrIconFg());

	const auto &nameFont = _nameStyle.font;
	const auto addressFont = st::walletCardAddressFont->monospace();
	const auto addressBaseline = size.width()
		- st::walletCardAddressRight
		- addressFont->height
		- addressFont->ascent;
	const auto stripLeft = addressBaseline - addressFont->descent;
	const auto nameMax = stripLeft
		- st::walletCardContentSkip
		- st::walletCardContentLeft;
	p.setPen(st::activeButtonFg);
	_name.drawLeftElided(
		p,
		st::walletCardContentLeft,
		size.height() - st::walletCardNameBottom - nameFont->ascent,
		nameMax,
		size.width(),
		1);

	if (!_addressLine1.isEmpty()) {
		p.setPen(st::windowActiveTextFg);
		p.setFont(addressFont);
		p.save();
		p.translate(addressBaseline, st::walletCardAddressSkip);
		p.rotate(90);
		p.drawText(0, 0, _addressLine1);
		p.drawText(0, -addressFont->height, _addressLine2);
		p.restore();
	}
}

CardFold ComputeCardFold(QRect cardRest, float64 fold) {
	auto result = CardFold();
	result.rest = cardRest;
	result.fold = fold;
	result.topY = cardRest.top() - cardRest.top() * fold;
	result.bottomY = result.topY + cardRest.height() * (1. - fold);
	result.opacity = 1. - std::pow(fold, st::walletCardFoldFadePower);
	if (result.bottomY - result.topY < kCardFoldMinHeight) {
		return result;
	}
	const auto bottomWidth = cardRest.width()
		* (1. - (1. - st::walletCardFoldBottomScale) * fold);
	const auto topWidth = bottomWidth
		* (1. - (1. - st::walletCardFoldTopScale) * fold);
	const auto center = cardRest.left() + cardRest.width() / 2.;
	const auto rest = QRectF(cardRest);
	const auto restQuad = QPolygonF({
		rest.topLeft(),
		rest.topRight(),
		rest.bottomRight(),
		rest.bottomLeft(),
	});
	auto quad = QPolygonF({
		QPointF(center - topWidth / 2., result.topY),
		QPointF(center + topWidth / 2., result.topY),
		QPointF(center + bottomWidth / 2., result.bottomY),
		QPointF(center - bottomWidth / 2., result.bottomY),
	});
	auto transform = QTransform();
	if (!QTransform::quadToQuad(restQuad, quad, transform)) {
		return result;
	}
	result.quad = std::move(quad);
	result.transform = transform;
	result.valid = true;
	return result;
}

} // namespace ContentDetails

} // namespace Wallet
