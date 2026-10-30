#pragma once

#include "ui/effects/animations.h"

class Painter;

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {

class Element;

struct ItemRevealState {
	float64 progress = 1.;
	float64 shift = 0.;
};

[[nodiscard]] crl::time ItemRevealDuration(int height);
[[nodiscard]] int ItemRevealDisplacement(int height);
[[nodiscard]] float64 ItemExpansionProgress(float64 progress);
[[nodiscard]] float64 ItemRevealOpacity(float64 progress);
void PaintItemReveal(
	Painter &p,
	QRect bounds,
	QRect clip,
	ItemRevealState state,
	int bottom,
	Fn<void(Painter&, int)> paint);
void PaintRevealingItem(
	Painter &p,
	not_null<Element*> view,
	const Ui::ChatPaintContext &context,
	ItemRevealState state,
	int bottom);

} // namespace HistoryView
