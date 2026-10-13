#include "extras/features/video_cover/video_cover.h"

#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "window/window_session_controller.h"

namespace ExtrasVideoCover {
namespace {

// 缩略图只有几百像素，按视频尺寸声明大小，查看器像播放时一样放大铺满。
[[nodiscard]] QSize displaySize(
		not_null<DocumentData*> document,
		QSize thumbnail) {
	const auto video = document->dimensions.isEmpty()
		? thumbnail
		: document->dimensions;
	const auto limit = PhotoData::SideLimit();
	const auto result = thumbnail.scaled(video, Qt::KeepAspectRatio);
	return result.scaled(
		result.boundedTo(QSize(limit, limit)),
		Qt::KeepAspectRatio);
}

// 服务器缩略图包装成图片，编号沿用视频编号，下载与缓存沿用缩略图的文件位置。
[[nodiscard]] not_null<PhotoData*> thumbnailPhoto(
		not_null<DocumentData*> document,
		TimeId date) {
	const auto owner = &document->owner();
	const auto id = PhotoId(document->id);
	const auto existing = owner->photo(id);
	if (!existing->isNull()) {
		return existing;
	}
	auto large = ImageWithLocation{
		.location = document->thumbnailLocation(),
		.bytesCount = document->thumbnailByteSize(),
	};
	const auto display = displaySize(
		document,
		QSize(large.location.width(), large.location.height()));
	large.location.setSize(display.width(), display.height());
	return owner->photo(
		id,
		uint64(),
		QByteArray(),
		date,
		document->getDC(),
		false,
		document->inlineThumbnailBytes(),
		ImageWithLocation(),
		ImageWithLocation(),
		large,
		ImageWithLocation(),
		ImageWithLocation(),
		crl::time(0));
}

} // namespace

bool hasCover(
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document) {
	return document->isVideoFile()
		&& (LookupVideoCover(document, item) || document->hasThumbnail());
}

void showCover(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document) {
	Expects(hasCover(item, document));

	const auto cover = LookupVideoCover(document, item);
	const auto photo = cover
		? not_null<PhotoData*>(cover)
		: thumbnailPhoto(document, item->date());
	// 挂到消息的即时预览图集上，查看器只显示这一张，不混入会话里的其他媒体。
	item->addPhotoForInstantView(photo);
	controller->openPhoto(photo, {
		.id = item->fullId(),
		.topicRootId = item->topicRootId(),
		.monoforumPeerId = item->sublistPeerId(),
	});
}

} // namespace ExtrasVideoCover
