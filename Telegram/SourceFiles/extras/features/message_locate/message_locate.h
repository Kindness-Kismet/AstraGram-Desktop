#pragma once

#include "ui/chat/message_bubble.h"

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace ExtrasFeatures::MessageLocate {

void paint(
	QPainter &p,
	const Ui::ChatPaintContext &context,
	QRect geometry,
	Ui::BubbleRounding rounding,
	int outerWidth,
	bool preferLeft,
	int leftLimit = 0,
	QRect action = {});

} // namespace ExtrasFeatures::MessageLocate
