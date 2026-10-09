#include "wallet/wallet_amount_field_internal.h"

namespace Wallet {

namespace AmountFieldDetails {}

using namespace AmountFieldDetails;

namespace AmountFieldDetails {

EquivalentLabel::EquivalentLabel(
	QWidget *parent,
	Ui::Text::MarkedContext context)
: RpWidget(parent)
, _context(std::move(context))
, _flow(
	st::walletSendUserFiatButton.style.font,
	st::walletSendUserFiatButton.style.font) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	_context.repaint = [=] { update(); };
	_arrows = createPart(tr::marked(u" ↑↓"_q));
	_animation.init([=](crl::time now) {
		return animationCallback(now);
	});
}

rpl::producer<int> EquivalentLabel::labelWidthValue() const {
	return _width.value();
}

void EquivalentLabel::setLabel(AmountLabel label) {
	if (_initialized && label == _label) {
		return;
	}
	const auto &st = st::walletSendUserFiatButton;
	auto full = label.prefix;
	full.append(label.amount).append(label.suffix).append(_arrows.source);
	_static.setMarkedText(st.style, full, kMarkupTextOptions, _context);
	auto targets = LabelTargets(label);
	const auto now = crl::now();
	if (!_initialized || anim::Disabled()) {
		_flow.reset(targets);
		_prefixes.clear();
		_prefixes.push_back(createPart(label.prefix));
		_suffixes.clear();
		_suffixes.push_back(createPart(tr::marked(label.suffix)));
		_targets = std::move(targets);
		_extra.jump(_static.maxWidth() - restWidth());
		_animation.stop();
	} else {
		const auto switched = (label.unit != _label.unit);
		const auto growing = AmountValue(label.amount, label.decimal)
			> AmountValue(_label.amount, _label.decimal);
		const auto duration = switched ? kSwitchFadeDuration : kEditDuration;
		_flow.roll(
			targets,
			switched ? GlyphMode::Roll : GlyphMode::Fade,
			growing,
			now);
		crossFade(_prefixes, label.prefix, duration, now);
		crossFade(_suffixes, tr::marked(label.suffix), duration, now);
		_targets = std::move(targets);
		_extra.retarget(
			_static.maxWidth() - restWidth(),
			now,
			switched ? kSwitchDuration : kEditDuration);
		if (animating(now) && !_animation.animating()) {
			_animation.start();
		}
	}
	_label = std::move(label);
	_initialized = true;
	refreshWidth(now);
	update();
}

QString EquivalentLabel::plainText() const {
	return _label.prefix.text
		+ _label.amount
		+ _label.suffix
		+ _arrows.source.text;
}

LabelPart EquivalentLabel::createPart(const TextWithEntities &source) const {
	const auto &st = st::walletSendUserFiatButton.style;
	const auto &text = source.text;
	const auto size = int(text.size());
	auto lead = 0;
	while (lead < size && text[lead] == QChar(' ')) {
		++lead;
	}
	auto trail = 0;
	while (trail < size - lead && text[size - 1 - trail] == QChar(' ')) {
		++trail;
	}
	auto result = LabelPart{ .source = source };
	result.text.setMarkedText(st, source, kMarkupTextOptions, _context);
	const auto space = float64(st.font->spacew);
	result.lead = lead * space;
	result.width = result.lead + result.text.maxWidth() + trail * space;
	result.v.jump(1.);
	return result;
}

void EquivalentLabel::crossFade(
		std::vector<LabelPart> &parts,
		const TextWithEntities &source,
		crl::time duration,
		crl::time now) {
	auto found = false;
	for (auto &part : parts) {
		const auto mine = !found && (part.source == source);
		found = found || mine;
		part.v.retarget(mine ? 1. : 0., now, duration);
	}
	if (!found) {
		auto &added = parts.emplace_back(createPart(source));
		added.v.jump(0.);
		added.v.retarget(1., now, duration);
	}
}

void EquivalentLabel::finishContent() {
	_flow.finish();
	for (auto *parts : { &_prefixes, &_suffixes }) {
		for (auto &part : *parts) {
			part.v.jump(part.v.to);
		}
		parts->erase(ranges::remove_if(*parts, [](const LabelPart &part) {
			return (part.v.to == 0.);
		}), end(*parts));
	}
	_extra.jump(_extra.to);
}

bool EquivalentLabel::animationCallback(crl::time now) {
	const auto disabled = anim::Disabled();
	if (disabled) {
		finishContent();
	} else {
		_flow.prune(now);
		for (auto *parts : { &_prefixes, &_suffixes }) {
			parts->erase(ranges::remove_if(*parts, [&](const LabelPart &part) {
				return (part.v.to == 0.) && !part.v.running(now);
			}), end(*parts));
		}
	}
	const auto result = !disabled && animating(now);
	refreshWidth(result ? now : 0);
	update();
	return result;
}

bool EquivalentLabel::animating(crl::time now) const {
	const auto running = [&](const LabelPart &part) {
		return part.v.running(now);
	};
	return _flow.animating(now)
		|| _extra.running(now)
		|| ranges::any_of(_prefixes, running)
		|| ranges::any_of(_suffixes, running);
}

float64 EquivalentLabel::restWidth() const {
	auto result = _arrows.width;
	for (const auto &target : _targets) {
		result += target.width;
	}
	for (const auto *parts : { &_prefixes, &_suffixes }) {
		for (const auto &part : *parts) {
			result += part.width * part.v.to;
		}
	}
	return result;
}

float64 EquivalentLabel::animatedWidth(crl::time now) const {
	auto result = _arrows.width + _flow.width(now) + _extra.value(now);
	for (const auto *parts : { &_prefixes, &_suffixes }) {
		for (const auto &part : *parts) {
			result += part.width * part.v.value(now);
		}
	}
	return result;
}

void EquivalentLabel::refreshWidth(crl::time now) {
	_width = (now && _animation.animating())
		? int(base::SafeRound(animatedWidth(now)))
		: _static.maxWidth();
}

float64 EquivalentLabel::paintParts(
		QPainter &p,
		std::span<const LabelPart> parts,
		float64 left,
		crl::time now) const {
	auto allotted = 0.;
	for (const auto &part : parts) {
		allotted += part.width * part.v.value(now);
	}
	const auto &st = st::walletSendUserFiatButton;
	const auto middle = st.style.font->height / 2.;
	auto palette = st::defaultTextPalette;
	palette.linkFg = st.numbersTextFg;
	const auto opacity = p.opacity();
	for (const auto &part : parts) {
		const auto v = part.v.value(now);
		const auto scale = (allotted < part.width)
			? (allotted / part.width)
			: 1.;
		if (v <= 0. || scale <= 0. || part.text.isEmpty()) {
			continue;
		}
		p.save();
		p.setOpacity(opacity * v);
		p.translate(left, st.padding.top() + st.textTop + middle);
		p.scale(scale, scale);
		p.translate(part.lead, -middle);
		part.text.draw(p, {
			.position = { 0, 0 },
			.availableWidth = part.text.maxWidth(),
			.palette = &palette,
		});
		p.restore();
	}
	return allotted;
}

void EquivalentLabel::paintAnimated(QPainter &p, crl::time now) const {
	const auto &st = st::walletSendUserFiatButton;
	const auto padding = st.padding.left() + st.padding.right();
	const auto top = float64(st.padding.top() + st.textTop);
	auto x = st.padding.left() + (width() - animatedWidth(now) - padding) / 2.;
	x += paintParts(p, _prefixes, x, now);
	_flow.paint(
		p,
		QPointF(x, top),
		st.style.font->ascent,
		st.textFg->c,
		now);
	x += _flow.width(now);
	x += paintParts(p, _suffixes, x, now);
	paintParts(p, { &_arrows, 1 }, x, now);
}

void EquivalentLabel::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto &st = st::walletSendUserFiatButton;
	p.setPen(st.textFg);
	if (_animation.animating()) {
		auto hq = PainterHighQualityEnabler(p);
		paintAnimated(p, crl::now());
		return;
	}
	const auto padding = st.padding.left() + st.padding.right();
	const auto inner = std::min(_static.maxWidth(), width() - padding);
	auto palette = st::defaultTextPalette;
	palette.linkFg = st.numbersTextFg;
	_static.draw(p, {
		.position = {
			st.padding.left() + (width() - inner - padding) / 2,
			st.padding.top() + st.textTop,
		},
		.availableWidth = std::max(inner, 0),
		.palette = &palette,
		.elisionLines = 1,
	});
}

void SwapPill::setAccessibleText(QString text) {
	if (_accessibleText != text) {
		_accessibleText = std::move(text);
		accessibilityNameChanged();
	}
}

QString SwapPill::accessibilityName() {
	return _accessibleText;
}

}

not_null<Ui::TonAmountInput*> AddAmountField(
		not_null<Ui::VerticalLayout*> container,
		int topSkip,
		AmountFieldArgs &&args) {
	const auto wrap = container->add(
		object_ptr<Ui::RpWidget>(container),
		style::margins(
			st::walletSendFieldMargin.left(),
			topSkip,
			st::walletSendFieldMargin.right(),
			st::walletSendFieldMargin.bottom()));
	const auto row = Ui::CreateChild<AmountRow>(wrap, args);
	const auto &st = st::walletSendUserFiatButton;
	const auto pill = Ui::CreateChild<SwapPill>(
		wrap,
		rpl::single(QString()),
		st);
	pill->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	pill->setClickedCallback(std::move(args.swap));
	const auto label = Ui::CreateChild<EquivalentLabel>(
		wrap,
		args.equivalentContext);
	std::move(args.equivalent) | rpl::on_next([=](AmountLabel value) {
		label->setLabel(std::move(value));
		pill->setAccessibleText(label->plainText());
	}, label->lifetime());
	std::move(args.equivalentShown) | rpl::on_next([=](bool shown) {
		pill->setVisible(shown);
		label->setVisible(shown);
	}, pill->lifetime());
	const auto band = AmountBand();
	rpl::combine(
		wrap->widthValue(),
		label->labelWidthValue()
	) | rpl::on_next([=](int width, int labelWidth) {
		const auto natural = labelWidth
			- st.width
			+ st.padding.left()
			+ st.padding.right();
		row->setGeometry(0, 0, width, band);
		pill->resize(std::min(natural, width), pill->height());
		pill->moveToLeft(
			(width - pill->width()) / 2,
			band + st::walletSendFieldMargin.top(),
			width);
		label->setGeometry(pill->geometry());
		wrap->resize(width, pill->y() + pill->height());
	}, wrap->lifetime());
	return row->field();
}

AmountDiamond TakeAmountDiamond(not_null<Ui::TonAmountInput*> field) {
	if (const auto row = dynamic_cast<AmountRow*>(field->parentWidget())) {
		return row->takeDiamond();
	}
	return {};
}

}
