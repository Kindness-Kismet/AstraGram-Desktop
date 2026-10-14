#pragma once

class HistoryItem;

namespace ExtrasFeatures::MediaViewer {

struct MediaSource {
	QRect geometry; // 全局坐标。
	int radius = 0;
};

// 消息媒体在宿主窗口聊天里的屏幕位置，相册取对应格子；不在可见区域时返回空。
[[nodiscard]] std::optional<MediaSource> findMediaSource(
	not_null<QWidget*> host,
	not_null<HistoryItem*> item);

} // namespace ExtrasFeatures::MediaViewer
