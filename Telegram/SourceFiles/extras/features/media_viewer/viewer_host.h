#pragma once

#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include <QtCore/QMargins>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <memory>

class QAbstractNativeEventFilter;

namespace Ui {
class LayerWidget;
class RpWidget;
class RpWindow;
} // namespace Ui

namespace ExtrasFeatures::WindowMaterial::Platform {
class Backend;
} // namespace ExtrasFeatures::WindowMaterial::Platform

namespace ExtrasFeatures::MediaViewer {

[[nodiscard]] QColor backdropTint();
// 渲染 rect 区域并垫上窗口底色，开启材质时也不透明；只在主线程调用。
[[nodiscard]] QImage grabOpaque(not_null<QWidget*> widget, QRect rect = QRect());
// 暂时隐藏图层栈，渲染图层所在区域的底层界面并恢复焦点；只在主线程调用。
[[nodiscard]] QImage grabUnderLayer(not_null<Ui::LayerWidget*> layer);
// 模糊并叠加暗色，与查看器背景一致；不访问样式，可在工作线程调用。
[[nodiscard]] QImage prepareBackdrop(const QImage &image);

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
	// 宿主带系统圆角时返回系统边框内沿的圆角半径，否则为 0。
	[[nodiscard]] float64 cornerRadius() const;
	// 系统边框内沿相对客户区的偏移，负值表示边框压在客户区上。
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
	float64 _cornerRadius = 0.;
	std::unique_ptr<QAbstractNativeEventFilter> _activationFilter;
	std::unique_ptr<WindowMaterial::Platform::Backend> _backdropBackend;
	bool _nativeBackdrop = false;

};

// 文章媒体使用真正的子控件，宿主销毁前把控件交回原父级。
class EmbeddedViewerHost final : public base::has_weak_ptr {
public:
	EmbeddedViewerHost(
		not_null<QWidget*> viewer,
		not_null<Ui::RpWidget*> host,
		Fn<void()> geometryChanged,
		Fn<void()> hostHidden,
		Fn<void()> hostDestroyed);
	~EmbeddedViewerHost();

	[[nodiscard]] QRect geometry() const;
	[[nodiscard]] bool hosts(not_null<QWidget*> window) const;
	[[nodiscard]] QImage grabBackdrop() const;
	void activate() const;
	void minimizeHost() const;
	void restoreFocus() const;
	void setContentScreenshotProtected(bool protectedContent) const;

private:
	void restoreScreenshotProtection() const;

	const not_null<QWidget*> _viewer;
	const QPointer<QWidget> _host;
	const QPointer<Ui::RpWindow> _window;
	const QPointer<QWidget> _originalParent;
	QPointer<QWidget> _previousFocus;
	bool _previousScreenshotProtected = false;
	bool _hostClosing = false;
	base::unique_qptr<QObject> _filter;
	rpl::lifetime _lifetime;

};

} // namespace ExtrasFeatures::MediaViewer
