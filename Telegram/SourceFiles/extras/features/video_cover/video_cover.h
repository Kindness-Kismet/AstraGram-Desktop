#pragma once

class DocumentData;
class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

namespace ExtrasVideoCover {

// 视频带有上传者设置的封面或服务器生成的缩略图时才能查看。
[[nodiscard]] bool hasCover(
	not_null<HistoryItem*> item,
	not_null<DocumentData*> document);

// 在媒体查看器里单独查看封面，优先上传者设置的封面，其次服务器缩略图。
void showCover(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item,
	not_null<DocumentData*> document);

} // namespace ExtrasVideoCover
