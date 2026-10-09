#include "wallet/wallet_amount_field_internal.h"

namespace Wallet {

namespace AmountFieldDetails {}

using namespace AmountFieldDetails;

namespace AmountFieldDetails {

[[nodiscard]] float64 Eased(Ease ease, float64 t) {
	return (ease == Ease::OutCirc)
		? anim::easeOutCirc(1., t)
		: anim::easeOutCubic(1., t);
}

[[nodiscard]] int AmountBand() {
	const auto &st = st::walletSendUserAmountField;
	return std::max(st.heightMin, st.style.font->height);
}

[[nodiscard]] int FigureHeight(const style::font &font) {
	return int(base::SafeRound(
		-font->metrics().tightBoundingRect(u"0123456789"_q).top()));
}

[[nodiscard]] int DiamondPart(int canvas, float64 part) {
	return int(base::SafeRound(canvas * part));
}

[[nodiscard]] int CountDigits(const QString &text) {
	return int(ranges::count_if(text, [](QChar ch) { return ch.isDigit(); }));
}

[[nodiscard]] QString GroupWhole(
		const QString &digits,
		bool fiat,
		const QString &currency) {
	auto result = QString();
	if (digits.isEmpty()) {
		return result;
	} else if (!fiat) {
		result = QLocale::system().toString(qlonglong(digits.toLongLong()));
	} else if (const auto thousands
			= Ui::LookupCurrencyRule(currency).thousands) {
		const auto size = int(digits.size());
		result.reserve(size + size / 3);
		for (auto i = 0; i != size; ++i) {
			if (i > 0 && (size - i) % 3 == 0) {
				result.append(QChar(thousands));
			}
			result.append(digits[i]);
		}
	}
	return (CountDigits(result) == digits.size()) ? result : digits;
}

[[nodiscard]] AmountLayout ComputeAmountLayout(
		const QString &whole,
		const QString &fraction,
		const style::font &big,
		const style::font &small,
		int wholeLeft,
		int fractionLeft) {
	const auto digits = CountDigits(whole);
	const auto length = digits + int(fraction.size());
	auto result = AmountLayout{
		.separatorAt = fraction.isEmpty() ? -1 : digits,
	};
	result.caret.reserve(length + 1);
	result.right.reserve(length);
	const auto &bigMetrics = big->metrics();
	for (auto i = 0; i != int(whole.size()); ++i) {
		if (!whole[i].isDigit()) {
			continue;
		}
		result.caret.push_back(
			wholeLeft + bigMetrics.horizontalAdvance(whole.left(i)));
		result.right.push_back(
			wholeLeft + bigMetrics.horizontalAdvance(whole.left(i + 1)));
	}
	result.caret.push_back(fractionLeft);
	const auto &smallMetrics = small->metrics();
	for (auto i = 0; i != int(fraction.size()); ++i) {
		const auto right = fractionLeft
			+ smallMetrics.horizontalAdvance(fraction.left(i + 1));
		result.right.push_back(right);
		result.caret.push_back(right);
	}
	return result;
}

[[nodiscard]] std::vector<float64> GlyphLefts(
		const style::font &font,
		const QString &text) {
	auto result = std::vector<float64>();
	if (text.isEmpty()) {
		return result;
	}
	auto layout = QTextLayout(text, font->f);
	layout.beginLayout();
	auto line = layout.createLine();
	line.setLineWidth(std::numeric_limits<short>::max());
	layout.endLayout();
	result.reserve(text.size());
	for (auto i = 0; i != int(text.size()); ++i) {
		result.push_back(line.cursorToX(i));
	}
	return result;
}

[[nodiscard]] std::vector<GlyphTarget> AmountTargets(
		const QString &whole,
		const QString &fraction,
		const style::font &big,
		const style::font &small) {
	auto result = std::vector<GlyphTarget>();
	result.reserve(whole.size() + fraction.size());
	const auto wholeLefts = GlyphLefts(big, whole);
	const auto wholeSize = int(whole.size());
	auto digitsAfter = CountDigits(whole);
	for (auto i = 0; i != wholeSize; ++i) {
		const auto ch = whole[i];
		const auto digit = ch.isDigit();
		if (digit) {
			--digitsAfter;
		}
		const auto right = (i + 1 < wholeSize)
			? wholeLefts[i + 1]
			: float64(big->width(whole));
		result.push_back({
			.ch = ch,
			.kind = digit ? GlyphKind::Digit : GlyphKind::Group,
			.width = right - wholeLefts[i],
			.place = digit ? digitsAfter : (digitsAfter - 1),
		});
	}
	const auto fractionLefts = GlyphLefts(small, fraction);
	const auto fractionSize = int(fraction.size());
	for (auto i = 0; i != fractionSize; ++i) {
		const auto ch = fraction[i];
		const auto right = (i + 1 < fractionSize)
			? fractionLefts[i + 1]
			: float64(small->width(fraction));
		result.push_back({
			.ch = ch,
			.kind = (i == 0)
				? GlyphKind::Decimal
				: ch.isDigit()
				? GlyphKind::Digit
				: GlyphKind::Other,
			.small = true,
			.width = right - fractionLefts[i],
			.place = -i,
		});
	}
	return result;
}

[[nodiscard]] std::vector<GlyphTarget> LabelTargets(
		const AmountLabel &label) {
	const auto &font = st::walletSendUserFiatButton.style.font;
	const auto at = label.decimal.isEmpty()
		? -1
		: int(label.amount.indexOf(label.decimal));
	return AmountTargets(
		(at >= 0) ? label.amount.left(at) : label.amount,
		(at >= 0) ? label.amount.mid(at) : QString(),
		font,
		font);
}

[[nodiscard]] float64 AmountValue(
		const QString &text,
		const QString &decimal) {
	const auto at = decimal.isEmpty() ? -1 : int(text.indexOf(decimal));
	auto digits = QString();
	for (auto i = 0; i != int(text.size()); ++i) {
		if (i == at) {
			digits.append(QChar('.'));
		} else if (text[i].isDigit()) {
			digits.append(text[i]);
		}
	}
	return digits.toDouble();
}

[[nodiscard]] EditKind ClassifyEdit(
		const QString &was,
		const QString &now,
		int cursor,
		const QString &separator) {
	const auto zero = u"0"_q;
	const auto nonZeroDigit = [](const QString &text) {
		return (text.size() == 1)
			&& text[0].isDigit()
			&& (text[0] != QChar('0'));
	};
	const auto wasAt = separator.isEmpty() ? -1 : int(was.indexOf(separator));
	const auto nowAt = separator.isEmpty() ? -1 : int(now.indexOf(separator));
	const auto atEnd = [&](const QString &shorter, const QString &longer) {
		return (longer.size() == shorter.size() + 1)
			&& longer.startsWith(shorter)
			&& (cursor == now.size());
	};
	const auto atWholeEnd = [&](
			const QString &shorter,
			int shorterAt,
			const QString &longer,
			int longerAt) {
		return (shorterAt >= 0)
			&& (longerAt == shorterAt + 1)
			&& (cursor == nowAt)
			&& (longer.size() == shorter.size() + 1)
			&& longer[shorterAt].isDigit()
			&& longer.startsWith(shorter.left(shorterAt))
			&& longer.endsWith(shorter.mid(shorterAt));
	};
	if (was == now) {
		return EditKind::None;
	} else if ((was == zero && nonZeroDigit(now))
		|| (now == zero && nonZeroDigit(was))) {
		return EditKind::Replace;
	} else if (atEnd(was, now) || atWholeEnd(was, wasAt, now, nowAt)) {
		return EditKind::Append;
	} else if (atEnd(now, was) || atWholeEnd(now, nowAt, was, wasAt)) {
		return EditKind::Remove;
	}
	return EditKind::Immediate;
}

[[nodiscard]] QString WholeText(const std::vector<GlyphTarget> &targets) {
	auto result = QString();
	for (const auto &target : targets) {
		if (!target.small) {
			result.append(target.ch);
		}
	}
	return result;
}

[[nodiscard]] std::vector<int> IndicesOf(
		const std::vector<GlyphTarget> &targets,
		bool groups) {
	auto result = std::vector<int>();
	for (auto i = 0; i != int(targets.size()); ++i) {
		if ((targets[i].kind == GlyphKind::Group) == groups) {
			result.push_back(i);
		}
	}
	return result;
}

[[nodiscard]] std::optional<std::vector<int>> EditMatch(
		const std::vector<GlyphTarget> &was,
		const std::vector<GlyphTarget> &now,
		EditKind kind) {
	auto result = std::vector<int>(now.size(), -1);
	const auto wasChars = IndicesOf(was, false);
	const auto nowChars = IndicesOf(now, false);
	const auto wasSize = int(wasChars.size());
	const auto nowSize = int(nowChars.size());
	if (kind == EditKind::Replace) {
		if (wasSize != 1 || nowSize != 1) {
			return std::nullopt;
		}
		result[nowChars[0]] = wasChars[0];
	} else {
		const auto grows = (kind == EditKind::Append);
		if (nowSize != wasSize + (grows ? 1 : -1)) {
			return std::nullopt;
		}
		auto prefix = 0;
		while (prefix < std::min(wasSize, nowSize)
			&& was[wasChars[prefix]].ch == now[nowChars[prefix]].ch) {
			++prefix;
		}
		for (auto i = 0; i != nowSize; ++i) {
			const auto from = (i < prefix)
				? i
				: grows
				? ((i == prefix) ? -1 : (i - 1))
				: (i + 1);
			if (from >= 0) {
				result[nowChars[i]] = wasChars[from];
			}
		}
	}
	if (WholeText(was) == WholeText(now)) {
		const auto wasGroups = IndicesOf(was, true);
		const auto nowGroups = IndicesOf(now, true);
		for (auto i = 0; i != int(nowGroups.size()); ++i) {
			result[nowGroups[i]] = wasGroups[i];
		}
	}
	return result;
}

[[nodiscard]] std::vector<int> PlaceMatch(
		const std::vector<GlyphTarget> &was,
		const std::vector<GlyphTarget> &now) {
	const auto key = [](const GlyphTarget &target) {
		const auto space = (target.kind == GlyphKind::Group)
			? 2
			: (target.kind == GlyphKind::Decimal)
			? 1
			: 0;
		return std::make_pair(space, target.place);
	};
	auto result = std::vector<int>(now.size(), -1);
	auto next = 0;
	for (auto j = 0; j != int(now.size()); ++j) {
		for (auto i = next; i != int(was.size()); ++i) {
			if (key(was[i]) == key(now[j])) {
				result[j] = i;
				next = i + 1;
				break;
			}
		}
	}
	return result;
}

[[nodiscard]] float64 Reach(
		InkRange range,
		const std::vector<InkRange> &inks) {
	auto result = std::numeric_limits<float64>::max();
	for (const auto &ink : inks) {
		result = std::min(
			result,
			std::max(ink.left - range.right, range.left - ink.right));
	}
	return result;
}

[[nodiscard]] const style::color &LayerColor(const Layer &layer) {
	return layer.fiat ? st::walletSendUserFiatFg : st::walletSendUserGramFg;
}

[[nodiscard]] float64 TargetWidth(const std::vector<Layer> &layers) {
	const auto i = ranges::find(layers, 1., [](const Layer &layer) {
		return layer.v.to;
	});
	return (i != end(layers)) ? i->width : 0.;
}

void ChangeLayers(
		std::vector<Layer> &layers,
		std::optional<Layer> target,
		bool animated,
		crl::time now) {
	const auto same = [&](const Layer &layer) {
		return target
			&& (layer.kind == target->kind)
			&& (layer.text == target->text)
			&& (layer.fiat == target->fiat);
	};
	if (!animated) {
		const auto shown = ranges::find(layers, 1., [](const Layer &layer) {
			return layer.v.to;
		});
		if ((shown != end(layers)) ? same(*shown) : !target) {
			return;
		}
		layers.clear();
		if (target) {
			target->v.jump(1.);
			layers.push_back(std::move(*target));
		}
		return;
	}
	auto found = false;
	for (auto &layer : layers) {
		const auto mine = !found && same(layer);
		found = found || mine;
		layer.v.retarget(mine ? 1. : 0., now, kSwitchFadeDuration);
	}
	if (!found && target) {
		target->v.jump(0.);
		target->v.retarget(1., now, kSwitchFadeDuration);
		layers.push_back(std::move(*target));
	}
}

}

}
