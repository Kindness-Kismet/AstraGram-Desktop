/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_rows_shift.h"

#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_list.h"
#include "dialogs/dialogs_row.h"
#include "ui/painter.h"
#include "styles/style_extras_styles.h"

#include <unordered_map>

namespace Dialogs {
namespace {

[[nodiscard]] crl::time ShiftDuration() {
	return crl::time(base::SafeRound(
		st::extrasInPlaceFadeDuration * anim::DurationMultiplier()));
}

// 最长递增子序列里的行保持相对顺序，只是被挤动；其余行视为离开原位。
[[nodiscard]] std::vector<bool> KeptIndices(const std::vector<int> &order) {
	const auto count = int(order.size());
	auto tails = std::vector<int>();
	auto previous = std::vector<int>(count, -1);
	for (auto i = 0; i != count; ++i) {
		const auto j = int(ranges::lower_bound(
			tails,
			order[i],
			ranges::less(),
			[&](int index) { return order[index]; }) - begin(tails));
		if (j > 0) {
			previous[i] = tails[j - 1];
		}
		if (j == int(tails.size())) {
			tails.push_back(i);
		} else {
			tails[j] = i;
		}
	}
	auto result = std::vector<bool>(count, false);
	for (auto i = tails.empty() ? -1 : tails.back(); i >= 0; i = previous[i]) {
		result[i] = true;
	}
	return result;
}

} // namespace

RowsShift::RowsShift(Fn<void()> repaint, GhostFactory ghost)
: _repaint(std::move(repaint))
, _ghost(std::move(ghost))
, _animation([=](crl::time now) { return step(now); }) {
}

RowsShift::~RowsShift() = default;

bool RowsShift::animating() const {
	return !_leaving.empty() || !_arriving.empty();
}

void RowsShift::clear() {
	_animation.stop();
	_leaving.clear();
	_arriving.clear();
}

float64 RowsShift::progress(
		crl::time started,
		crl::time duration,
		crl::time now) const {
	if (duration <= 0) {
		return 1.;
	}
	const auto linear = std::clamp(
		(now - started) / float64(duration),
		0.,
		1.);
	return anim::easeOutCubic(1., linear);
}

int RowsShift::arrivingHeight(
		const Arriving &arriving,
		int full,
		crl::time now) const {
	const auto hidden = 1. - progress(arriving.started, arriving.duration, now);
	return full - int(base::SafeRound(full * hidden));
}

void RowsShift::leave(not_null<Row*> row, Key next, bool keepSlot) {
	const auto key = row->key();
	const auto now = crl::now();
	for (auto &leaving : _leaving) {
		if (leaving.next == key) {
			leaving.next = next;
		}
	}
	auto height = row->height();
	auto shown = 1.;
	const auto i = ranges::find(_arriving, key, &Arriving::key);
	if (i != end(_arriving)) {
		height = arrivingHeight(*i, height, now);
		shown = progress(i->started, i->duration, now);
		_arriving.erase(i);
	}
	if (!keepSlot || height <= 0) {
		return;
	}
	_leaving.push_back({
		.ghost = _ghost(row),
		.key = key,
		.next = next,
		.height = height,
		.shown = shown,
		.started = now,
		.duration = ShiftDuration(),
	});
	startAnimation();
}

void RowsShift::arrive(Key key) {
	_arriving.erase(
		ranges::remove(_arriving, key, &Arriving::key),
		end(_arriving));
	_arriving.push_back({
		.key = key,
		.started = crl::now(),
		.duration = ShiftDuration(),
	});
	startAnimation();
}

void RowsShift::forget(not_null<const Entry*> entry) {
	for (auto &leaving : _leaving) {
		if (leaving.key.entry() == entry) {
			leaving.ghost = nullptr;
		}
	}
	_arriving.erase(ranges::remove_if(_arriving, [&](const Arriving &a) {
		return (a.key.entry() == entry);
	}), end(_arriving));
}

RowsShift::Order RowsShift::Snapshot(const List &list) {
	auto result = Order();
	result.rows.reserve(list.size());
	result.tops.reserve(list.size());
	for (const auto &row : list) {
		result.rows.push_back(row);
		result.tops.push_back(row->top());
	}
	return result;
}

void RowsShift::reordered(
		const Order &was,
		const List &list,
		int from,
		int till) {
	const auto count = int(was.rows.size());
	if (count != list.size()) {
		return;
	}
	auto changed = false;
	for (auto i = 0; i != count; ++i) {
		if (was.rows[i] != *(list.cbegin() + i)) {
			changed = true;
			break;
		}
	}
	if (!changed) {
		return;
	}
	auto wasIndex = std::unordered_map<const Row*, int>();
	wasIndex.reserve(count);
	for (auto i = 0; i != count; ++i) {
		wasIndex.emplace(was.rows[i].get(), i);
	}
	auto order = std::vector<int>();
	order.reserve(count);
	for (const auto &row : list) {
		order.push_back(wasIndex[row.get()]);
	}
	const auto keptByNow = KeptIndices(order);
	auto kept = std::vector<bool>(count, false);
	for (auto i = 0; i != count; ++i) {
		kept[order[i]] = keptByNow[i];
	}

	// 离开的行在原位挂到其后第一个未离开的行前面。
	auto next = Key();
	auto moved = std::vector<std::pair<int, Key>>();
	for (auto i = count; i != 0;) {
		--i;
		const auto row = was.rows[i];
		if (kept[i]) {
			next = row->key();
			continue;
		}
		moved.emplace_back(i, next);
	}
	ranges::reverse(moved);
	for (const auto &[index, anchor] : moved) {
		const auto row = was.rows[index];
		const auto height = row->height();
		const auto wasTop = was.tops[index];
		const auto nowTop = row->top();
		const auto above = (wasTop + height <= from)
			&& (nowTop + height <= from);
		leave(row, anchor, !above && (wasTop < till));
		if (!above && (nowTop < till)) {
			arrive(row->key());
		}
	}
}

void RowsShift::enumerate(
		const List &list,
		int from,
		int till,
		Fn<bool(const Piece&)> callback) const {
	struct Slot {
		int index = 0;
		int order = 0;
		int height = 0;
		float64 shown = 0.;
		Row *ghost = nullptr;
	};
	struct Growing {
		int index = 0;
		int height = 0;
		int full = 0;
		float64 shown = 0.;
	};
	const auto now = crl::now();
	const auto count = list.size();
	auto slots = std::vector<Slot>();
	auto slotsHeight = 0;
	for (auto i = 0; i != int(_leaving.size()); ++i) {
		const auto &leaving = _leaving[i];
		const auto next = leaving.next ? list.getRow(leaving.next) : nullptr;
		if (leaving.next && !next) {
			continue;
		}
		const auto hidden = 1.
			- progress(leaving.started, leaving.duration, now);
		const auto height = int(base::SafeRound(leaving.height * hidden));
		if (height <= 0) {
			continue;
		}
		slots.push_back({
			.index = next ? next->index() : count,
			.order = i,
			.height = height,
			.shown = leaving.shown * hidden,
			.ghost = leaving.ghost.get(),
		});
		slotsHeight += height;
	}
	ranges::sort(slots, [](const Slot &a, const Slot &b) {
		return std::pair(a.index, a.order) < std::pair(b.index, b.order);
	});
	auto growing = std::vector<Growing>();
	for (const auto &arriving : _arriving) {
		const auto row = list.getRow(arriving.key);
		if (!row) {
			continue;
		}
		const auto full = row->height();
		growing.push_back({
			.index = row->index(),
			.height = arrivingHeight(arriving, full, now),
			.full = full,
			.shown = progress(arriving.started, arriving.duration, now),
		});
	}
	ranges::sort(growing, ranges::less(), &Growing::index);

	// 收起行最多把前面的行往下推 slotsHeight，从更靠前的行开始找。
	const auto start = int(list.findByY(from - slotsHeight) - list.cbegin());
	auto top = (start < count)
		? (*(list.cbegin() + start))->top()
		: list.height();
	auto slot = begin(slots);
	for (; slot != end(slots) && slot->index < start; ++slot) {
		top += slot->height;
	}
	auto grow = begin(growing);
	for (; grow != end(growing) && grow->index < start; ++grow) {
		top -= grow->full - grow->height;
	}
	for (auto index = start; index <= count; ++index) {
		for (; slot != end(slots) && slot->index == index; ++slot) {
			if (top >= till) {
				return;
			}
			const auto piece = Piece{
				.row = slot->ghost,
				.top = top,
				.height = slot->height,
				.shown = slot->shown,
				.ghost = true,
			};
			if (top + piece.height > from && !callback(piece)) {
				return;
			}
			top += piece.height;
		}
		if (index == count || top >= till) {
			return;
		}
		const auto row = (list.cbegin() + index)->get();
		auto piece = Piece{
			.row = row,
			.top = top,
			.height = row->height(),
		};
		if (grow != end(growing) && grow->index == index) {
			piece.height = grow->height;
			piece.shown = grow->shown;
			++grow;
		}
		if (piece.height > 0
			&& top + piece.height > from
			&& !callback(piece)) {
			return;
		}
		top += piece.height;
	}
}

RowsShift::Piece RowsShift::pieceAt(const List &list, int y) const {
	auto result = Piece();
	enumerate(list, y, y + 1, [&](const Piece &piece) {
		if (piece.top <= y && piece.top + piece.height > y) {
			result = piece;
			return false;
		}
		return true;
	});
	return result;
}

bool RowsShift::step(crl::time now) {
	const auto finished = [&](crl::time started, crl::time duration) {
		return (now >= started + duration);
	};
	_leaving.erase(ranges::remove_if(_leaving, [&](const Leaving &l) {
		return finished(l.started, l.duration);
	}), end(_leaving));
	_arriving.erase(ranges::remove_if(_arriving, [&](const Arriving &a) {
		return finished(a.started, a.duration);
	}), end(_arriving));
	_repaint();
	return animating();
}

void RowsShift::startAnimation() {
	if (!_animation.animating()) {
		_animation.start();
	}
	_repaint();
}

void PaintShiftedRow(
		Painter &p,
		QSize size,
		int height,
		float64 shown,
		const QBrush &bg,
		Fn<void(Painter&)> paint) {
	p.save();
	p.setClipRect(0, 0, size.width(), height, Qt::IntersectClip);
	if (shown >= 1.) {
		paint(p);
		p.restore();
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	auto layer = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	layer.setDevicePixelRatio(ratio);
	layer.fill(Qt::transparent);
	{
		auto q = Painter(&layer);
		q.setInactive(p.inactive());
		paint(q);
	}
	auto frame = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	frame.setDevicePixelRatio(ratio);
	frame.fill(Qt::transparent);
	{
		auto q = QPainter(&frame);
		q.fillRect(QRect(QPoint(), size), bg);
		// Source 模式带不透明度 o 时结果为 o * 行内容 + (1 - o) * 底色。
		q.setCompositionMode(QPainter::CompositionMode_Source);
		q.setOpacity(shown);
		q.drawImage(0, 0, layer);
	}
	p.drawImage(0, 0, frame);
	p.restore();
}

} // namespace Dialogs
