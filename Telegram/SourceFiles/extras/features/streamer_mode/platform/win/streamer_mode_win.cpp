
#include "extras/features/streamer_mode/platform/win/streamer_mode_win.h"

#include <QtWidgets/QWidget>

#include <windows.h>

namespace ExtrasFeatures::StreamerMode::Platform {

void SetWindowCaptureExcluded(
		not_null<QWidget*> widget,
		bool excluded) {
	const auto handle = reinterpret_cast<HWND>(widget->winId());
	SetWindowDisplayAffinity(
		handle,
		excluded ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
}

} // namespace ExtrasFeatures::StreamerMode::Platform
