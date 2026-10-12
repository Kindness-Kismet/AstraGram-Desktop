#pragma once

class DocumentData;
class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

namespace ExtrasVideoPreview {

// 服务器为视频生成了缩略图拼图及时间索引时才能预览。
[[nodiscard]] bool hasPreview(not_null<DocumentData*> document);

// 弹窗按时间列出缩略图，点击后从对应位置开始播放。
void showBox(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item,
	not_null<DocumentData*> document);

} // namespace ExtrasVideoPreview
