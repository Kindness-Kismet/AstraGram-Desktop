#pragma once

#include "base/unique_qptr.h"

namespace ExtrasFeatures::MediaViewer {

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
	[[nodiscard]] bool hosts(not_null<QWidget*> window) const;
	void minimizeHost() const;

private:
	const not_null<QWidget*> _viewer;
	const QPointer<QWidget> _host;
	base::unique_qptr<QObject> _filter;

};

} // namespace ExtrasFeatures::MediaViewer
