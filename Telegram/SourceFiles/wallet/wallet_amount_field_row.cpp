#include "wallet/wallet_amount_field_internal.h"

namespace Wallet {

namespace AmountFieldDetails {}

using namespace AmountFieldDetails;

namespace AmountFieldDetails {

AmountRow::AmountRow(QWidget *parent, AmountFieldArgs &args)
: RpWidget(parent)
, _field(Ui::CreateChild<Ui::TonAmountInput>(
	this,
	st::walletSendUserAmountField,
	args.value,
	std::move(args.fractionDigits),
	args.separator))
, _separator(std::move(args.separator))
, _diamondCanvas(
	GramDiamondCanvas(st::walletSendUserAmountField.style.font))
, _figureBig(FigureHeight(st::walletSendUserAmountField.style.font))
, _figureSmall(FigureHeight(st::walletSendUserTickerLabel.style.font))
// Not CreateLottieIcon: it replays any icon resting past its first frame.
, _diamond(Lottie::MakeIcon({
	.name = u"gram"_q,
	.sizeOverride = { _diamondCanvas, _diamondCanvas },
	.frame = -1,
	.limitFps = true,
}))
, _flow(
	st::walletSendUserAmountField.style.font,
	st::walletSendUserTickerLabel.style.font)
, _blink([=] {
	_caretShown = !_caretShown;
	update(caretRect());
}) {
	_field->setAttribute(Qt::WA_TransparentForMouseEvents);
	setMouseTracking(true);
	setCursor(style::cur_pointer);

	_animation.init([=](crl::time now) {
		return animationCallback(now);
	});

	rpl::combine(
		std::move(args.entryFiat),
		std::move(args.currency)
	) | rpl::on_next([=](bool fiat, QString code) {
		const auto name = Ui::CurrencyName(code);
		const auto switched = _initialized
			&& ((fiat != _fiat) || (fiat && code != _currency));
		_fiat = fiat;
		_ticker = fiat ? code : GramTicker();
		_symbol = (fiat && name != code) ? name : QString();
		_currency = std::move(code);
		if (!switched) {
			refreshContent();
			return;
		}
		_switchPending = true;
		Ui::PostponeCall(this, [=] {
			if (_switchPending) {
				refreshContent();
			}
		});
	}, lifetime());

	_field->changes() | rpl::on_next([=] {
		refreshContent();
	}, lifetime());
	const auto moved = [=] {
		if (_field->hasSelectedText()) {
			finishAnimation();
		}
		restartBlink();
		update();
		if (_field->hasFocus()) {
			QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
		}
	};
	connect(_field, &QLineEdit::cursorPositionChanged, this, moved);
	connect(_field, &QLineEdit::selectionChanged, this, moved);
	_field->compositionChanges() | rpl::on_next(moved, lifetime());
	connect(_field, &Ui::MaskedInputField::focused, this, [=] {
		restartBlink();
	});
	connect(_field, &Ui::MaskedInputField::blurred, this, [=] {
		restartBlink();
	});
	_field->setCaretRectCallback([=] {
		return caretRect().translated(-_field->pos());
	});

	sizeValue() | rpl::on_next([=] {
		refreshGeometry();
	}, lifetime());
	refreshContent();
	_initialized = true;
}

not_null<Ui::TonAmountInput*> AmountRow::field() const {
	return _field;
}

void AmountRow::refreshContent() {
	const auto &text = _field->getLastText();
	const auto separator = _separator ? _separator() : QString();
	const auto at = separator.isEmpty() ? -1 : int(text.indexOf(separator));
	const auto digits = (at >= 0) ? text.left(at) : text;
	const auto fraction = (at >= 0) ? text.mid(at) : QString();
	const auto whole = GroupWhole(digits, _fiat, _currency);
	const auto &big = st::walletSendUserAmountField.style.font;
	const auto &small = st::walletSendUserTickerLabel.style.font;
	const auto switching = base::take(_switchPending);
	refreshLayers(switching, crl::now());
	const auto additionWidth = int(TargetWidth(_additions));
	const auto gap = st::walletDetailsAmountMinorSkip;
	_painter.setContent({
		.big = big,
		.small = small,
		.ticker = small,
		.additionWidth = additionWidth,
		.additionSkip = gap,
		.tickerSkip = gap,
	}, {
		.whole = whole,
		.fraction = fraction,
		.ticker = _ticker,
	});
	_layout = ComputeAmountLayout(
		whole,
		fraction,
		big,
		small,
		_painter.wholeLeft(),
		_painter.fractionLeft());
	applyChange(text, separator, whole, fraction, switching);
	refreshGeometry();
}

void AmountRow::refreshLayers(bool switching, crl::time now) {
	auto addition = std::optional<Layer>();
	if (!_fiat) {
		addition = Layer{
			.kind = LayerKind::Diamond,
			.width = float64(DiamondPart(
				_diamondCanvas,
				kGramDiamondRight - kGramDiamondLeft)),
		};
	} else if (!_symbol.isEmpty()) {
		addition = Layer{
			.kind = LayerKind::Symbol,
			.text = _symbol,
			.width = float64(
				st::walletSendUserAmountLabel.style.font->width(_symbol)),
			.fiat = true,
		};
	}
	auto ticker = std::optional<Layer>();
	if (!_ticker.isEmpty()) {
		ticker = Layer{
			.kind = LayerKind::Ticker,
			.text = _ticker,
			.width = float64(
				st::walletSendUserTickerLabel.style.font->width(_ticker)),
			.fiat = _fiat,
		};
	}
	ChangeLayers(_additions, std::move(addition), switching, now);
	ChangeLayers(_tickers, std::move(ticker), switching, now);
}

void AmountRow::applyChange(
		const QString &text,
		const QString &separator,
		const QString &whole,
		const QString &fraction,
		bool switching) {
	auto targets = AmountTargets(
		whole,
		fraction,
		st::walletSendUserAmountField.style.font,
		st::walletSendUserTickerLabel.style.font);
	const auto changed = (text != _shownText);
	if (!_initialized || anim::Disabled()) {
		_flow.reset(std::move(targets));
		finishAnimation();
	} else if (switching) {
		const auto growing = AmountValue(text, separator)
			> AmountValue(_shownText, _shownSeparator);
		_flow.roll(std::move(targets), GlyphMode::Roll, growing, crl::now());
		startAnimation();
	} else {
		const auto kind = ClassifyEdit(
			_shownText,
			text,
			_field->cursorPosition(),
			separator);
		_flow.edit(std::move(targets), kind, crl::now());
		startAnimation();
		if (changed && !_fiat) {
			playDiamond();
		}
	}
	_shownText = text;
	_shownSeparator = separator;
}

void AmountRow::startAnimation() {
	if (contentAnimating(crl::now()) && !_animation.animating()) {
		_animation.start();
	}
}

void AmountRow::finishContent() {
	_flow.finish();
	for (auto *layers : { &_additions, &_tickers }) {
		for (auto &layer : *layers) {
			layer.v.jump(layer.v.to);
		}
		layers->erase(ranges::remove_if(*layers, [](const Layer &layer) {
			return (layer.v.to == 0.);
		}), end(*layers));
	}
}

void AmountRow::finishAnimation() {
	finishContent();
	_animation.stop();
	update();
}

bool AmountRow::animationCallback(crl::time now) {
	if (anim::Disabled()) {
		finishContent();
		update();
		return false;
	}
	_flow.prune(now);
	for (auto *layers : { &_additions, &_tickers }) {
		layers->erase(ranges::remove_if(*layers, [&](const Layer &layer) {
			return (layer.v.to == 0.) && !layer.v.running(now);
		}), end(*layers));
	}
	update();
	return contentAnimating(now);
}

bool AmountRow::contentAnimating(crl::time now) const {
	const auto running = [&](const Layer &layer) {
		return layer.v.running(now);
	};
	return _flow.animating(now)
		|| ranges::any_of(_additions, running)
		|| ranges::any_of(_tickers, running);
}

bool AmountRow::flowPainted() const {
	return _animation.animating() || !_field->composition().isEmpty();
}

void AmountRow::playDiamond() {
	if (!_diamond
		|| _diamond->animating()
		|| !_diamond->valid()
		|| anim::Disabled()
		|| PowerSaving::On(PowerSaving::kStickersChat)) {
		return;
	}
	_diamond->animate(
		[=] { update(additionRect()); },
		0,
		_diamond->framesCount() - 1);
}

QRect AmountRow::additionRect() const {
	if (flowPainted()) {
		return rect();
	}
	const auto k = _painter.scale();
	const auto x = -DiamondPart(_diamondCanvas, kGramDiamondLeft);
	const auto y = _painter.baseline()
		- DiamondPart(_diamondCanvas, kGramDiamondBottom);
	return QRectF(
		_left + k * x,
		_top + k * y,
		k * _diamondCanvas,
		k * _diamondCanvas).toAlignedRect();
}

std::optional<QRectF> AmountRow::diamondCanvas(crl::time now) const {
	if (_fiat || !_diamond || !_diamond->valid()) {
		return std::nullopt;
	}
	const auto diamond = ranges::find(
		_additions,
		LayerKind::Diamond,
		&Layer::kind);
	if (diamond == end(_additions)
		|| diamond->v.to != 1.
		|| diamond->v.value(now) < 1.) {
		return std::nullopt;
	}
	const auto animated = flowPainted();
	const auto geometry = animated ? animatedGeometry(now) : RowGeometry();
	const auto k = animated ? geometry.k : _painter.scale();
	const auto left = animated ? geometry.left : _left;
	const auto top = animated ? geometry.top : _top;
	const auto allotted = animated
		? geometry.additions
		: float64(_painter.wholeLeft());
	const auto full = diamond->width + st::walletDetailsAmountMinorSkip;
	const auto scale = (allotted < full) ? (allotted / full) : 1.;
	if (scale <= 0.) {
		return std::nullopt;
	}
	const auto baseline = float64(_painter.baseline());
	const auto middle = baseline - _figureBig / 2.;
	const auto x = -DiamondPart(_diamondCanvas, kGramDiamondLeft);
	const auto y = baseline - DiamondPart(_diamondCanvas, kGramDiamondBottom);
	return QRectF(
		left + k * scale * x,
		top + k * (middle + scale * (y - middle)),
		k * scale * _diamondCanvas,
		k * scale * _diamondCanvas);
}

AmountDiamond AmountRow::takeDiamond() {
	const auto canvas = diamondCanvas(crl::now());
	if (!canvas) {
		return {};
	}
	_diamond->jumpTo(_diamond->frameIndex(), nullptr);
	auto result = AmountDiamond{
		.icon = std::move(_diamond),
		.global = canvas->translated(QPointF(mapToGlobal(QPoint()))),
	};
	update();
	return result;
}

RowGeometry AmountRow::animatedGeometry(crl::time now) const {
	const auto gap = float64(st::walletDetailsAmountMinorSkip);
	auto result = RowGeometry{
		.flow = _flow.width(now),
		.composition = compositionFont()->metrics().horizontalAdvance(
			_field->composition()),
	};
	for (const auto &layer : _additions) {
		if (layer.width > 0.) {
			result.additions += layer.v.value(now) * (layer.width + gap);
		}
	}
	auto tail = 0.;
	for (const auto &layer : _tickers) {
		const auto v = layer.v.value(now);
		result.tickerSkip += v * gap;
		tail += v * (gap + layer.width);
	}
	const auto natural = result.additions
		+ result.flow
		+ result.composition
		+ tail;
	const auto available = width();
	result.k = (available > 0 && natural > available)
		? (available / natural)
		: 1.;
	result.left = std::max(
		std::floor((available - result.k * natural) / 2.),
		0.);
	result.top = std::floor(
		(height() - result.k * _painter.naturalHeight()) / 2.);
	return result;
}

void AmountRow::refreshGeometry() {
	_painter.setAvailableWidth(width());
	const auto k = _painter.scale();
	_left = std::max(
		std::floor((width() - k * _painter.naturalWidth()) / 2.),
		0.);
	_top = std::floor((height() - k * _painter.naturalHeight()) / 2.);
	const auto gap = st::walletDetailsAmountMinorSkip;
	const auto wholeLeft = _painter.wholeLeft();
	const auto digitsWidth = _painter.tickerLeft() - gap - wholeLeft;
	_field->setGeometry(
		int(std::floor(_left + k * wholeLeft)),
		0,
		std::max(int(std::ceil(k * digitsWidth)), 1),
		height());
	update();
}

void AmountRow::restartBlink() {
	_caretShown = _field->hasFocus();
	const auto interval = QGuiApplication::styleHints()->cursorFlashTime() / 2;
	if (_caretShown && interval > 0) {
		_blink.callEach(interval);
	} else {
		_blink.cancel();
	}
	update(caretRect());
}

int AmountRow::caretPosition() const {
	return std::clamp(
		_field->cursorPosition(),
		0,
		std::max(int(_layout.caret.size()) - 1, 0));
}

bool AmountRow::caretInFraction() const {
	return (_layout.separatorAt >= 0)
		&& (caretPosition() > _layout.separatorAt);
}

const style::font &AmountRow::compositionFont() const {
	return caretInFraction()
		? st::walletSendUserTickerLabel.style.font
		: st::walletSendUserAmountField.style.font;
}

QRect AmountRow::caretRect() const {
	if (_layout.caret.empty()) {
		return QRect();
	}
	if (!_field->composition().isEmpty()) {
		const auto now = crl::now();
		return flowCaretRect(animatedGeometry(now), now);
	}
	return caretRect(
		_painter.scale(),
		_left,
		_top,
		_layout.caret[caretPosition()]);
}

QRect AmountRow::caretRect(
		float64 k,
		float64 left,
		float64 top,
		float64 x) const {
	const auto figure = caretInFraction() ? _figureSmall : _figureBig;
	const auto baseline = _painter.baseline();
	const auto from = base::SafeRound(left + k * x);
	const auto upper = base::SafeRound(top + k * (baseline - figure));
	const auto lower = base::SafeRound(top + k * baseline);
	const auto width = std::max(
		_field->style()->pixelMetric(
			QStyle::PM_TextCursorWidth,
			nullptr,
			_field),
		1);
	return QRect(int(from), int(upper), width, int(lower - upper));
}

QRect AmountRow::flowCaretRect(
		const RowGeometry &geometry,
		crl::time now) const {
	return caretRect(
		geometry.k,
		geometry.left,
		geometry.top,
		(geometry.additions
			+ _flow.caretX(caretPosition(), now)
			+ geometry.composition));
}

std::array<QRectF, 2> AmountRow::selectionRects() const {
	auto result = std::array<QRectF, 2>();
	if (!_field->hasSelectedText()) {
		return result;
	}
	const auto length = int(_layout.right.size());
	const auto from = std::clamp(_field->selectionStart(), 0, length);
	const auto till = std::clamp(
		from + _field->selectionLength(),
		from,
		length);
	const auto whole = (_layout.separatorAt >= 0)
		? _layout.separatorAt
		: length;
	const auto baseline = _painter.baseline();
	if (from < whole && from < till) {
		const auto end = std::min(till, whole);
		const auto left = _layout.caret[from];
		const auto right = (end == whole)
			? _layout.caret[whole]
			: _layout.right[end - 1];
		result[0] = QRectF(
			left,
			baseline - _figureBig,
			right - left,
			_figureBig);
	}
	if (till > whole) {
		const auto start = std::max(from, whole);
		const auto left = _layout.caret[start];
		const auto right = _layout.right[till - 1];
		result[1] = QRectF(
			left,
			baseline - _figureSmall,
			right - left,
			_figureSmall);
	}
	return result;
}

int AmountRow::positionAt(int x) const {
	const auto natural = (x - _left) / _painter.scale();
	auto result = 0;
	auto distance = std::numeric_limits<float64>::max();
	for (auto i = 0; i != int(_layout.caret.size()); ++i) {
		const auto now = std::abs(_layout.caret[i] - natural);
		if (now < distance) {
			distance = now;
			result = i;
		}
	}
	return result;
}

bool AmountRow::inDigitsZone(int x) const {
	const auto k = _painter.scale();
	const auto half = st::walletDetailsAmountMinorSkip / 2.;
	const auto from = _left + k * (_painter.wholeLeft() - half);
	const auto till = _left + k * (_painter.tickerLeft() - half);
	return (x >= from) && (x <= till);
}

int AmountRow::selectionAnchor() const {
	const auto cursor = _field->cursorPosition();
	if (!_field->hasSelectedText()) {
		return cursor;
	}
	const auto start = _field->selectionStart();
	const auto end = start + _field->selectionLength();
	return (cursor == start) ? end : start;
}

void AmountRow::select(int anchor, int position) {
	if (anchor == position) {
		_field->setCursorPosition(position);
	} else {
		_field->setSelection(anchor, position - anchor);
	}
}

void AmountRow::paintAddition(
		QPainter &p,
		float64 allotted,
		crl::time now) const {
	const auto baseline = _painter.baseline();
	const auto middle = baseline - _figureBig / 2.;
	const auto gap = float64(st::walletDetailsAmountMinorSkip);
	const auto opacity = p.opacity();
	for (const auto &layer : _additions) {
		const auto full = layer.width + gap;
		const auto scale = (allotted < full) ? (allotted / full) : 1.;
		const auto v = layer.v.value(now);
		if (v <= 0. || scale <= 0.) {
			continue;
		}
		p.save();
		p.setOpacity(opacity * v);
		p.translate(0., middle);
		p.scale(scale, scale);
		p.translate(0., -middle);
		if (layer.kind == LayerKind::Diamond) {
			if (_diamond) {
				_diamond->paint(
					p,
					-DiamondPart(_diamondCanvas, kGramDiamondLeft),
					baseline - DiamondPart(_diamondCanvas, kGramDiamondBottom));
			}
		} else {
			p.setFont(st::walletSendUserAmountLabel.style.font);
			p.setPen(LayerColor(layer));
			p.drawText(QPointF(0., baseline), layer.text);
		}
		p.restore();
	}
}

void AmountRow::paintTickers(
		QPainter &p,
		float64 left,
		crl::time now) const {
	auto allotted = 0.;
	for (const auto &layer : _tickers) {
		allotted += layer.v.value(now) * layer.width;
	}
	const auto baseline = float64(_painter.baseline());
	const auto middle = baseline - _figureSmall / 2.;
	const auto &font = st::walletSendUserTickerLabel.style.font;
	const auto opacity = p.opacity();
	for (const auto &layer : _tickers) {
		const auto scale = (allotted < layer.width)
			? (allotted / layer.width)
			: 1.;
		const auto v = layer.v.value(now);
		if (v <= 0. || scale <= 0.) {
			continue;
		}
		auto path = QPainterPath();
		path.addText(0., baseline - middle, font->f, layer.text);
		p.save();
		p.setOpacity(opacity * v);
		p.translate(left, middle);
		p.scale(scale, scale);
		p.fillPath(path, LayerColor(layer)->c);
		p.restore();
	}
}

void AmountRow::paintComposition(
		QPainter &p,
		float64 left,
		float64 baseline) const {
	auto path = QPainterPath();
	path.addText(
		left,
		baseline,
		compositionFont()->underline()->f,
		_field->composition());
	p.fillPath(path, st::walletSendUserAmountField.textFg->c);
}

void AmountRow::paintAnimated(QPainter &p, crl::time now) const {
	const auto geometry = animatedGeometry(now);
	const auto baseline = float64(_painter.baseline());
	const auto &fg = st::walletSendUserAmountField.textFg;
	p.translate(geometry.left, geometry.top);
	p.scale(geometry.k, geometry.k);
	paintAddition(p, geometry.additions, now);
	const auto position = caretPosition();
	_flow.paint(
		p,
		QPointF(geometry.additions, 0.),
		baseline,
		fg->c,
		now,
		{ .position = position, .width = geometry.composition });
	if (geometry.composition > 0.) {
		paintComposition(
			p,
			geometry.additions + _flow.caretX(position, now),
			baseline);
	}
	paintTickers(
		p,
		(geometry.additions
			+ geometry.flow
			+ geometry.composition
			+ geometry.tickerSkip),
		now);
	p.resetTransform();
	if (_caretShown && _field->hasFocus()) {
		p.fillRect(flowCaretRect(geometry, now), fg);
	}
}

void AmountRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto now = crl::now();
	if (flowPainted()) {
		paintAnimated(p, now);
		return;
	}
	const auto k = _painter.scale();
	const auto &fg = st::walletSendUserAmountField.textFg;
	const auto ticker = (_fiat
		? st::walletSendUserFiatFg
		: st::walletSendUserGramFg)->c;
	p.translate(_left, _top);
	p.scale(k, k);
	paintAddition(p, _painter.wholeLeft(), now);
	const auto selection = selectionRects();
	for (const auto &rect : selection) {
		if (!rect.isEmpty()) {
			p.fillRect(rect, st::msgInBgSelected);
		}
	}
	_painter.paint(p, { .digits = fg->c, .ticker = ticker });
	for (const auto &rect : selection) {
		if (!rect.isEmpty()) {
			p.save();
			p.setClipRect(rect);
			_painter.paint(p, {
				.digits = st::historyTextInFgSelected->c,
				.ticker = ticker,
			});
			p.restore();
		}
	}
	p.resetTransform();
	if (_caretShown && _field->hasFocus()) {
		p.fillRect(caretRect(), fg);
	}
}

void AmountRow::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_field->setFocusFast();
	_field->commitComposition();
	const auto x = e->pos().x();
	if (!inDigitsZone(x)) {
		return;
	}
	const auto position = positionAt(x);
	if (e->modifiers() & Qt::ShiftModifier) {
		_anchor = selectionAnchor();
		select(_anchor, position);
	} else {
		_anchor = position;
		_field->setCursorPosition(position);
	}
	_selecting = true;
}

void AmountRow::mouseMoveEvent(QMouseEvent *e) {
	const auto x = e->pos().x();
	const auto shape = inDigitsZone(x)
		? style::cur_text
		: style::cur_pointer;
	if (cursor().shape() != shape) {
		setCursor(shape);
	}
	if (_selecting && (e->buttons() & Qt::LeftButton)) {
		select(_anchor, positionAt(x));
	}
}

void AmountRow::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_selecting = false;
	}
}

void AmountRow::mouseDoubleClickEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_field->setFocusFast();
	_field->commitComposition();
	_selecting = false;
	if (inDigitsZone(e->pos().x())) {
		_field->selectAll();
	}
}

void AmountRow::contextMenuEvent(QContextMenuEvent *e) {
	_field->setFocusFast();
	auto mapped = QContextMenuEvent(
		e->reason(),
		_field->mapFrom(this, e->pos()),
		e->globalPos(),
		e->modifiers());
	QCoreApplication::sendEvent(_field, &mapped);
	e->accept();
}

}

}
