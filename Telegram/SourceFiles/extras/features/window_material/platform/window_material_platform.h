#pragma once

#include "base/basic_types.h"
#include <memory>
#include <vector>

class QWidget;
enum class WindowMaterial;

namespace ExtrasFeatures::WindowMaterial::Platform {

class Backend {
public:
	virtual ~Backend() = default;
	[[nodiscard]] virtual bool apply(::WindowMaterial mode, bool dark) = 0;

	// 以下供独立面板和菜单使用，平台不支持时保持原样。
	[[nodiscard]] virtual bool panelSupported(::WindowMaterial mode) {
		return false;
	}
	[[nodiscard]] virtual bool setRoundedCorners(bool rounded) {
		return false;
	}
	virtual void activatePopupBackdrop() {
	}
	// 撤销全部原生设置，窗口恢复原有的透明绘制方式。
	virtual void release() {
	}
};

[[nodiscard]] std::unique_ptr<Backend> create(not_null<QWidget*> window);
[[nodiscard]] std::vector<::WindowMaterial> availableModes();

} // namespace ExtrasFeatures::WindowMaterial::Platform
