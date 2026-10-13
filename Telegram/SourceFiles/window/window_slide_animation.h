/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {

enum class SlideDirection {
	FromRight,
	FromLeft,
	FromBottom,
};

// 原地交叉淡化：内容沿方向平移 shift，bg 铺满且保持不动。
struct CrossFadeArgs {
	int shift = 0;
	QColor bg;
	// 上层直接盖在下层上，前后相同的像素全程不变，不使用 bg。
	// 前进时新内容是上层，后退时旧内容是上层，只有上层平移。
	bool overlay = false;
	// 为 0 时沿用 st::slideDuration。
	int duration = 0;
};

// 在 parent 的 geometry 区域把旧画面原地交叉淡化到新画面。
void ShowCrossFadeAnimation(
	not_null<QWidget*> parent,
	const QRect &geometry,
	QPixmap oldContentCache,
	QPixmap newContentCache,
	SlideDirection direction,
	CrossFadeArgs args);

// 旧画面盖住 parent 的 geometry 区域淡出并向右平移 shift，底下实时内容的变化同样随之淡入。
// 返回结束后自行删除的过渡层。
not_null<Ui::RpWidget*> ShowFadeOutAnimation(
	not_null<QWidget*> parent,
	const QRect &geometry,
	QPixmap cache,
	crl::time duration,
	int shift = 0);

class SlideAnimation {
public:
	void paintContents(QPainter &p) const;

	[[nodiscard]] float64 progress() const;

	void setDirection(SlideDirection direction);
	void setPixmaps(
		const QPixmap &oldContentCache,
		const QPixmap &newContentCache);
	void setTopBarShadow(bool enabled);
	void setTopSkip(int skip);
	void setTopBarMask(const QPixmap &mask);
	void setWithFade(bool withFade);
	void setCrossFade(CrossFadeArgs args);

	using RepaintCallback = Fn<void()>;
	void setRepaintCallback(RepaintCallback &&callback);

	using FinishedCallback = Fn<void()>;
	void setFinishedCallback(FinishedCallback &&callback);

	void start();

	static const anim::transition &transition() {
		return anim::easeOutCirc;
	}

private:
	void animationCallback();
	void paintCrossFade(QPainter &p) const;
	void paintCrossFadeFrame(QPainter &p) const;

	SlideDirection _direction = SlideDirection::FromRight;
	int _topSkip = 0;
	bool _topBarShadowEnabled = false;
	bool _withFade = false;
	std::optional<CrossFadeArgs> _crossFade;
	mutable QImage _crossFadeFrame;

	mutable Ui::Animations::Simple _animation;
	QPixmap _cacheUnder, _cacheOver;
	QPixmap _mask;

	RepaintCallback _repaintCallback;
	FinishedCallback _finishedCallback;

};

} // namespace Window
