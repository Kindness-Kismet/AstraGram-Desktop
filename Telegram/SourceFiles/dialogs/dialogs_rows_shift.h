/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_key.h"
#include "ui/effects/animations.h"

class Painter;

namespace Dialogs {

class Entry;
class List;
class Row;

// 会话列表换位过渡：离开的行在原位收起淡出，到达的行在新位展开淡入。
// 绘制位置等于各行累加的当前高度，任何时刻行与行都不会重叠。
class RowsShift final {
public:
	using GhostFactory = Fn<std::unique_ptr<Row>(not_null<const Row*>)>;

	RowsShift(Fn<void()> repaint, GhostFactory ghost);
	~RowsShift();

	struct Piece {
		Row *row = nullptr; // 列表里的行或收起中的副本
		int top = 0; // 列表坐标
		int height = 0; // 当前占用高度，不超过行高
		float64 shown = 1.; // 内容不透明度
		bool ghost = false;
	};

	// 解冻前的行顺序与位置，用来找出解冻后离开原位的行。
	struct Order {
		std::vector<not_null<Row*>> rows;
		std::vector<int> tops;
	};

	[[nodiscard]] bool animating() const;
	void clear();

	// row 即将离开原位，next 是原位之后的行（空键为列表末尾）。
	// keepSlot 为假时原位不留收起过程，只转移挂在 row 前的收起行。
	void leave(not_null<Row*> row, Key next, bool keepSlot);
	// key 对应的行刚到达新位置，从零高度展开。
	void arrive(Key key);
	// 条目销毁后不再绘制它的收起副本，只保留高度收起。
	void forget(not_null<const Entry*> entry);

	[[nodiscard]] static Order Snapshot(const List &list);
	// 对比解冻前后的顺序，[from, till) 是可见范围（列表坐标）。
	// 可见范围以下的变化看不到；原位和新位都在上方时互相抵消，都不做过渡。
	void reordered(const Order &was, const List &list, int from, int till);

	// 按绘制顺序枚举与 [from, till) 相交的片段，回调返回假时停止。
	void enumerate(
		const List &list,
		int from,
		int till,
		Fn<bool(const Piece&)> callback) const;
	[[nodiscard]] Piece pieceAt(const List &list, int y) const;

private:
	struct Leaving {
		std::unique_ptr<Row> ghost;
		Key key;
		Key next;
		int height = 0;
		float64 shown = 1.;
		crl::time started = 0;
		crl::time duration = 0;
	};
	struct Arriving {
		Key key;
		crl::time started = 0;
		crl::time duration = 0;
	};

	[[nodiscard]] float64 progress(
		crl::time started,
		crl::time duration,
		crl::time now) const;
	[[nodiscard]] int arrivingHeight(
		const Arriving &arriving,
		int full,
		crl::time now) const;
	bool step(crl::time now);
	void startAnimation();

	Fn<void()> _repaint;
	GhostFactory _ghost;
	std::vector<Leaving> _leaving;
	std::vector<Arriving> _arriving;
	Ui::Animations::Basic _animation;

};

// 先铺底色，再按不透明度逐像素替换为行内容，只保留顶部 height 高的部分。
void PaintShiftedRow(
		Painter &p,
		QSize size,
		int height,
		float64 shown,
		const QBrush &bg,
		Fn<void(Painter&)> paint);

} // namespace Dialogs
