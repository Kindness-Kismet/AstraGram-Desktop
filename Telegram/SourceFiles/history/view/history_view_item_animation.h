#pragma once

#include "ui/effects/animations.h"

class Painter;

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {

class Element;

inline constexpr auto kItemExpandDuration = crl::time(220);
inline constexpr auto kItemFadeDuration = crl::time(160);
inline constexpr auto kItemRevealDuration = kItemExpandDuration + kItemFadeDuration;

[[nodiscard]] float64 ItemExpansionProgress(float64 progress);
[[nodiscard]] float64 ItemRevealOpacity(float64 progress);
void PaintFadingItem(
	Painter &p,
	not_null<Element*> view,
	const Ui::ChatPaintContext &context,
	float64 opacity);

} // namespace HistoryView
