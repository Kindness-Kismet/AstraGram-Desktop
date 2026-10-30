#pragma once

class DocumentData;
class HistoryItem;
class PhotoData;

namespace Window {
class SessionController;
} // namespace Window

namespace ExtrasVideoCover {

// 视频带有上传者设置的封面或服务器生成的缩略图时才能查看。
[[nodiscard]] bool hasCover(
	not_null<HistoryItem*> item,
	not_null<DocumentData*> document);

// 图片尺寸和下载位置沿用封面源，优先上传者设置的封面，其次服务器缩略图。
[[nodiscard]] not_null<PhotoData*> photoForCover(
	not_null<HistoryItem*> item,
	not_null<DocumentData*> document);

// 封面沿用图片查看器，消息上下文保留原视频。
void showCover(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item,
	not_null<DocumentData*> document);

} // namespace ExtrasVideoCover
