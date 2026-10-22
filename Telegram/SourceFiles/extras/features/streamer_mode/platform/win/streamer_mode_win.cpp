
#include "extras/features/streamer_mode/platform/win/streamer_mode_win.h"

#include <QtWidgets/QWidget>

#include <windows.h>

namespace ExtrasFeatures::StreamerMode::Platform {

void SetWindowCaptureExcluded(
		not_null<QWidget*> widget,
		bool excluded) {
	const auto handle = reinterpret_cast<HWND>(widget->winId());
	// 亲和性另有防截屏等写入方，按系统实际状态比较；一致时不调用，新窗口保持不被触碰。
	auto current = DWORD(WDA_NONE);
	if (GetWindowDisplayAffinity(handle, &current)
		&& ((current != WDA_NONE) == excluded)) {
		return;
	}
	SetWindowDisplayAffinity(
		handle,
		excluded ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
}

} // namespace ExtrasFeatures::StreamerMode::Platform
