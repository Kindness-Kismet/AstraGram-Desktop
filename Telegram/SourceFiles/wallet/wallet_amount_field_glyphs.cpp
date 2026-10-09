#include "wallet/wallet_amount_field_internal.h"

namespace Wallet {

namespace AmountFieldDetails {}

using namespace AmountFieldDetails;

namespace AmountFieldDetails {

float64 Motion::value(crl::time now) const {
	if (duration <= 0 || now >= start + duration) {
		return to;
	} else if (now <= start) {
		return from;
	}
	const auto t = (now - start) / float64(duration);
	return from + (to - from) * Eased(ease, t);
}

bool Motion::running(crl::time now) const {
	return (duration > 0) && (now < start + duration);
}

void Motion::retarget(
		float64 target,
		crl::time now,
		crl::time length,
		Ease kind) {
	if (target == to) {
		return;
	}
	from = value(now);
	to = target;
	start = now;
	duration = length;
	ease = kind;
}

void Motion::jump(float64 target) {
	from = to = target;
	duration = 0;
}

GlyphFlow::GlyphFlow(const style::font &big, const style::font &small)
: _big(&big)
, _small(&small)
, _digitsInk(big->metrics().tightBoundingRect(u"0123456789"_q)) {
}

void GlyphFlow::reset(std::vector<GlyphTarget> targets) {
	_slots.clear();
	_separators.clear();
	_slots.reserve(targets.size());
	for (const auto &target : targets) {
		_slots.push_back(settledSlot(target));
		if (target.kind == GlyphKind::Group) {
			_separators.push_back(settledSeparator(_slots.back()));
		}
	}
	_targets = std::move(targets);
}

void GlyphFlow::edit(
		std::vector<GlyphTarget> targets,
		EditKind kind,
		crl::time now) {
	if (kind == EditKind::None && targets == _targets) {
		return;
	}
	const auto match = (kind == EditKind::None
		|| kind == EditKind::Immediate)
		? std::nullopt
		: EditMatch(_targets, targets, kind);
	if (!match) {
		reset(std::move(targets));
		return;
	}
	auto change = GlyphChange{
		.mode = GlyphMode::Scale,
		.ease = Ease::OutCubic,
		.slide = true,
		.durations = std::vector<crl::time>(targets.size(), kEditDuration),
	};
	apply(std::move(targets), *match, change, now);
}

void GlyphFlow::roll(
		std::vector<GlyphTarget> targets,
		GlyphMode mode,
		bool growing,
		crl::time now) {
	const auto count = int(targets.size());
	auto change = GlyphChange{
		.mode = mode,
		.side = growing ? -1 : 1,
		.ease = (mode == GlyphMode::Roll) ? Ease::OutCirc : Ease::OutCubic,
	};
	change.durations.reserve(count);
	for (auto i = 0; i != count; ++i) {
		change.durations.push_back((mode == GlyphMode::Roll)
			? (kSwitchFirstDuration
				+ (kSwitchDuration - kSwitchFirstDuration)
					* i
					/ std::max(count - 1, 1))
			: kEditDuration);
	}
	const auto match = PlaceMatch(_targets, targets);
	apply(std::move(targets), match, change, now);
}

void GlyphFlow::apply(
		std::vector<GlyphTarget> targets,
		const std::vector<int> &match,
		const GlyphChange &change,
		crl::time now) {
	auto alive = std::vector<int>();
	for (auto i = 0; i != int(_slots.size()); ++i) {
		if (!_slots[i].dying) {
			alive.push_back(i);
		}
	}
	if (alive.size() != _targets.size() || targets.empty()) {
		reset(std::move(targets));
		return;
	}
	const auto lefts = this->lefts(now);
	auto xs = std::vector<float64>();
	xs.reserve(_separators.size());
	for (const auto &separator : _separators) {
		xs.push_back(separatorX(separator, lefts, now));
	}
	auto result = std::vector<Slot>();
	result.reserve(_slots.size() + targets.size());
	auto killed = std::vector<uint32>();
	auto born = std::vector<uint32>();
	const auto kill = [&](Slot &&slot, crl::time duration) {
		if (!slot.dying) {
			slot.dying = true;
			slot.presence.retarget(0., now, duration, change.ease);
			if (slot.kind == GlyphKind::Group) {
				killed.push_back(slot.id);
			}
		}
		result.push_back(std::move(slot));
	};
	auto next = 0;
	for (auto j = 0; j != int(targets.size()); ++j) {
		const auto &target = targets[j];
		const auto duration = change.durations[j];
		if (match[j] < 0) {
			result.push_back(createSlot(target, change, duration, now));
			if (target.kind == GlyphKind::Group) {
				born.push_back(result.back().id);
			}
			continue;
		}
		const auto index = alive[match[j]];
		while (next < index) {
			kill(std::move(_slots[next++]), duration);
		}
		auto &slot = result.emplace_back(std::move(_slots[next++]));
		slot.kind = target.kind;
		slot.small = target.small;
		slot.place = target.place;
		slot.width.retarget(target.width, now, duration, change.ease);
		changeGlyph(slot, target, change, duration, now);
	}
	while (next < int(_slots.size())) {
		kill(std::move(_slots[next++]), change.durations.back());
	}
	_slots = std::move(result);
	_targets = std::move(targets);
	applySeparators(
		std::move(xs),
		std::move(killed),
		std::move(born),
		change,
		now);
}

void GlyphFlow::applySeparators(
		std::vector<float64> xs,
		std::vector<uint32> killed,
		std::vector<uint32> born,
		const GlyphChange &change,
		crl::time now) {
	auto orphans = std::vector<int>();
	for (auto i = 0; i != int(_separators.size()); ++i) {
		const auto &separator = _separators[i];
		if (separator.presence.to > 0.
			&& ranges::find(killed, separator.gap) != end(killed)) {
			orphans.push_back(i);
		}
	}
	ranges::sort(orphans, ranges::less(), [&](int i) {
		return slotIndex(_separators[i].gap);
	});
	if (change.slide) {
		const auto orphansCount = int(orphans.size());
		const auto bornCount = int(born.size());
		const auto count = std::min(orphansCount, bornCount);
		for (auto i = 0; i != count; ++i) {
			const auto index = orphans[orphansCount - 1 - i];
			auto &separator = _separators[index];
			const auto &gap = _slots[slotIndex(born[bornCount - 1 - i])];
			separator.ch = gap.current.ch;
			separator.gap = gap.id;
			separator.width = gap.width.to;
			separator.fromX = xs[index];
			separator.slide.jump(0.);
			separator.slide.retarget(
				1.,
				now,
				gap.presence.duration,
				change.ease);
		}
		orphans.resize(orphansCount - count);
		born.resize(bornCount - count);
	}
	for (const auto index : orphans) {
		auto &separator = _separators[index];
		separator.presence.retarget(
			0.,
			now,
			_slots[slotIndex(separator.gap)].presence.duration,
			change.ease);
	}
	const auto existing = int(_separators.size());
	for (auto i = 0; i != existing; ++i) {
		const auto index = slotIndex(_separators[i].gap);
		if (index < 0
			|| _slots[index].dying
			|| _separators[i].presence.to == 0.
			|| _separators[i].ch == _slots[index].current.ch) {
			continue;
		}
		const auto &gap = _slots[index];
		const auto duration = change.durations[ranges::count_if(
			_slots.begin(),
			_slots.begin() + index,
			[](const Slot &slot) { return !slot.dying; })];
		_separators[i].presence.retarget(0., now, duration, change.ease);
		auto added = settledSeparator(gap);
		added.presence.jump(0.);
		added.presence.retarget(1., now, duration, change.ease);
		_separators.push_back(std::move(added));
	}
	for (const auto id : born) {
		const auto &gap = _slots[slotIndex(id)];
		auto added = settledSeparator(gap);
		added.presence = gap.presence;
		_separators.push_back(std::move(added));
	}
	const auto rolls = [&](const GlyphLayer &layer) {
		return (layer.mode == GlyphMode::Roll) && layer.v.running(now);
	};
	const auto rolling = ranges::any_of(_slots, [&](const Slot &slot) {
		return rolls(slot.current) || ranges::any_of(slot.leaving, rolls);
	});
	if (rolling) {
		// Rolling digits cross the dip lane, so slides land in their gaps.
		for (auto &separator : _separators) {
			separator.slide.jump(1.);
		}
	}
}

void GlyphFlow::changeGlyph(
		Slot &slot,
		const GlyphTarget &target,
		const GlyphChange &change,
		crl::time duration,
		crl::time now) const {
	auto &current = slot.current;
	if (slot.kind == GlyphKind::Group || current.ch == target.ch) {
		current.ch = target.ch;
		current.width = target.width;
		return;
	}
	auto entering = GlyphLayer{
		.ch = target.ch,
		.mode = change.mode,
		.side = change.side,
	};
	const auto same = [&](const GlyphLayer &layer) {
		return (layer.ch == target.ch) && (layer.mode == change.mode);
	};
	const auto revived = ranges::find_if(slot.leaving, same);
	if (revived != end(slot.leaving)) {
		entering = std::move(*revived);
		slot.leaving.erase(revived);
	}
	for (auto &layer : slot.leaving) {
		// Restart on this change's clock, so the slot never sums above 1.
		layer.v.jump(layer.v.value(now));
		layer.v.retarget(0., now, duration, change.ease);
	}
	entering.width = target.width;
	entering.v.retarget(1., now, duration, change.ease);
	if (current.v.value(now) >= 1.) {
		// All modes paint the same at v == 1, so the hand-over is seamless.
		current.mode = change.mode;
		current.side = -change.side;
	}
	current.v.retarget(0., now, duration, change.ease);
	slot.leaving.push_back(std::move(current));
	current = std::move(entering);
}

Slot GlyphFlow::createSlot(
		const GlyphTarget &target,
		const GlyphChange &change,
		crl::time duration,
		crl::time now) {
	auto result = Slot{
		.id = ++_autoincrement,
		.kind = target.kind,
		.small = target.small,
		.place = target.place,
		.current = {
			.ch = target.ch,
			.mode = change.mode,
			.side = change.side,
			.width = target.width,
		},
	};
	result.width.jump(target.width);
	result.presence.retarget(1., now, duration, change.ease);
	if (change.mode == GlyphMode::Scale) {
		result.current.v.jump(1.);
	} else {
		result.current.v.retarget(1., now, duration, change.ease);
	}
	return result;
}

Slot GlyphFlow::settledSlot(const GlyphTarget &target) {
	auto result = Slot{
		.id = ++_autoincrement,
		.kind = target.kind,
		.small = target.small,
		.place = target.place,
		.current = {
			.ch = target.ch,
			.width = target.width,
		},
	};
	result.width.jump(target.width);
	result.presence.jump(1.);
	result.current.v.jump(1.);
	return result;
}

Separator GlyphFlow::settledSeparator(const Slot &gap) const {
	auto result = Separator{
		.ch = gap.current.ch,
		.gap = gap.id,
		.width = gap.width.to,
	};
	result.slide.jump(1.);
	result.presence.jump(1.);
	return result;
}

void GlyphFlow::prune(crl::time now) {
	const auto finished = [&](const Motion &motion) {
		return (motion.to == 0.) && !motion.running(now);
	};
	for (auto &slot : _slots) {
		slot.leaving.erase(
			ranges::remove_if(slot.leaving, finished, &GlyphLayer::v),
			end(slot.leaving));
	}
	_slots.erase(ranges::remove_if(_slots, [&](const Slot &slot) {
		return slot.dying && finished(slot.presence);
	}), end(_slots));
	_separators.erase(ranges::remove_if(_separators, [&](const auto &s) {
		return finished(s.presence) || (slotIndex(s.gap) < 0);
	}), end(_separators));
}

void GlyphFlow::finish() {
	for (auto &slot : _slots) {
		slot.width.jump(slot.width.to);
		slot.presence.jump(slot.presence.to);
		slot.current.v.jump(slot.current.v.to);
		slot.leaving.clear();
	}
	_slots.erase(ranges::remove_if(_slots, [](const Slot &slot) {
		return slot.dying;
	}), end(_slots));
	for (auto &separator : _separators) {
		separator.slide.jump(1.);
		separator.presence.jump(separator.presence.to);
	}
	_separators.erase(ranges::remove_if(_separators, [&](const auto &s) {
		return (s.presence.to == 0.) || (slotIndex(s.gap) < 0);
	}), end(_separators));
}

bool GlyphFlow::animating(crl::time now) const {
	for (const auto &slot : _slots) {
		if (slot.width.running(now)
			|| slot.presence.running(now)
			|| slot.current.v.running(now)
			|| ranges::any_of(slot.leaving, [&](const GlyphLayer &layer) {
				return layer.v.running(now);
			})) {
			return true;
		}
	}
	return ranges::any_of(_separators, [&](const Separator &separator) {
		return separator.slide.running(now)
			|| separator.presence.running(now);
	});
}

float64 GlyphFlow::width(crl::time now) const {
	auto result = 0.;
	for (const auto &slot : _slots) {
		result += slot.width.value(now) * slot.presence.value(now);
	}
	return result;
}

std::vector<float64> GlyphFlow::lefts(crl::time now) const {
	auto result = std::vector<float64>();
	result.reserve(_slots.size());
	auto left = 0.;
	for (const auto &slot : _slots) {
		result.push_back(left);
		left += slot.width.value(now) * slot.presence.value(now);
	}
	return result;
}

std::vector<float64> GlyphFlow::restLefts() const {
	auto result = std::vector<float64>();
	result.reserve(_slots.size());
	auto left = 0.;
	for (const auto &slot : _slots) {
		result.push_back(left);
		left += slot.width.to * slot.presence.to;
	}
	return result;
}

int GlyphFlow::slotIndex(uint32 id) const {
	const auto i = ranges::find(_slots, id, &Slot::id);
	return (i != end(_slots)) ? int(i - begin(_slots)) : -1;
}

float64 GlyphFlow::caretX(int position, crl::time now) const {
	const auto lefts = this->lefts(now);
	const auto index = gapIndex(position);
	return (index < int(lefts.size())) ? lefts[index] : width(now);
}

int GlyphFlow::gapIndex(int position) const {
	auto index = 0;
	auto result = 0;
	for (auto i = 0; i != int(_slots.size()); ++i) {
		const auto &slot = _slots[i];
		if (slot.dying || slot.kind == GlyphKind::Group) {
			continue;
		} else if (index++ == position) {
			return i;
		}
		result = i + 1;
	}
	return result;
}

float64 GlyphFlow::separatorX(
		const Separator &separator,
		const std::vector<float64> &lefts,
		crl::time now) const {
	const auto index = slotIndex(separator.gap);
	const auto gap = (index >= 0) ? lefts[index] : separator.fromX;
	return separator.fromX
		+ (gap - separator.fromX) * separator.slide.value(now);
}

std::vector<InkRange> GlyphFlow::visibleInks(
		const std::vector<float64> &lefts,
		crl::time now) const {
	auto result = std::vector<InkRange>();
	for (auto i = 0; i != int(_slots.size()); ++i) {
		const auto &slot = _slots[i];
		if (slot.kind == GlyphKind::Group) {
			continue;
		}
		const auto presence = slot.presence.value(now);
		const auto centre = lefts[i] + slot.width.value(now) * presence / 2.;
		const auto add = [&](const GlyphLayer &layer) {
			const auto v = layer.v.value(now);
			const auto scale = presence
				* ((layer.mode == GlyphMode::Scale) ? v : 1.);
			if (presence * v <= 0. || scale <= 0.) {
				return;
			}
			const auto ink = shape(slot.small, layer.ch).ink;
			result.push_back({
				.left = centre + scale * (ink.left() - layer.width / 2.),
				.right = centre + scale * (ink.right() - layer.width / 2.),
			});
		};
		add(slot.current);
		for (const auto &layer : slot.leaving) {
			add(layer);
		}
	}
	return result;
}

std::vector<InkRange> GlyphFlow::restInks(
		const std::vector<float64> &lefts) const {
	auto result = std::vector<InkRange>();
	for (auto i = 0; i != int(_slots.size()); ++i) {
		const auto &slot = _slots[i];
		if (slot.dying || slot.kind == GlyphKind::Group) {
			continue;
		}
		const auto ink = shape(slot.small, slot.current.ch).ink;
		result.push_back({
			.left = lefts[i] + ink.left(),
			.right = lefts[i] + ink.right(),
		});
	}
	return result;
}

float64 GlyphFlow::dip(
		const Separator &separator,
		float64 x,
		const std::vector<float64> &lefts,
		crl::time now) const {
	const auto ink = shape(false, separator.ch).ink;
	const auto index = slotIndex(separator.gap);
	if (!separator.slide.running(now) || ink.isEmpty() || index < 0) {
		return 0.;
	}
	const auto down = _digitsInk.bottom() - ink.top() + st::lineWidth;
	const auto up = ink.bottom() - _digitsInk.top() + st::lineWidth;
	const auto clearance = std::min(down, up);
	const auto range = [&](float64 left) {
		return InkRange{ left + ink.left(), left + ink.right() };
	};
	const auto rest = restLefts();
	const auto reach = Reach(range(x), visibleInks(lefts, now));
	const auto ramp = std::min(
		clearance,
		Reach(range(rest[index]), restInks(rest)));
	const auto factor = (reach <= 0.)
		? 1.
		: (reach >= ramp)
		? 0.
		: (1. - reach / ramp);
	return ((down <= up) ? 1. : -1.) * clearance * factor;
}

GlyphFlow::Shape GlyphFlow::shape(bool small, QChar ch) const {
	const auto key = std::make_pair(small, ch);
	const auto i = _shapes.find(key);
	if (i != end(_shapes)) {
		return i->second;
	}
	const auto &font = small ? *_small : *_big;
	auto result = Shape{
		.ink = font->metrics().tightBoundingRect(QString(ch)),
	};
	result.path.addText(0., 0., font->f, QString(ch));
	return _shapes.emplace(key, std::move(result)).first->second;
}

void GlyphFlow::paintLayer(
		QPainter &p,
		const Slot &slot,
		const GlyphLayer &layer,
		QPointF centre,
		float64 presence,
		const QColor &color,
		crl::time now) const {
	const auto v = layer.v.value(now);
	const auto scale = presence
		* ((layer.mode == GlyphMode::Scale) ? v : 1.);
	const auto opacity = presence * v;
	if (opacity <= 0. || scale <= 0.) {
		return;
	}
	const auto &font = slot.small ? *_small : *_big;
	const auto dy = (layer.mode == GlyphMode::Roll)
		? ((1. - v) * layer.side * font->height)
		: 0.;
	const auto glyph = shape(slot.small, layer.ch);
	const auto middle = glyph.ink.center().y();
	const auto was = p.opacity();
	p.save();
	p.setOpacity(was * opacity);
	p.translate(centre.x(), centre.y() + dy + middle);
	p.scale(scale, scale);
	p.translate(-layer.width / 2., -middle);
	p.fillPath(glyph.path, color);
	p.restore();
}

void GlyphFlow::paint(
		QPainter &p,
		QPointF origin,
		float64 baseline,
		const QColor &color,
		crl::time now,
		FlowGap gap) const {
	auto lefts = this->lefts(now);
	if (gap.width > 0.) {
		for (auto i = gapIndex(gap.position); i < int(lefts.size()); ++i) {
			lefts[i] += gap.width;
		}
	}
	for (auto i = 0; i != int(_slots.size()); ++i) {
		const auto &slot = _slots[i];
		if (slot.kind == GlyphKind::Group) {
			continue;
		}
		const auto presence = slot.presence.value(now);
		const auto centre = QPointF(
			origin.x() + lefts[i] + slot.width.value(now) * presence / 2.,
			origin.y() + baseline);
		for (const auto &layer : slot.leaving) {
			paintLayer(p, slot, layer, centre, presence, color, now);
		}
		paintLayer(p, slot, slot.current, centre, presence, color, now);
	}
	const auto was = p.opacity();
	for (const auto &separator : _separators) {
		const auto presence = separator.presence.value(now);
		if (presence <= 0.) {
			continue;
		}
		const auto x = separatorX(separator, lefts, now);
		const auto index = slotIndex(separator.gap);
		const auto scale = (separator.slide.running(now) || index < 0)
			? presence
			: std::min(presence, _slots[index].presence.value(now));
		const auto glyph = shape(false, separator.ch);
		const auto middle = glyph.ink.center().y();
		p.save();
		p.setOpacity(was * presence);
		p.translate(
			origin.x() + x + separator.width * scale / 2.,
			origin.y() + baseline + dip(separator, x, lefts, now) + middle);
		p.scale(scale, scale);
		p.translate(-separator.width / 2., -middle);
		p.fillPath(glyph.path, color);
		p.restore();
	}
}

}

}
