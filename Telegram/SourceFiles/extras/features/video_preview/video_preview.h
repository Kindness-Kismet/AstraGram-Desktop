#pragma once

class DocumentData;
class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

namespace ExtrasVideoPreview {

// 服务器为视频生成了缩略图拼图及时间索引时才能预览。
[[nodiscard]] bool hasPreview(not_null<DocumentData*> document);

// 在媒体查看器里逐帧大图浏览，说明文字中的链接从该帧时间开始播放。
void showFrames(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item,
	not_null<DocumentData*> document);

} // namespace ExtrasVideoPreview
