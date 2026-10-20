#pragma once

class QWidget;

namespace ExtrasFeatures::StreamerMode {

void bindWidgetWindow(
	not_null<QWidget*> widget,
	uint64 userId,
	rpl::lifetime &lifetime);
void inheritWidgetWindow(not_null<QWidget*> widget);
void inheritWidgetWindow(not_null<QWidget*> widget, not_null<QWidget*> owner);
void refreshWidgetWindow(not_null<QWidget*> widget);

} // namespace ExtrasFeatures::StreamerMode
