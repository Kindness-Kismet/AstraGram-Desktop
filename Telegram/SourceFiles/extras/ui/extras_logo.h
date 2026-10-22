#pragma once

namespace ExtrasAssets {

enum class AppIcoStatus {
	Unchanged,
	Updated,
	Failed,
};

// 把内置图标写到 tdata，供任务栏与快捷方式引用；内容一致时不重写。
[[nodiscard]] AppIcoStatus loadAppIco();
[[nodiscard]] QString appIcoPath();

[[nodiscard]] QImage currentAppLogo();

} // namespace ExtrasAssets
