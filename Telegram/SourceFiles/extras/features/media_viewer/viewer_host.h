#pragma once

#include "base/unique_qptr.h"
#include <QtCore/QMargins>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <memory>

namespace ExtrasFeatures::WindowMaterial::Platform {
class Backend;
} // namespace ExtrasFeatures::WindowMaterial::Platform

namespace ExtrasFeatures::MediaViewer {

[[nodiscard]] QColor backdropTint();

// 设置开启且平台支持时，查看器只铺满打开它的主窗口。
[[nodiscard]] bool inWindowEnabled();

// 查看器挂到宿主主窗口下：不单独占任务栏，始终在宿主之上，随宿主移动和缩放。
class ViewerHost final {
public:
	ViewerHost(
		not_null<QWidget*> viewer,
		not_null<QWidget*> host,
		Fn<void()> geometryChanged,
		Fn<void()> hostHidden);
	~ViewerHost();

	[[nodiscard]] QRect geometry() const;
	[[nodiscard]] QScreen *screen() const;
	// 宿主带系统圆角时返回圆角半径，否则为 0。
	[[nodiscard]] int cornerRadius() const;
	[[nodiscard]] QMarginsF cornerMargins() const;
	[[nodiscard]] QImage grabBackdrop() const;
	void setFullScreenBackdrop(bool enabled);
	[[nodiscard]] bool nativeBackdrop() const;
	[[nodiscard]] bool hosts(not_null<QWidget*> window) const;
	void minimizeHost() const;

private:
	void updateCorners();

	const not_null<QWidget*> _viewer;
	const QPointer<QWidget> _host;
	base::unique_qptr<QObject> _filter;
	QMarginsF _cornerMargins;
	int _cornerRadius = 0;
	std::unique_ptr<WindowMaterial::Platform::Backend> _backdropBackend;
	bool _nativeBackdrop = false;

};

} // namespace ExtrasFeatures::MediaViewer
