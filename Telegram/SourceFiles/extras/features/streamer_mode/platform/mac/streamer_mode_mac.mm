
#include "extras/features/streamer_mode/platform/mac/streamer_mode_mac.h"

#include <QtWidgets/QWidget>

#include <Cocoa/Cocoa.h>

namespace ExtrasFeatures::StreamerMode::Platform {

void SetWindowCaptureExcluded(
		not_null<QWidget*> widget,
		bool excluded) {
	const auto view = reinterpret_cast<NSView*>(widget->winId());
	view.window.sharingType = excluded
		? NSWindowSharingNone
		: NSWindowSharingReadOnly;
}

} // namespace ExtrasFeatures::StreamerMode::Platform
